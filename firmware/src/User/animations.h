/**
 ******************************************************************************
 * @file    animations.h
 * @brief   LED animation engine for the Charlieplex LEDs.
 *
 * The active animation is selected through register 0x0E (REG_ANIMATION):
 *   0x00 = IDLE     : custom control over the LEDs via registers 0x10-0x1B
 *   0x01 = LOADING  : rotating loading animation
 *   0x02 = FLASHING : all LEDs flash on and off
 *   0x03 = PULSING  : all LEDs pulse in brightness
 *   0x04 = BREATHING: all LEDs smoothly fade in and out
 * Register 0x0F (REG_ANIMATION_SETTINGS) holds the setting of the
 *   selected animation (timing, brightness or gauge maximum, depending on
 *   the animation); writing REG_ANIMATION loads that animation's default.
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

/* Animation IDs, matching the values written to register 0x0E. */
typedef enum {
    ANIMATION_IDLE      = 0x00, /* LEDs are driven manually through registers 0x10-0x1B */
    ANIMATION_LOADING   = 0x01, /* rotating loading pattern */
    ANIMATION_FLASHING  = 0x02, /* all LEDs flash on and off */
    ANIMATION_PULSING   = 0x03, /* all LEDs pulse in brightness */
    ANIMATION_BREATHING = 0x04, /* all LEDs fade in and out */

    ANIMATION_FOLLOWING = 0x80, /* LEDs follow the encoder rotation (one LED per detent) */
    ANIMATION_POINT     = 0x81, /* one LED lights up at a time, moving with the encoder rotation */
    ANIMATION_GAUGE     = 0x82, /* LEDs light up in a gauge pattern based on the encoder rotation */

    ANIMATION_ALL_ON    = 0xFE, /* all LEDs on at the brightness set through 0x0F (power-on default) */
    ANIMATION_ALL_OFF   = 0xFF, /* all LEDs off */
    /* Add new animations here, then register them in the animation table in
     * animations.c. */
} Animation_Id_t;

#define ANIMATION_DEFAULT ANIMATION_FOLLOWING /* Default animation at power-on. */

/* Animation settings. */
#define ANIMATION_TICK_MS 11 // Animation engine tick, driven from the main loop.

/* Defaults loaded into register 0x0F (REG_ANIMATION_SETTINGS) when the
 * matching animation is selected through register 0x0E. The register value
 * is the animation's setting: 0-255, the timing in tens of milliseconds for
 * LOADING and FLASHING, in milliseconds for PULSING, the brightness for
 * ALL_ON, the off-state brightness of the non-lit LEDs for FOLLOWING and
 * POINT, and the maximum gauge value for GAUGE. A value of 0 keeps the
 * previous setting. */
#define ANIMATION_LOADING_SETTINGS_DEFAULT 10                   // 10 * 10 ms = 100 ms between LED steps.
#define ANIMATION_FLASHING_SETTINGS_DEFAULT 25                  // 25 * 10 ms = 250 ms between LED state toggles.
#define ANIMATION_PULSING_SETTINGS_DEFAULT 10                   // 10 ms between LED brightness changes.
#define ANIMATION_ALL_ON_SETTINGS_DEFAULT CHARLIE_PWM_STEPS - 1 // Brightness of all LEDs (values above full-on clamp).
#define ANIMATION_FOLLOWING_SETTINGS_DEFAULT 0                  // Off-state brightness of the non-lit LEDs.
#define ANIMATION_POINT_SETTINGS_DEFAULT 0                      // Off-state brightness of the non-lit LEDs.
#define ANIMATION_GAUGE_SETTINGS_DEFAULT 100                    // Maximum gauge value (encoder count upper limit).

/* Animation settings register scaling, in ms per register step. */
#define ANIMATION_LOADING_SETTINGS_MS 10
#define ANIMATION_FLASHING_SETTINGS_MS 10
#define ANIMATION_PULSING_SETTINGS_MS 1

#define ANIMATION_BREATHING_IN_MS 1500    // Time for LEDs to fade in during the breathing animation.
#define ANIMATION_BREATHING_HOLD_MS 750   // Time to hold full brightness during the breathing animation.
#define ANIMATION_BREATHING_OUT_MS 2500   // Time for LEDs to fade out during the breathing animation.
#define ANIMATION_BREATHING_PAUSE_MS 2000 // Time to pause between breathing cycles.
#define ANIMATION_FOLLOWING_LED_STEPS 4   // Number of LEDs to light up in the following animation (one per detent).
#define ANIMATION_GAUGE_START_LED 8       // Starting LED index for the gauge animation (0-CHARLIE_LED_COUNT-1).
#define ANIMATION_GAUGE_LED_COUNT 9       // Number of LEDs to light up in the gauge animation (1-CHARLIE_LED_COUNT).

/* Define to hold the LEDs that are not part of the gauge arc at a low
 * brightness while the gauge animation is active, instead of leaving them at
 * whatever state the previous animation or host writes left them in. Comment
 * out to leave the unused LEDs untouched. */
#define ANIMATION_GAUGE_DIM_UNUSED_LEDS
#define ANIMATION_GAUGE_UNUSED_BRIGHTNESS 8 // Brightness of the non-gauge LEDs while GAUGE is active (0-CHARLIE_PWM_STEPS-1).

/* Number of LEDs driven by the animations (register 0x10-0x1B). */
#define ANIMATION_LED_COUNT 12

/* Called once at boot to initialize the animation engine. */
void Animations_Init(void);

/* Select the active animation. Unknown values fall back to ANIMATION_IDLE.
 * Loading a new animation also loads its default timing setting. */
void Animations_Set(uint8_t id);

/* Update the setting of the current animation (timing, brightness or gauge
 * maximum). A value of 0 keeps the current setting; ignored for animations
 * without a setting. */
void Animations_SetSettings(uint8_t settings);

/* Current animation value, as exposed in register 0x0E. */
uint8_t Animations_Get(void);

/* Timing setting of the current animation, as exposed in register 0x0F. */
uint8_t Animations_GetSettings(void);

/* Advance the active animation. Called from the main loop at a fixed
 * interval; the engine keeps its own tick timing internally. */
void Animations_Tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANIMATIONS_H */
