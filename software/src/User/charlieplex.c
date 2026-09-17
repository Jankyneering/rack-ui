/* charlieplex.c */
#include "charlieplex.h"
#include <string.h>

static uint8_t charlie_brightness[CHARLIE_LED_COUNT] = {0}; // 0 means off, max is CHARLIE_PWM_STEPS - 1
// Top brightness value accepted by Charlie_SetLED (64 = full on for all 64 PWM steps).

static const uint32_t charlie_pins[CHARLIE_PIN_COUNT] = {
    CHARLIE_X0,
    CHARLIE_X1,
    CHARLIE_X2,
    CHARLIE_X3,
};

static const uint8_t charlie_map[CHARLIE_LED_COUNT][2] = {
    {0, 1},
    {1, 0},
    {1, 2},
    {2, 1},
    {2, 3},
    {3, 2},
    {3, 0},
    {0, 3},
    {0, 2},
    {2, 0},
    {1, 3},
    {3, 1},
};

/* Bitmask of all charlieplex pins, built once in Charlie_Init().
 * Mode and pull never change after init, so the hot path (called every
 * Charlie_Tick(), i.e. every superloop iteration) only needs to touch
 * OTYPER + ODR instead of re-running SetPinMode/SetPinPull on every pin. */
static uint32_t charlie_all_pins_mask = 0;

/* Precomputed register state for each of the 12 LEDs, plus one extra "off"
 * entry (index CHARLIE_LED_COUNT) for full Hi-Z. Built once in Charlie_Init()
 * so Charlie_Tick() can drive the GPIO with two raw register writes instead
 * of four LL_GPIO_* calls plus mask arithmetic every single iteration. */
typedef struct {
    uint32_t otyper_bits; /* bits to OR into OTYPER (open-drain=1) within charlie_all_pins_mask */
    uint32_t odr_bits;    /* bits to OR into ODR (high=1) within charlie_all_pins_mask */
} charlie_state_t;

static charlie_state_t charlie_states[CHARLIE_LED_COUNT + 1];
#define CHARLIE_OFF_STATE CHARLIE_LED_COUNT

/* Last state written to the GPIO. Initialised to an out-of-range value so the
 * first charlie_apply_state() always performs a real write; after that,
 * repeated identical states (very common at low brightness, where most PWM
 * sub-frames are OFF) skip the register writes entirely. */
static uint8_t charlie_last_state = CHARLIE_OFF_STATE + 1;

static void charlie_build_states(void) {
    for (int led = 0; led < CHARLIE_LED_COUNT; led++) {
        uint8_t anode      = charlie_map[led][0];
        uint8_t cathode    = charlie_map[led][1];
        uint32_t active_pins = charlie_pins[anode] | charlie_pins[cathode];

        // Active anode+cathode go push-pull (OTYPER bit 0); everything else stays open-drain (bit 1).
        charlie_states[led].otyper_bits = charlie_all_pins_mask & ~active_pins;
        // Everything high except the cathode, which is driven low.
        charlie_states[led].odr_bits = charlie_all_pins_mask & ~charlie_pins[cathode];
    }
    // Fully Hi-Z: all open-drain, all released high.
    charlie_states[CHARLIE_OFF_STATE].otyper_bits = charlie_all_pins_mask;
    charlie_states[CHARLIE_OFF_STATE].odr_bits    = charlie_all_pins_mask;
}

static inline void charlie_apply_state(uint8_t state_index) {
    if (state_index == charlie_last_state)
        return;
    charlie_last_state = state_index;

    const charlie_state_t *s = &charlie_states[state_index];
    CHARLIE_GPIO->OTYPER = (CHARLIE_GPIO->OTYPER & ~charlie_all_pins_mask) | s->otyper_bits;
    CHARLIE_GPIO->ODR    = (CHARLIE_GPIO->ODR & ~charlie_all_pins_mask) | s->odr_bits;
}

static void charlie_all_hiz(void) {
    charlie_apply_state(CHARLIE_OFF_STATE);
}

void Charlie_Init(void) {
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

    uint32_t pinMask = 0;
    for (int i = 0; i < CHARLIE_PIN_COUNT; i++) {
        LL_GPIO_SetPinMode(CHARLIE_GPIO, charlie_pins[i], LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinPull(CHARLIE_GPIO, charlie_pins[i], LL_GPIO_PULL_NO);
        pinMask |= charlie_pins[i];
    }
    charlie_all_pins_mask = pinMask;

    charlie_build_states();
    charlie_all_hiz();
}

void Charlie_SetLED(uint8_t led_index, uint8_t brightness) {
    if (led_index >= CHARLIE_LED_COUNT || brightness > CHARLIE_PWM_STEPS)
        return;

    charlie_brightness[led_index] = brightness;
}

void Charlie_SetAllLEDs(uint8_t brightness) {
    memset(charlie_brightness, brightness, CHARLIE_LED_COUNT);
}

void Charlie_Off(void) {
    charlie_all_hiz();
}

static uint8_t _pwm_step = 0;
static uint8_t _led_index = 0;

void Charlie_Tick(void) {
    uint8_t state_index = (charlie_brightness[_led_index] > _pwm_step) ? _led_index : CHARLIE_OFF_STATE;
    charlie_apply_state(state_index);

    _led_index++;
    if (_led_index >= CHARLIE_LED_COUNT) {
        _led_index = 0;
        _pwm_step++;
        if (_pwm_step >= CHARLIE_PWM_STEPS)
            _pwm_step = 0;
    }
}
