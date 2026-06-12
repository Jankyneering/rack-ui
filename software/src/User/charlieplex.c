/* charlieplex.c */
#include "charlieplex.h"

static uint8_t charlie_brightness[CHARLIE_LED_COUNT] = {0}; // 0 means off, max is CHARLIE_PWM_STEPS

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

static void charlie_all_hiz(void) {
    uint32_t pinMask = 0;

    for (int i = 0; i < CHARLIE_PIN_COUNT; i++) {
        LL_GPIO_SetPinMode(CHARLIE_GPIO, charlie_pins[i], LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinPull(CHARLIE_GPIO, charlie_pins[i], LL_GPIO_PULL_NO);
        pinMask |= charlie_pins[i];
    }

    LL_GPIO_SetPinOutputType(CHARLIE_GPIO, pinMask, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetOutputPin(CHARLIE_GPIO, pinMask); // Set all high (Hi-Z due to open-drain)
}

void Charlie_Init(void) {
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    charlie_all_hiz();
}

static void Charlie_ScanLED(uint8_t led_index, uint8_t state) {
    charlie_all_hiz();

    if (!state || led_index >= CHARLIE_LED_COUNT)
        return;

    uint8_t anode   = charlie_map[led_index][0];
    uint8_t cathode = charlie_map[led_index][1];

    LL_GPIO_SetPinOutputType(CHARLIE_GPIO, charlie_pins[cathode] | charlie_pins[anode], LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_ResetOutputPin(CHARLIE_GPIO, charlie_pins[cathode]);
}

void Charlie_SetLED(uint8_t led_index, uint8_t brightness) {
    if (led_index >= CHARLIE_LED_COUNT || brightness >= CHARLIE_PWM_STEPS)
        return;

    charlie_brightness[led_index] = brightness;
}

void Charlie_Off(void) {
    charlie_all_hiz();
}


static uint8_t _pwm_step = 0;
static uint8_t _led_index = 0;

void Charlie_Tick(void) {

    if (charlie_brightness[_led_index] > _pwm_step)
        Charlie_ScanLED(_led_index, 1);
    else
        Charlie_ScanLED(_led_index, 0);

    _led_index++;
    if (_led_index >= CHARLIE_LED_COUNT) {
        _led_index = 0;
        _pwm_step++;
        if (_pwm_step >= CHARLIE_PWM_STEPS)
            _pwm_step = 0;
    }
}
