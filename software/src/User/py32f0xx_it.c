#include "py32f0xx_it.h"
#include "animations.h"
#include "main.h"
#include "py32f0xx_ll_i2c.h"
#include "py32f0xx_ll_tim.h"

/* The encoder lines each have a 100 nF capacitor to ground, so contact bounce
 * is suppressed by the RC filter formed with the internal pull-ups; the guard
 * below is only a glitch filter and must stay well below the shortest edge
 * gap at hand-rotation speed or ticks get dropped. */
#define ENCODER_DEBOUNCE_MS 1
#define BUTTON_DEBOUNCE_MS 50

static volatile uint32_t last_encoder_tick = 0;
static volatile uint32_t last_button_tick  = 0;

extern volatile uint32_t sys_tick_ms;
/* External variables from main.c */
extern volatile uint8_t device_memory[REG_COUNT];
extern volatile uint8_t current_reg_ptr;
extern __IO I2C_Slave_State_t slave_state;

#define I2C_INSTANCE I2C1

/* Writable registers: config, encoder counters, LED brightness and the
 * general-purpose RAM area. Everything else (version, push state, reserved)
 * is read-only and master writes are ignored (pointer still advances).
 * Register 0x00 is not writable in the normal sense: writing a non-zero
 * value to it triggers a soft reset, handled in APP_ResetRequest(). */
static bool reg_is_writable(uint8_t reg) {
    if (reg == REG_CONFIG)
        return true;
    if (reg >= REG_ENC_COUNT_HI && reg <= REG_ENC_PUSH_COUNT) // 0x03-0x05
        return true;
    if (reg == REG_ANIMATION) // 0x0E
        return true;
    if (reg == REG_ANIMATION_SETTINGS) // 0x0F
        return true;
    if (reg >= REG_LED_BASE && reg < REG_LED_BASE + REG_LED_COUNT) // 0x10-0x1B
        return true;
    return reg >= REG_GP_BASE; // 0x20-0xFF
}

/* Send the byte at the current register pointer and advance it, applying
 * per-register read side effects:
 * - push state is sampled live from the pin (with optional inversion)
 * - rotation count clears after its low byte is read, unless disabled
 * - push count clears after it is read, unless disabled */
static void slave_transmit_next(void) {
    uint8_t reg = current_reg_ptr;

    if (reg == REG_ENC_PUSH_STATE) {
        bool pressed                      = !LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_0);
        bool flipped                      = device_memory[REG_CONFIG] & CFG_PUSH_STATE_FLIP;
        device_memory[REG_ENC_PUSH_STATE] = (pressed != flipped) ? 0x01 : 0x00;
    }

    LL_I2C_TransmitData8(I2C_INSTANCE, device_memory[reg]);

    if (reg == REG_ENC_COUNT_LO && (device_memory[REG_CONFIG] & CFG_ROT_RESET_ON_READ)) {
        device_memory[REG_ENC_COUNT_HI] = 0;
        device_memory[REG_ENC_COUNT_LO] = 0;
    }
    if (reg == REG_ENC_PUSH_COUNT && (device_memory[REG_CONFIG] & CFG_PUSH_RESET_ON_READ)) {
        device_memory[REG_ENC_PUSH_COUNT] = 0;
    }

    current_reg_ptr++; // uint8_t: wraps past 0xFF back to 0x00
}

void I2C1_IRQHandler(void) {
    // --- 1. ERROR HANDLING ---
    if (LL_I2C_IsActiveFlag_AF(I2C_INSTANCE)) {
        LL_I2C_ClearFlag_AF(I2C_INSTANCE);
        slave_state = I2C_STATE_IDLE;
        return;
    }

    // --- 2. STOP CONDITION ---
    if (LL_I2C_IsActiveFlag_STOP(I2C_INSTANCE)) {
        LL_I2C_ClearFlag_STOP(I2C_INSTANCE);
        slave_state = I2C_STATE_IDLE;
        return;
    }
    // --- 3. ADDRESS MATCH ---
    if (LL_I2C_IsActiveFlag_ADDR(I2C_INSTANCE)) {
        // IMPORTANT: Clearing ADDR resets the internal state for the NEW transaction
        LL_I2C_ClearFlag_ADDR(I2C_INSTANCE);

        // Detect if this is a fresh start or a repeated start
        // If we were already in a state, the master just sent a new address (Repeated Start)
        if (LL_I2C_GetTransferDirection(I2C_INSTANCE) == 0) {
            // Master is WRITING (Setting the register pointer)
            slave_state = I2C_STATE_ADDR_RECEIVED;
        } else {
            // Master is READING (Requesting data from the pointer we just set)
            slave_state = I2C_STATE_REG_PTR_SET;
            // Transmit the first byte immediately
            slave_transmit_next();
        }
        return; // Exit to allow the next byte to trigger the next interrupt
    }

    // --- 4. DATA PHASE ---
    switch (slave_state) {
    case I2C_STATE_ADDR_RECEIVED:
        if (LL_I2C_IsActiveFlag_RXNE(I2C_INSTANCE)) {
            current_reg_ptr = LL_I2C_ReceiveData8(I2C_INSTANCE);
            slave_state     = I2C_STATE_REG_PTR_SET;
        }
        break;

    case I2C_STATE_REG_PTR_SET:
        if (LL_I2C_GetTransferDirection(I2C_INSTANCE) == 0) { // 0 = master writing
            // MASTER IS WRITING
            if (LL_I2C_IsActiveFlag_RXNE(I2C_INSTANCE)) {
                uint8_t data = LL_I2C_ReceiveData8(I2C_INSTANCE);
                if (current_reg_ptr == REG_SOFT_RESET) {
                    if (data != 0x00)
                        APP_ResetRequest(); // software reset, does not return
                } else if (reg_is_writable(current_reg_ptr)) {
                    if (current_reg_ptr == REG_CONFIG)
                        data &= 0x3F; // bits 6-7 are reserved
                    device_memory[current_reg_ptr] = data;
                    if (current_reg_ptr == REG_CONFIG ||
                        (current_reg_ptr >= REG_LED_BASE &&
                         current_reg_ptr < REG_LED_BASE + REG_LED_COUNT)) {
                        APP_MarkRegsDirty(); // config/LED changes are applied in the main loop
                    }
                    if (current_reg_ptr == REG_ANIMATION)
                        Animations_Set(data); // switch the active animation
                    if (current_reg_ptr == REG_ANIMATION_SETTINGS)
                        Animations_SetSettings(data); // update the animation timing
                }
                current_reg_ptr++; // advance pointer even for ignored writes
            }
        } else {
            // MASTER IS READING
            if (LL_I2C_IsActiveFlag_TXE(I2C_INSTANCE) || LL_I2C_IsActiveFlag_BTF(I2C_INSTANCE)) {
                slave_transmit_next();
            }
        }
        break;

    default:
        break;
    }
}

void EXTI0_1_IRQHandler(void) {
    // --- SW (EncSW, PA0) ---
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0)) {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_0);
        bool button_pressed = !LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_0);
        if (button_pressed && (sys_tick_ms - last_button_tick) >= BUTTON_DEBOUNCE_MS) {
            last_button_tick = sys_tick_ms;
            if (device_memory[REG_CONFIG] & CFG_PUSH_COUNT_DEC) {
                if (device_memory[REG_ENC_PUSH_COUNT] > 0)
                    device_memory[REG_ENC_PUSH_COUNT]--; // saturate at 0
            } else {
                device_memory[REG_ENC_PUSH_COUNT]++; // wraps past 0xFF
            }
        }
    }
}

void EXTI4_15_IRQHandler(void) {
    // --- Encoder rotation (EncA; pin and EXTI line follow ENCODER_AB_SWAP) ---
    if (LL_EXTI_IsActiveFlag(ENC_A_EXTI_LINE)) {
        LL_EXTI_ClearFlag(ENC_A_EXTI_LINE);

        // 1 ms glitch guard only; real debounce is the RC filter (see above).
        if ((sys_tick_ms - last_encoder_tick) >= ENCODER_DEBOUNCE_MS) {
            last_encoder_tick = sys_tick_ms;

            // EncA just transitioned; EncB's level at this instant gives direction.
            bool enc_b = LL_GPIO_IsInputPinSet(GPIOA, ENC_B_PIN);

#ifdef ENCODER_TRIGGER_TOGGLE
            /* On a rising EncA edge EncB's level means the opposite of what it
             * does on a falling edge, so the decode must account for which edge
             * fired; sampling EncA's new level tells us. */
            bool enc_a   = LL_GPIO_IsInputPinSet(GPIOA, ENC_A_PIN);
            int8_t delta = (enc_a == enc_b) ? -1 : 1;
#else
            int8_t delta = enc_b ? 1 : -1;
#endif
#ifdef ENCODER_DIRECTION_FLIP
            /* Compile-time default direction for the encoder model; composes
             * with the runtime CFG_ENC_DIR_FLIP config bit (both set cancel). */
            delta = -delta;
#endif
            if (device_memory[REG_CONFIG] & CFG_ENC_DIR_FLIP)
                delta = -delta;

            int16_t count =
                (int16_t)((device_memory[REG_ENC_COUNT_HI] << 8) | device_memory[REG_ENC_COUNT_LO]);
            count += delta;

            device_memory[REG_ENC_COUNT_HI] = (uint8_t)((count >> 8) & 0xFF);
            device_memory[REG_ENC_COUNT_LO] = (uint8_t)(count & 0xFF);
        }
    }
}

void SysTick_Handler(void) {
    sys_tick_ms++;
}

/* Charlieplex refresh: advance the multiplex/PWM state at a fixed cadence
 * set by APP_Charlie_Timer_Init() (TIM16 update). Runs at a lower priority
 * than I2C/EXTI so host communication and encoder input never get starved. */
void TIM16_IRQHandler(void) {
    if (LL_TIM_IsActiveFlag_UPDATE(TIM16)) {
        LL_TIM_ClearFlag_UPDATE(TIM16);
        Charlie_Tick();
    }
}
