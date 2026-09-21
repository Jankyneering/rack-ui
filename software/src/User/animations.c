/**
 ******************************************************************************
 * @file    animations.c
 * @brief   LED animation engine for the Charlieplex LEDs.
 *
 * The active animation is selected through register 0x0F (REG_ANIMATION):
 *   0x00 = IDLE     : custom control over the LEDs via registers 0x10-0x1B
 *   0x01 = LOADING  : rotating loading animation (default)
 *   0x02 = BREATHING: all LEDs smoothly fade in and out
 ******************************************************************************
 */

#include "animations.h"
#include "main.h"

/* External variables from main.c */
extern volatile uint32_t sys_tick_ms;
extern volatile uint8_t device_memory[REG_COUNT];

typedef void (*Animation_Start_Fn_t)(void);
typedef void (*Animation_Step_Fn_t)(void);

/* One entry per animation exposed through register 0x0F. To add a new
 * animation: give it an ID in Animation_Id_t, implement its start/step
 * functions and append an entry here. */
typedef struct {
    uint8_t id;
    Animation_Start_Fn_t start;
    Animation_Step_Fn_t step;
} Animation_Entry_t;

static const Animation_Entry_t *Animation_Find(uint8_t id);
static void Animation_Idle_Start(void);
static void Animation_Idle_Step(void);
static void Animation_Loading_Start(void);
static void Animation_Loading_Step(void);
static void Animation_Breathing_Start(void);
static void Animation_Breathing_Step(void);

static const Animation_Entry_t animation_table[] = {
    {ANIMATION_IDLE, Animation_Idle_Start, Animation_Idle_Step},
    {ANIMATION_LOADING, Animation_Loading_Start, Animation_Loading_Step},
    {ANIMATION_BREATHING, Animation_Breathing_Start, Animation_Breathing_Step},
};

static const Animation_Entry_t *active_animation = &animation_table[0];
static uint8_t current_id = ANIMATION_IDLE;

void Animations_Init(void) {
    current_id = ANIMATION_DEFAULT;
    active_animation = Animation_Find(ANIMATION_DEFAULT);
    active_animation->start();
}

void Animations_Set(uint8_t id) {
    const Animation_Entry_t *entry = Animation_Find(id);
    current_id = id;
    if (entry == NULL)
        entry = Animation_Find(ANIMATION_IDLE); // unknown value: fall back to IDLE
    if (entry != active_animation) {
        active_animation = entry;
        entry->start();
    }
}

uint8_t Animations_Get(void) {
    return current_id;
}

void Animations_Tick(void) {
    static uint32_t last_tick = 0;

    if ((sys_tick_ms - last_tick) < ANIMATION_TICK_MS)
        return;
    last_tick = sys_tick_ms;

    active_animation->step();
}

static const Animation_Entry_t *Animation_Find(uint8_t id) {
    for (uint8_t i = 0; i < (sizeof(animation_table) / sizeof(animation_table[0])); i++) {
        if (animation_table[i].id == id)
            return &animation_table[i];
    }
    return NULL;
}

/**
 * @brief IDLE: the LEDs are driven manually through registers 0x10-0x1B.
 */
static void Animation_Idle_Start(void) {
    /* Nothing to reset: the master owns the LED brightness registers. */
}

static void Animation_Idle_Step(void) {
    /* Nothing to do: LED register writes are applied by the main loop. */
}

/**
 * @brief Perform a rotating loading animation on the Charlieplex LEDs.
 * This animation lights up each LED in a circular pattern, with each LED
 * reaching maximum brightness before moving to the next.
 */
static uint8_t loading_brightness_lut[ANIMATION_LED_COUNT];
static uint8_t loading_current_led = 0;
static uint32_t last_loading_tick = 0;

static void Animation_Loading_Start(void) {
    // Build a simple linear LUT for the loading animation brightnesses
    for (uint8_t i = 1; i < ANIMATION_LED_COUNT; i++) {
        // Calculate the brightness for each LED in the loading animation
        loading_brightness_lut[i] =
            (uint8_t)(((uint16_t)(i + 1) * CHARLIE_PWM_STEPS) / ANIMATION_LED_COUNT - 1);
    }
    loading_current_led = 0;
    last_loading_tick = sys_tick_ms;
}

static void Animation_Loading_Step(void) {
    if ((sys_tick_ms - last_loading_tick) >= ANIMATION_LOADING_TICK_MS) {
        last_loading_tick = sys_tick_ms;

        loading_current_led = (loading_current_led + 1) % ANIMATION_LED_COUNT;

        // Apply the calculated brightnesses to all LEDs
        for (uint8_t i = 0; i < ANIMATION_LED_COUNT; i++) {
            uint8_t led_index = (loading_current_led + i) % ANIMATION_LED_COUNT;
            device_memory[REG_LED_BASE + led_index] = loading_brightness_lut[i];
        }

        // Mark the registers as dirty so that the main loop applies the changes
        APP_MarkRegsDirty();
    }
}

/**
 * @brief Perform a breathing animation on the Charlieplex LEDs.
 * This animation smoothly increases and decreases the brightness of all LEDs.
 */
typedef enum {
    ANIMATION_BREATHING_PHASE_IN,
    ANIMATION_BREATHING_PHASE_HOLD,
    ANIMATION_BREATHING_PHASE_OUT,
    ANIMATION_BREATHING_PHASE_PAUSE,
} BreathingPhase_t;

static BreathingPhase_t breathing_phase = ANIMATION_BREATHING_PHASE_IN;
static uint32_t breathing_phase_start_tick = 0;

static void Animation_Breathing_Start(void) {
    breathing_phase = ANIMATION_BREATHING_PHASE_IN;
    breathing_phase_start_tick = sys_tick_ms;
}

static void Animation_Breathing_Step(void) {
    uint32_t elapsed = sys_tick_ms - breathing_phase_start_tick;

    switch (breathing_phase) {
        case ANIMATION_BREATHING_PHASE_IN:
            if (elapsed >= ANIMATION_BREATHING_IN_MS) {
                breathing_phase = ANIMATION_BREATHING_PHASE_HOLD;
                breathing_phase_start_tick = sys_tick_ms;
                elapsed = 0;
            }
            break;
        case ANIMATION_BREATHING_PHASE_HOLD:
            if (elapsed >= ANIMATION_BREATHING_HOLD_MS) {
                breathing_phase = ANIMATION_BREATHING_PHASE_OUT;
                breathing_phase_start_tick = sys_tick_ms;
                elapsed = 0;
            }
            break;
        case ANIMATION_BREATHING_PHASE_OUT:
            if (elapsed >= ANIMATION_BREATHING_OUT_MS) {
                breathing_phase = ANIMATION_BREATHING_PHASE_PAUSE;
                breathing_phase_start_tick = sys_tick_ms;
                elapsed = 0;
            }
            break;
        case ANIMATION_BREATHING_PHASE_PAUSE:
            if (elapsed >= ANIMATION_BREATHING_PAUSE_MS) {
                breathing_phase = ANIMATION_BREATHING_PHASE_IN;
                breathing_phase_start_tick = sys_tick_ms;
                elapsed = 0;
            }
            break;
        default:
            // Unknown phase, reset to IN
            breathing_phase = ANIMATION_BREATHING_PHASE_IN;
            breathing_phase_start_tick = sys_tick_ms;
            elapsed = 0;
            break;
    }

    // Calculate brightness based on the current phase and elapsed time
    uint8_t brightness = 0;

    switch (breathing_phase) {
        case ANIMATION_BREATHING_PHASE_IN:
            brightness = (uint8_t)((elapsed * CHARLIE_PWM_STEPS) / ANIMATION_BREATHING_IN_MS);
            break;
        case ANIMATION_BREATHING_PHASE_HOLD:
            brightness = CHARLIE_PWM_STEPS; // Full brightness
            break;
        case ANIMATION_BREATHING_PHASE_OUT:
            brightness =
                (uint8_t)(CHARLIE_PWM_STEPS -
                          ((elapsed * CHARLIE_PWM_STEPS) / ANIMATION_BREATHING_OUT_MS));
            break;
        case ANIMATION_BREATHING_PHASE_PAUSE:
            brightness = 0; // Off
            break;
        default:
            brightness = 0; // Default to off
            break;
    }

    // Apply the calculated brightness to all LEDs
    for (uint8_t i = 0; i < ANIMATION_LED_COUNT; i++) {
        device_memory[REG_LED_BASE + i] = brightness;
    }

    // Mark the registers as dirty so that the main loop applies the changes
    APP_MarkRegsDirty();
}
