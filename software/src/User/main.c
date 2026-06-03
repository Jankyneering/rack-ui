/***
 * I2C slave – rotary encoder + push button
 * Target: PY32F002Ax5  (TSSOP20, 20 KB flash, 3 KB RAM)
 *
 * Wiring
 * ──────
 *  PA3   SCL  (I2C1, AF12, open-drain, external 4k7 pull-up to 3V3)
 *  PA2   SDA  (I2C1, AF12, open-drain, external 4k7 pull-up to 3V3)
 *
 *  PA0   ENC_A   (EXTI line 0, falling edge, internal pull-up)
 *  PA1   ENC_B   (plain input,  internal pull-up, sampled inside ENC_A ISR)
 *  PA4   ENC_SW  (EXTI line 4, falling edge, internal pull-up, active-low)
 *
 * I2C register map  (slave address 0x36, 7-bit)
 * ─────────────────
 *  0x00  COUNTER_L  low  byte of signed 16-bit encoder count
 *  0x01  COUNTER_H  high byte of signed 16-bit encoder count
 *  0x02  STATUS     bit 0 = button pressed  (latching, cleared on read)
 *
 * Read sequence (master side)
 * ──────────────────────────
 *  START  0x6C(W)  <reg>  STOP          – set register pointer
 *  START  0x6D(R)  <byte> [<byte>] NACK STOP  – read 1 or 2 bytes
 *
 * Makefile settings
 * ─────────────────
 *  MCU_TYPE    = PY32F002Ax5
 *  USE_LL_LIB ?= y
 */
#include "main.h"
#include "py32f0xx_bsp_clock.h"

/* ── Slave address (7-bit) ───────────────────────────────────── */
#define I2C_SLAVE_ADDR   0x36

/* ── Shared state – read by ISRs in py32f0xx_it.c ───────────── */
volatile int16_t enc_count   = 0;
volatile uint8_t btn_latched = 0;

/* ── Local helpers ───────────────────────────────────────────── */
static void APP_GPIO_Config(void);
static void APP_I2C_Config(void);

/* ═══════════════════════════════════════════════════════════════ */
int main(void)
{
  /* 8 MHz HSI (default for PY32F002A – stays within I2C 100 kHz limit) */
  BSP_RCC_HSI_8MConfig();

  APP_GPIO_Config();
  APP_I2C_Config();

  while (1)
  {
    /* All work happens in py32f0xx_it.c ISRs */
    __WFI();
  }
}

/* ── GPIO ────────────────────────────────────────────────────── */
static void APP_GPIO_Config(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  LL_EXTI_InitTypeDef EXTI_InitStruct = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

  /* ── I2C pins: PA2 (SDA) and PA3 (SCL), AF12, open-drain ── */
  GPIO_InitStruct.Pin        = LL_GPIO_PIN_2 | LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode       = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;   /* external pull-ups on bus */
  GPIO_InitStruct.Alternate  = LL_GPIO_AF_12;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ── ENC_A (PA1): EXTI falling edge ── */
  GPIO_InitStruct.Pin        = LL_GPIO_PIN_1;
  GPIO_InitStruct.Mode       = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull       = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate  = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  EXTI_InitStruct.Line        = LL_EXTI_LINE_1;
  EXTI_InitStruct.LineCommand = ENABLE;
  EXTI_InitStruct.Mode        = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger     = LL_EXTI_TRIGGER_FALLING;
  LL_EXTI_Init(&EXTI_InitStruct);

  NVIC_SetPriority(EXTI0_1_IRQn, 1);
  NVIC_EnableIRQ(EXTI0_1_IRQn);

  /* ── ENC_B (PA1): plain input, sampled in ENC_A ISR ── */
  GPIO_InitStruct.Pin  = LL_GPIO_PIN_4;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ── ENC_SW (PA4): EXTI falling edge ── */
  GPIO_InitStruct.Pin  = LL_GPIO_PIN_13;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  EXTI_InitStruct.Line        = LL_EXTI_LINE_13;
  EXTI_InitStruct.LineCommand = ENABLE;
  EXTI_InitStruct.Mode        = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger     = LL_EXTI_TRIGGER_FALLING;
  LL_EXTI_Init(&EXTI_InitStruct);

  NVIC_SetPriority(EXTI4_15_IRQn, 1);
  NVIC_EnableIRQ(EXTI4_15_IRQn);
}

/* ── I2C ─────────────────────────────────────────────────────── */
static void APP_I2C_Config(void)
{
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

  LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
  LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);

  LL_I2C_InitTypeDef I2C_InitStruct;
  I2C_InitStruct.ClockSpeed      = LL_I2C_MAX_SPEED_STANDARD; /* 100 kHz */
  I2C_InitStruct.DutyCycle       = LL_I2C_DUTYCYCLE_2;
  I2C_InitStruct.OwnAddress1     = (I2C_SLAVE_ADDR << 1);
  I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
  LL_I2C_Init(I2C1, &I2C_InitStruct);

  /* Clock stretching on (default) keeps the master waiting while we load data */
  LL_I2C_EnableClockStretching(I2C1);

  /* Enable event, buffer, and error interrupts */
  LL_I2C_EnableIT_EVT(I2C1);
  LL_I2C_EnableIT_BUF(I2C1);
  LL_I2C_EnableIT_ERR(I2C1);

  /* I2C1 IRQ – highest priority so it beats the encoder ISRs */
  NVIC_SetPriority(I2C1_IRQn, 0);
  NVIC_EnableIRQ(I2C1_IRQn);
}

/* ── Error handler ───────────────────────────────────────────── */
void APP_ErrorHandler(void)
{
  while (1);
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  while (1);
}
#endif
