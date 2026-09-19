/* charlieplex.h */
#pragma once
#include "main.h"
#include <math.h>
#include <stdint.h>

#define CHARLIE_PIN_COUNT 4
#define CHARLIE_LED_COUNT 12
#define CHARLIE_PWM_STEPS 64

#define CHARLIE_GPIO GPIOA
#define CHARLIE_X0 LL_GPIO_PIN_6
#define CHARLIE_X1 LL_GPIO_PIN_7
#define CHARLIE_X2 LL_GPIO_PIN_12
#define CHARLIE_X3 LL_GPIO_PIN_8

/* LUTs */
#define GAMMA_LUT_SIZE (CHARLIE_PWM_STEPS)

/* Gamma exponent for the brightness curve. Change this one value to retune it
 * (2.0 = simple square law, 2.2 ≈ perceptual/sRGB-style curve, etc). */
#define CHARLIE_GAMMA 2.2f

void Charlie_Init(void);
void Charlie_SetLED(uint8_t led_index, uint8_t brightness); /* 0–CHARLIE_PWM_STEPS, 64 = full on */
void Charlie_SetAllLEDs(uint8_t brightness);                /* same value for every LED */
void Charlie_GammaEnable(bool enable);                      /* if false, Charlie_SetLED() uses linear brightness instead of gamma-corrected */
void Charlie_Off(void);
void Charlie_Tick(void); /* to refresh state */
