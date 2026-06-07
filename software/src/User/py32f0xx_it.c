#include "py32f0xx_it.h"
#include "main.h"
#include "py32f0xx_ll_i2c.h"

#define ENCODER_DEBOUNCE_MS 50
#define BUTTON_DEBOUNCE_MS 50

static volatile uint32_t last_encoder_tick = 0;
static volatile uint32_t last_button_tick  = 0;

extern volatile uint32_t sys_tick_ms;
/* External variables from main.c */
extern uint8_t device_memory[256];
extern volatile uint8_t current_reg_ptr;
extern __IO I2C_Slave_State_t slave_state;

#define I2C_INSTANCE I2C1
#define REG_RW_LIMIT 4
#define DEFAULT_VAL 0x42

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
            LL_I2C_TransmitData8(I2C_INSTANCE, device_memory[current_reg_ptr]);
            current_reg_ptr++;
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
        if (LL_I2C_GetTransferDirection(I2C_INSTANCE) == 0) { // fixed: 0 = master writing
            // MASTER IS WRITING
            if (LL_I2C_IsActiveFlag_RXNE(I2C_INSTANCE)) {
                uint8_t data = LL_I2C_ReceiveData8(I2C_INSTANCE);
                if (current_reg_ptr < REG_RW_LIMIT) {
                    device_memory[current_reg_ptr] = data;
                    current_reg_ptr++; // advance pointer for multi-byte writes
                }
            }
        } else {
            // MASTER IS READING
            if (!LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_6)) {
                device_memory[0x06] |= 0x01;
            } else {
                device_memory[0x06] &= ~0x01;
            }
            if (LL_I2C_IsActiveFlag_TXE(I2C_INSTANCE) || LL_I2C_IsActiveFlag_BTF(I2C_INSTANCE)) {
                LL_I2C_TransmitData8(I2C_INSTANCE, device_memory[current_reg_ptr]);
                if (current_reg_ptr == 0x07 && true) {
                    device_memory[0x07] = 0x00; // clear after read
                }
                current_reg_ptr++;
            }
        }
        break;

    default:
        break;
    }
}

void EXTI4_15_IRQHandler(void) {
    // --- ENCODER (PA4) ---
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_4)) {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_4);

        if ((sys_tick_ms - last_encoder_tick) >= ENCODER_DEBOUNCE_MS) {
            last_encoder_tick         = sys_tick_ms;

            bool dt                = LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_5) ? 1 : 0;

            uint16_t counter          = (uint16_t)((device_memory[0x04] << 8) | device_memory[0x05]);

            dt ? counter++ : counter--;

            device_memory[0x04] = (uint8_t)((counter >> 8) & 0xFF);
            device_memory[0x05] = (uint8_t)(counter & 0xFF);
        }
    }

    // --- SW (PA6) ---
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_6)) {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_6);
        bool button_pressed = !LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_6);
        if ((sys_tick_ms - last_button_tick) >= BUTTON_DEBOUNCE_MS) {
            last_button_tick = sys_tick_ms;
            button_pressed ? device_memory[0x07]++ : (void)0; // Increment on press, do nothing on release
        }
    }
}

void SysTick_Handler(void) {
    sys_tick_ms++;
}