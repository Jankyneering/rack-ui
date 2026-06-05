#include "py32f0xx_it.h"
#include "main.h"
#include "py32f0xx_ll_i2c.h"

/* External variables from main.c */
extern uint8_t device_memory[256];
extern volatile uint8_t current_reg_ptr;
extern __IO I2C_Slave_State_t slave_state;

#define I2C_INSTANCE I2C1
#define REG_RW_LIMIT 4
#define DEFAULT_VAL 0x42

/* Internal state types matching main.c */
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

    // --- 3. ADDRESS MATCH (Crucial for Repeated Start) ---
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
            if (LL_I2C_IsActiveFlag_TXE(I2C_INSTANCE) || LL_I2C_IsActiveFlag_BTF(I2C_INSTANCE)) {
                // if (current_reg_ptr == 0x07 && false) {
                //     device_memory[0x07] = 0x00; // clear after read
                // }
                LL_I2C_TransmitData8(I2C_INSTANCE, device_memory[current_reg_ptr]);
                current_reg_ptr++;
            }
        }
        break;

    default:
        break;
    }
}

void EXTI4_15_IRQHandler(void) {
    // --- CLK (PA4) ---
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_4)) {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_4);
        // Read DT (PA5) to determine direction
        uint16_t counter = (device_memory[0x04] << 8) | device_memory[0x05];
        if (LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_5)) {
            counter++;
        } else {
            counter--;
        }
        device_memory[0x04] = (counter >> 8) & 0xFF;
        device_memory[0x05] = counter & 0xFF;
    }

    // --- SW (PA6) ---
    if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_6)) {
        LL_EXTI_ClearFlag(LL_EXTI_LINE_6);
        if (!LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_6)) {
            // Button pressed (active low)
            device_memory[0x06] |= 0x01;
            device_memory[0x07]++;
        } else {
            // Button released
            device_memory[0x06] &= ~0x01;
        }
    }
}