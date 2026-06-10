/* charlieplex.c */
#include "charlieplex.h"

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

void Charlie_SetLED(uint8_t led_index, uint8_t state) {
    charlie_all_hiz();

    if (!state || led_index >= CHARLIE_LED_COUNT)
        return;

    uint8_t anode   = charlie_map[led_index][0];
    uint8_t cathode = charlie_map[led_index][1];

    LL_GPIO_SetPinOutputType(CHARLIE_GPIO, charlie_pins[cathode] | charlie_pins[anode], LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_ResetOutputPin(CHARLIE_GPIO, charlie_pins[cathode]);
}

void Charlie_Off(void) {
    charlie_all_hiz();
}