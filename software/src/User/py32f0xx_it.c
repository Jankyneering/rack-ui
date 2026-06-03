#include "main.h"
#include "py32f0xx_it.h"

/* ── Shared state (defined in main.c) ───────────────────────── */
extern volatile int16_t enc_count;
extern volatile uint8_t btn_latched;

/* ── Cortex-M0+ core handlers ───────────────────────────────── */
void NMI_Handler(void)       {}
void HardFault_Handler(void) { while (1); }
void SVC_Handler(void)       {}
void PendSV_Handler(void)    {}

void SysTick_Handler(void)
{
  /* LL tick used by LL_mDelay() */
  /* Nothing else needed – we don't use HAL */
}

/* ── EXTI0_1: ENC_A falling edge (PA0) ──────────────────────
 *
 * Classic two-wire quadrature decode:
 *   A↓ while B=1  →  clockwise        →  count++
 *   A↓ while B=0  →  counter-clockwise →  count--
 */
void EXTI0_1_IRQHandler(void)
{
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0))
  {
    if (LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_1))
      enc_count++;
    else
      enc_count--;

    LL_EXTI_ClearFlag(LL_EXTI_LINE_0);
  }
}

/* ── EXTI4_15: ENC_SW falling edge (PA4) ────────────────────
 *
 * Crude debounce: spin ~1 ms at 8 MHz then re-read the pin.
 */
void EXTI4_15_IRQHandler(void)
{
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_4))
  {
    for (volatile uint32_t d = 0; d < 8000; d++) __NOP();
    if (!LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_4))
      btn_latched = 1;

    LL_EXTI_ClearFlag(LL_EXTI_LINE_4);
  }
}

/* ── I2C1 combined event + error handler ────────────────────
 *
 * Slave state machine:
 *
 *  1. ADDR flag:  master addressed us.
 *     • Read TDR direction bit to know if master is writing (reg select)
 *       or reading (data fetch).
 *     • Always clear ADDR by reading SR1 then SR2.
 *
 *  2. RXNE (master writing → slave receiving):
 *     • First byte after ADDR is the register pointer.
 *     • Load the answer into the tx shadow so it is ready for the
 *       next read phase.
 *
 *  3. TXE / BTF (master reading → slave transmitting):
 *     • Send bytes from tx_buf, indexed by tx_idx.
 *
 *  4. AF (acknowledge failure = master sent NACK after last read byte):
 *     • Normal end of a read transfer.  Reset state.
 *
 *  5. STOP:
 *     • End of write transfer.  Reset state.
 *
 *  Register map (slave address 0x36):
 *    0x00  COUNTER_L  – low  byte of signed 16-bit encoder count
 *    0x01  COUNTER_H  – high byte of signed 16-bit encoder count
 *    0x02  STATUS     – bit 0 = button pressed (latching, cleared on read)
 */

/* forward declaration */
static void i2c_load_tx(void);

static uint8_t  reg_ptr  = 0;      /* register selected by master write  */
static uint8_t  tx_buf[2];         /* up to 2 bytes to send back         */
static uint8_t  tx_idx   = 0;      /* how many bytes sent so far         */
static uint8_t  rx_done  = 0;      /* 1 after master has written reg ptr */

void I2C1_IRQHandler(void)
{
  uint32_t sr1 = READ_REG(I2C1->SR1);

  /* ── 1. Address matched ─────────────────────────────────── */
  if (sr1 & I2C_SR1_ADDR)
  {
    uint32_t sr2 = READ_REG(I2C1->SR2); /* clears ADDR */
    tx_idx  = 0;
    rx_done = 0;

    if (sr2 & I2C_SR2_TRA)
    {
      /* Master wants to READ from us – preload first byte */
      i2c_load_tx();
      LL_I2C_TransmitData8(I2C1, tx_buf[tx_idx++]);
    }
    /* If master is writing, just wait for RXNE */
    LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_ACK);
    return;
  }

  /* ── 2. Receive data register not empty ────────────────── */
  if (sr1 & I2C_SR1_RXNE)
  {
    reg_ptr = LL_I2C_ReceiveData8(I2C1) & 0x03;
    rx_done = 1;
    i2c_load_tx();   /* pre-fill tx_buf so it's ready for a repeated start */
    return;
  }

  /* ── 3. Transmit data register empty ───────────────────── */
  if ((sr1 & I2C_SR1_TXE) && !(sr1 & I2C_SR1_BTF))
  {
    if (tx_idx < sizeof(tx_buf))
      LL_I2C_TransmitData8(I2C1, tx_buf[tx_idx++]);
    else
      LL_I2C_TransmitData8(I2C1, 0xFF);
    return;
  }

  /* ── 4. Acknowledge failure (master NACK'd – end of read) ─ */
  if (sr1 & I2C_SR1_AF)
  {
    LL_I2C_ClearFlag_AF(I2C1);
    tx_idx = 0;
    return;
  }

  /* ── 5. Stop condition received ────────────────────────── */
  if (sr1 & I2C_SR1_STOPF)
  {
    /* Clear STOPF: read SR1 (already done) then write CR1 */
    SET_BIT(I2C1->CR1, I2C_CR1_PE);
    tx_idx = 0;
    return;
  }

  /* ── 6. Error flags – reset PE to recover ───────────────── */
  if (sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR))
  {
    LL_I2C_ClearFlag_BERR(I2C1);
    LL_I2C_ClearFlag_ARLO(I2C1);
    LL_I2C_ClearFlag_OVR(I2C1);
    CLEAR_BIT(I2C1->CR1, I2C_CR1_PE);
    SET_BIT(I2C1->CR1, I2C_CR1_PE);
  }
}

/* Fill tx_buf from current encoder state for the requested register. */
static void i2c_load_tx(void)
{
  int16_t  count  = enc_count;   /* snapshot volatile */
  uint8_t  status = btn_latched;

  switch (reg_ptr)
  {
    case 0x00:  /* COUNTER_L */
      tx_buf[0] = (uint8_t)(count & 0xFF);
      tx_buf[1] = (uint8_t)((count >> 8) & 0xFF);
      break;
    case 0x01:  /* COUNTER_H */
      tx_buf[0] = (uint8_t)((count >> 8) & 0xFF);
      tx_buf[1] = 0x00;
      break;
    case 0x02:  /* STATUS – auto-clear on read */
      tx_buf[0] = status & 0x01;
      tx_buf[1] = 0x00;
      btn_latched = 0;
      break;
    default:
      tx_buf[0] = 0xFF;
      tx_buf[1] = 0xFF;
      break;
  }
}
