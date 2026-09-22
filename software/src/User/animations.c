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
static void Animation_Following_Start(void);
static void Animation_Following_Step(void);
static void Animation_Point_Start(void);
static void Animation_Point_Step(void);
static void Animation_Gauge_Start(void);
static void Animation_Gauge_Step(void);

static const Animation_Entry_t animation_table[] = {
    {ANIMATION_IDLE, Animation_Idle_Start, Animation_Idle_Step},
    {ANIMATION_LOADING, Animation_Loading_Start, Animation_Loading_Step},
    {ANIMATION_BREATHING, Animation_Breathing_Start, Animation_Breathing_Step},
    {ANIMATION_FOLLOWING, Animation_Following_Start, Animation_Following_Step},
    {ANIMATION_POINT, Animation_Point_Start, Animation_Point_Step},
    {ANIMATION_GAUGE, Animation_Gauge_Start, Animation_Gauge_Step},
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


/**
 * @brief Perform a following animation on the Charlieplex LEDs.
 * This animation lights up one in three LEDs based on the encoder rotation, creating a "following" effect.
 */
static void Animation_Following_Start(void) {
    // Nothing to reset: the master owns the LED brightness registers.
}

static void Animation_Following_Step(void) {
    // Get the current encoder rotation count
    int16_t rotation_count = (device_memory[REG_ENC_COUNT_LO] | (device_memory[REG_ENC_COUNT_HI] << 8));
    // Set LED states
    for (uint8_t i = 0; i < ANIMATION_LED_COUNT; i++) {
        device_memory[REG_LED_BASE + i] = (i-rotation_count)%ANIMATION_FOLLOWING_LED_STEPS == 0 ? CHARLIE_PWM_STEPS-1 : 0; // Light up every third LED
    }
    // Mark the registers as dirty so that the main loop applies the changes
    APP_MarkRegsDirty();
}

/**
 * @brief Perform a point animation on the Charlieplex LEDs.
 * This animation lights up one LED at a time based on the encoder rotation, creating a "pointing" effect.
 */
static void Animation_Point_Start(void) {
    // Nothing to reset: the master owns the LED brightness registers.
}

static void Animation_Point_Step(void) {
    // Get the current encoder rotation count
    int16_t rotation_count = (device_memory[REG_ENC_COUNT_LO] | (device_memory[REG_ENC_COUNT_HI] << 8));
    // Set LED states
    for (uint8_t i = 0; i < ANIMATION_LED_COUNT; i++) {
        device_memory[REG_LED_BASE + i] = (i-rotation_count)%CHARLIE_LED_COUNT == 0 ? CHARLIE_PWM_STEPS-1 : 0;
    }
    // Mark the registers as dirty so that the main loop applies the changes
    APP_MarkRegsDirty();
}

/**
 * @brief Perform a gauge animation on the Charlieplex LEDs.
 * This animation lights up LEDs in a gauge pattern based on the encoder rotation.
 */
static uint32_t last_gauge_tick = 0;
static void Animation_Gauge_Start(void) {
    // Reset encoder count to zero for a fresh start (signed 16-bit value)
    device_memory[REG_ENC_COUNT_HI] = 0x00;
    device_memory[REG_ENC_COUNT_LO] = 0x00;

    last_gauge_tick = sys_tick_ms;
}

static void Animation_Gauge_Step(void) {
    if ((sys_tick_ms - last_gauge_tick) < 50) // Update every 50ms
        return;
    last_gauge_tick = sys_tick_ms;
    
    // Get the current encoder rotation count
    int16_t rotation_count = (device_memory[REG_ENC_COUNT_LO] | (device_memory[REG_ENC_COUNT_HI] << 8));

    // Clamp percentage to 0-100 range
    if (rotation_count < 0) {
        rotation_count = 0;
        // clamp device_memory[REG_ENC_COUNT_HI] and device_memory[REG_ENC_COUNT_LO] to 0
        device_memory[REG_ENC_COUNT_HI] = (uint8_t)((rotation_count >> 8) & 0xFF);
        device_memory[REG_ENC_COUNT_LO] = (uint8_t)(rotation_count & 0xFF);
    } else if (rotation_count > 100) {
        rotation_count = 100;
        // clamp device_memory[REG_ENC_COUNT_HI] and device_memory[REG_ENC_COUNT_LO] to 100
        device_memory[REG_ENC_COUNT_HI] = (uint8_t)((rotation_count >> 8) & 0xFF);
        device_memory[REG_ENC_COUNT_LO] = (uint8_t)(rotation_count & 0xFF);
    } 

    // calculate the brightness for each LED based on the percentage
    // each LED represents ~8.33% of the gauge, each LED maps to a range of 0-8.33% of the total percentage
    for (uint8_t i = 0; i < ANIMATION_GAUGE_LED_COUNT; i++) {
        // calculate the percentage range for this LED
        float led_percentage_start = (i * 100.0f) / ANIMATION_GAUGE_LED_COUNT;
        float led_percentage_end = ((i + 1) * 100.0f) / ANIMATION_GAUGE_LED_COUNT;

        if (rotation_count >= led_percentage_end) {
            // LED is fully lit
            device_memory[REG_LED_BASE + (i + ANIMATION_GAUGE_START_LED)%CHARLIE_LED_COUNT] = CHARLIE_PWM_STEPS - 1;
        } else if (rotation_count <= led_percentage_start) {
            // LED is off
            device_memory[REG_LED_BASE + (i + ANIMATION_GAUGE_START_LED)%CHARLIE_LED_COUNT] = 0;
        } else {
            // LED is partially lit, calculate brightness based on the percentage
            float led_range = led_percentage_end - led_percentage_start;
            float led_brightness_percentage = (rotation_count - led_percentage_start) / led_range;
            device_memory[REG_LED_BASE + (i + ANIMATION_GAUGE_START_LED)%CHARLIE_LED_COUNT] = (uint8_t)(led_brightness_percentage * (CHARLIE_PWM_STEPS - 1));
        }
    }

    // Mark the registers as dirty so that the main loop applies the changes
    APP_MarkRegsDirty();
}