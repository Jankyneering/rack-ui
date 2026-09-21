/**
 ******************************************************************************
 * @file    animations.h
 * @brief   LED animation engine for the Charlieplex LEDs.
 *
 * The active animation is selected through register 0x0F (REG_ANIMATION):
 *   0x00 = IDLE     : custom control over the LEDs via registers 0x10-0x1B
 *   0x01 = LOADING  : rotating loading animation (default)
 *   0x02 = BREATHING: all LEDs smoothly fade in and out
 *   ...add new animations by extending Animation_Id_t and the table in
 *   animations.c.
 ******************************************************************************
 */

#ifndef __ANIMATIONS_H
#define __ANIMATIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Animation IDs, matching the values written to register 0x0F. */
typedef enum {
    ANIMATION_IDLE = 0x00, /* LEDs are driven manually through registers 0x10-0x1B */
    ANIMATION_LOADING = 0x01, /* rotating loading pattern (default) */
    ANIMATION_BREATHING = 0x02, /* all LEDs fade in and out */
    /* Add new animations here, then register them in the animation table in
     * animations.c. */
} Animation_Id_t;

#define ANIMATION_DEFAULT ANIMATION_LOADING

/* Number of LEDs driven by the animations (register 0x10-0x1B). */
#define ANIMATION_LED_COUNT 12

/* Called once at boot to initialize the animation engine. */
void Animations_Init(void);

/* Select the active animation. Unknown values fall back to ANIMATION_IDLE. */
void Animations_Set(uint8_t id);

/* Current animation value, as exposed in register 0x0F. */
uint8_t Animations_Get(void);

/* Advance the active animation. Called from the main loop at a fixed
 * interval; the engine keeps its own tick timing internally. */
void Animations_Tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANIMATIONS_H */
