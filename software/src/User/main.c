#include "main.h"
#include <stddef.h>

/* I2C Configuration */
#define SLAVE_ADDRESS (0x36 << 1)
#define I2C_SPEEDCLOCK 100000
#define I2C_STATE_READY 0
#define I2C_STATE_BUSY_TX 1
#define I2C_STATE_BUSY_RX 2

/* Register Map Configuration */
#define REG_COUNT 256
#define REG_RW_LIMIT 4 // 0x00 to 0x03
#define REG_RO_LIMIT 8 // 0x04 to 0x07
#define DEFAULT_VAL 0x42

/* Private variables */
volatile uint8_t device_memory[REG_COUNT];
uint8_t current_reg_ptr            = 0;
__IO I2C_Slave_State_t slave_state = I2C_STATE_IDLE;

/* Prototypes */
static void APP_SystemClockConfig(void);
static void APP_GPIOConfig(void);
static void APP_I2C_Slave_Init(void);
static void APP_Encoder_Init(void);

int main(void) {
    APP_SystemClockConfig();
    APP_GPIOConfig();
    APP_I2C_Slave_Init();
    APP_Encoder_Init();

    // Initialize memory
    for (int i = 0; i < REG_COUNT; i++) {
        if (i < REG_RW_LIMIT)
            device_memory[i] = i + 12; // Start RW regs at 0
        else if (i < REG_RO_LIMIT)
            device_memory[i] = 0xAA; // Start RO regs at 0xAA
        else
            device_memory[i] = DEFAULT_VAL; // Everything else is 0x42
    }

    while (1) {
        if (device_memory[1] == 0xFF) {
            LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_5); // ON
            device_memory[4] = 0x55; // Update a RO register to show we can write to it internally
        } else {
            LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_5); // OFF
            device_memory[4] = 0xAA; // Update the RO register back to its default
        }
    }
}

static void APP_SystemClockConfig(void) {
    LL_RCC_HSI_Enable();
    while (LL_RCC_HSI_IsReady() != 1)
        ;
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS)
        ;
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_Init1msTick(8000000);
    LL_SetSystemCoreClock(8000000);
}

static void APP_GPIOConfig(void) {
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    // PB5 active low LED
    GPIO_InitStruct.Pin        = LL_GPIO_PIN_5;
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void APP_I2C_Slave_Init(void) {
    /* Enable GPIOA peripheral clock */
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

    /* Enable I2C1 peripheral clock */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

    /* Configure SCL pin: Alternative function, High speed, Open-drain, Pull-up */
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin                 = LL_GPIO_PIN_3;
    GPIO_InitStruct.Mode                = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed               = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.OutputType          = LL_GPIO_OUTPUT_OPENDRAIN;
    GPIO_InitStruct.Pull                = LL_GPIO_PULL_UP;
    GPIO_InitStruct.Alternate           = LL_GPIO_AF_12;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Configure SDA pin: Alternative function, High speed, Open-drain, Pull-up */
    GPIO_InitStruct.Pin        = LL_GPIO_PIN_2;
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_UP;
    GPIO_InitStruct.Alternate  = LL_GPIO_AF_12;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Reset I2C */
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);

    /* Enable NVIC interrupt */
    NVIC_SetPriority(I2C1_IRQn, 0);
    NVIC_EnableIRQ(I2C1_IRQn);

    /* I2C Initialisation */
    LL_I2C_InitTypeDef I2C_InitStruct = {0};
    I2C_InitStruct.ClockSpeed         = I2C_SPEEDCLOCK;
    I2C_InitStruct.DutyCycle          = LL_I2C_DUTYCYCLE_16_9;
    I2C_InitStruct.OwnAddress1        = SLAVE_ADDRESS;
    I2C_InitStruct.TypeAcknowledge    = LL_I2C_ACK; // Always ACK
    LL_I2C_Init(I2C1, &I2C_InitStruct);

    // Enable all necessary interrupts for a Register-based Slave
    LL_I2C_EnableIT_EVT(I2C1);
    LL_I2C_EnableIT_BUF(I2C1);
    LL_I2C_EnableIT_ERR(I2C1);

    LL_I2C_Enable(I2C1);
}

static void APP_Encoder_Init(void) {
    // GPIOA clock already enabled by I2C init
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    // CLK (PA4), DT (PA5), SW (PA6) — input with pull-up
    GPIO_InitStruct.Pin       = LL_GPIO_PIN_4 | LL_GPIO_PIN_5 | LL_GPIO_PIN_6;
    GPIO_InitStruct.Mode      = LL_GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull      = LL_GPIO_PULL_UP;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Configure EXTI for PA4 (CLK) — trigger on falling edge
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line   = LL_EXTI_LINE_4;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode        = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger     = LL_EXTI_TRIGGER_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);

    // Configure EXTI for PA6 (SW) — trigger on both edges to detect press and release
    EXTI_InitStruct.Line   = LL_EXTI_LINE_6;
    EXTI_InitStruct.Trigger     = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);

    // Connect EXTI lines to GPIOA
    LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE4);
    LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE6);

    // Enable NVIC
    NVIC_SetPriority(EXTI4_15_IRQn, 1); // lower priority than I2C
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void APP_ErrorHandler(void) {
    while (1)
        ;
}
