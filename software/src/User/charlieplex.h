/* charlieplex.h */
#pragma once
#include "main.h"
#include <stdint.h>

#define CHARLIE_PIN_COUNT  4
#define CHARLIE_LED_COUNT  12
#define CHARLIE_PWM_STEPS 64

#define CHARLIE_GPIO  GPIOA
/* Prototype pins */
// #define CHARLIE_X0    LL_GPIO_PIN_5
// #define CHARLIE_X1    LL_GPIO_PIN_6
// #define CHARLIE_X2    LL_GPIO_PIN_7
// #define CHARLIE_X3    LL_GPIO_PIN_4

/* PCB */
#define CHARLIE_X0    LL_GPIO_PIN_6
#define CHARLIE_X1    LL_GPIO_PIN_7
#define CHARLIE_X2    LL_GPIO_PIN_12
#define CHARLIE_X3    LL_GPIO_PIN_8


void Charlie_Init(void);
void Charlie_SetLED(uint8_t led_index, uint8_t brightness); /* 0–CHARLIE_PWM_STEPS */
void Charlie_Off(void);
void Charlie_Tick(void);  /* to refresh state */
// void charlie_all_hiz(void);
// void charlie_drive(uint8_t led_index, uint8_t state);
