#include "main.h"
#include <stddef.h>

/* I2C Configuration */
#define SLAVE_ADDRESS (I2C_SLAVE_ADDR << 1)
#define I2C_SPEEDCLOCK 100000

/* Private variables */
volatile uint8_t device_memory[REG_COUNT];
volatile uint8_t current_reg_ptr   = 0;
__IO I2C_Slave_State_t slave_state  = I2C_STATE_IDLE;
volatile uint32_t sys_tick_ms       = 0;

/* Set when the master writes to a register that requires the main loop to act
 * (config or LED brightness). The main loop clears it after applying, so I2C
 * writes keep latency bounded and ISRs never call Charlie_* functions. */
static volatile bool regs_dirty = false;

/* Set by the I2C ISR when the master writes a non-zero value to the command
 * register (0x00). The reset itself runs in the main loop so the current I2C
 * transaction finishes (STOP seen by the master) before the MCU resets. */
static volatile bool reset_pending = false;

/* Prototypes */
static void APP_SystemClockConfig(void);
static void APP_GPIOConfig(void);
static void APP_I2C_Slave_Init(void);
static void APP_Encoder_Init(void);
static void APP_Charlie_Timer_Init(void);
static void APP_ApplyConfig(void);
static void APP_ApplyLedRegisters(void);

int main(void) {
    APP_SystemClockConfig();
    APP_GPIOConfig();
    APP_I2C_Slave_Init();
    APP_Encoder_Init();

    Charlie_Init();
    APP_Charlie_Timer_Init();

    // Initialize memory
    for (int i = 0; i < REG_COUNT; i++) {
        if (i >= REG_GP_BASE)
            device_memory[i] = REG_GP_DEFAULT;
        else
            device_memory[i] = 0x00;
    }

    /* Firmware version, big-endian */
    device_memory[REG_FW_VERSION_HI] = (uint8_t)(FW_VERSION >> 8);
    device_memory[REG_FW_VERSION_LO] = (uint8_t)(FW_VERSION & 0xFF);

    /* Push state default: not pushed (bit 0 clear) */
    device_memory[REG_ENC_PUSH_STATE] = 0x00;

    /* Apply config-dependent display settings once at boot */
    APP_ApplyConfig();
    APP_ApplyLedRegisters();

    while (1) {
        if (reset_pending) {
            NVIC_SystemReset(); // does not return
        }
        if (regs_dirty) {
            regs_dirty = false;
            APP_ApplyConfig();
            APP_ApplyLedRegisters();
        }
        // Charlie_Tick() now runs from the TIM16 update ISR at a fixed cadence;
        // sleep until any interrupt wakes us instead of spinning on the loop.
        __WFI();
    }
}

void APP_MarkRegsDirty(void) {
    regs_dirty = true;
}

void APP_ResetRequest(void) {
    reset_pending = true;
}

static void APP_ApplyConfig(void) {
    Charlie_GammaEnable(!(device_memory[REG_CONFIG] & CFG_LED_LINEAR));
}

/* Push the LED brightness registers (0x10-0x1B) into the charlieplex driver.
 * Charlie_SetLED() clamps out-of-range values to full on. */
static void APP_ApplyLedRegisters(void) {
    for (uint8_t i = 0; i < REG_LED_COUNT; i++) {
        Charlie_SetLED(i, device_memory[REG_LED_BASE + i]);
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
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    LL_SetSystemCoreClock(8000000);
}

static void APP_GPIOConfig(void) {
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    // PB5 active low LED
    GPIO_InitStruct.Pin        = LL_GPIO_PIN_5;
    GPIO_InitStruct.Mode      = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed     = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull      = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB5 is active low: drive it high to keep the onboard LED off by default
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_5);
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

    // EncA (PA5), EncB (PA4), EncSW (PA0) — input with pull-up
    GPIO_InitStruct.Pin  = LL_GPIO_PIN_5 | LL_GPIO_PIN_4 | LL_GPIO_PIN_0;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Configure EXTI for PA5 (EncA) — trigger on falling edge to detect rotation;
    // direction is read from EncB's level at the moment EncA transitions.
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line                = LL_EXTI_LINE_5;
    EXTI_InitStruct.LineCommand         = ENABLE;
    EXTI_InitStruct.Mode                = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger             = LL_EXTI_TRIGGER_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);

    // Configure EXTI for PA0 (EncSW) — trigger on both edges to detect press/release
    EXTI_InitStruct.Line    = LL_EXTI_LINE_0;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);

    // Connect EXTI lines to GPIOA
    LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE5);
    LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE0);

    // Line 0 is serviced by EXTI0_1_IRQHandler, line 5 by EXTI4_15_IRQHandler —
    // both vectors need enabling, not just one, since EncSW and EncA now sit
    // on different NVIC lines.
    NVIC_SetPriority(EXTI0_1_IRQn, 1); // lower priority than I2C
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_SetPriority(EXTI4_15_IRQn, 1); // lower priority than I2C
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

/* Charlieplex refresh timer. The main loop used to spin-call Charlie_Tick()
 * unconditionally, which pinned the CPU at 100% and tied PWM timing to loop
 * iteration count. TIM16 now fires a periodic update interrupt that advances
 * Charlie_Tick() at a fixed cadence; the main loop sleeps with __WFI between
 * ticks. With the 8 MHz HSI prescaled to a 1 MHz counting clock and a 24 us
 * period, one full PWM cycle (12 LEDs * 64 steps = 768 sub-frames) takes
 * ~18.4 ms ~ 54 Hz, flicker-free. */
#define CHARLIE_TICK_US 24

static void APP_Charlie_Timer_Init(void) {
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_TIM16);

    LL_TIM_SetPrescaler(TIM16, __LL_TIM_CALC_PSC(SystemCoreClock, 1000000));
    LL_TIM_SetAutoReload(TIM16, CHARLIE_TICK_US - 1);
    LL_TIM_SetCounterMode(TIM16, LL_TIM_COUNTERMODE_UP);
    LL_TIM_EnableARRPreload(TIM16);
    LL_TIM_EnableUpdateEvent(TIM16);
    LL_TIM_ClearFlag_UPDATE(TIM16);
    LL_TIM_EnableIT_UPDATE(TIM16);

    NVIC_SetPriority(TIM16_IRQn, 2); // lowest priority: I2C > EXTI > refresh
    NVIC_EnableIRQ(TIM16_IRQn);

    LL_TIM_EnableCounter(TIM16);
}

void APP_ErrorHandler(void) {
    while (1)
        ;
}
