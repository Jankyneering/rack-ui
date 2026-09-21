/**
 ******************************************************************************
 * @file    main.h
 * @author  MCU Application Team
 * @brief   Header for main.c file.
 *          This file contains the common defines of the application.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by Puya under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "charlieplex.h"
#include "py32f0xx_it.h"
#include "py32f0xx_ll_bus.h"
#include "py32f0xx_ll_cortex.h"
#include "py32f0xx_ll_exti.h"
#include "py32f0xx_ll_gpio.h"
#include "py32f0xx_ll_i2c.h"
#include "py32f0xx_ll_pwr.h"
#include "py32f0xx_ll_rcc.h"
#include "py32f0xx_ll_system.h"
#include "py32f0xx_ll_tim.h"
#include "py32f0xx_ll_utils.h"
#include <stdbool.h>

#if defined(USE_FULL_ASSERT)
#include "py32_assert.h"
#endif /* USE_FULL_ASSERT */

/* Private includes ----------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Firmware version, exposed big-endian in registers 0x00-0x01 */
#define FW_VERSION 0x0001u
#define HW_VERSION 0x0100u // hardware version, used for peripheral mapping and other hardware-specific behavior

/* I2C slave address (7-bit) */
#define I2C_SLAVE_ADDR 0x36

/* Register map */
#define REG_COUNT            256  /* 0x00-0xFF, one byte each */
#define REG_FW_VERSION_HI   0x00 /* R/O: firmware version, high byte */
#define REG_SOFT_RESET      0x00 /* W: command register (same address as version hi):
                                     writing any non-zero value soft-resets the MCU */
#define REG_FW_VERSION_LO   0x01 /* R/O: firmware version, low byte */
#define REG_CONFIG           0x02 /* R/W: configuration bits, default 0x00 */
#define REG_ENC_COUNT_HI     0x03 /* R/W: encoder rotation count, high byte */
#define REG_ENC_COUNT_LO     0x04 /* R/W: encoder rotation count, low byte */
#define REG_ENC_PUSH_COUNT   0x05 /* R/W: encoder push button count */
#define REG_ENC_PUSH_STATE   0x06 /* R/O: encoder push button state */
#define REG_ANIMATION         0x07 /* R/W: active animation, see animations.h:
                                     0x00 = IDLE (custom control over the LEDs),
                                     0x01 = LOADING (default), 0x02 = BREATHING, ... */
/* 0x08-0x0F: reserved, reads as 0x00, writes ignored */
#define REG_LED_BASE         0x10 /* R/W: LED brightness, one register per LED */
#define REG_LED_COUNT        12   /* 0x10-0x1B */
/* 0x1C-0x1F: reserved, reads as 0x00, writes ignored */
#define REG_GP_BASE          0x20 /* R/W: general-purpose I2C RAM */
#define REG_GP_DEFAULT       0x00

/* REG_CONFIG bit definitions */
#define CFG_ROT_RESET_ON_READ  (1u << 0) /* 1: reset rotation count after its low byte is read */
#define CFG_PUSH_RESET_ON_READ (1u << 1) /* 1: reset push count after it is read */
#define CFG_ENC_DIR_FLIP       (1u << 2) /* 1: flip encoder increment direction */
#define CFG_PUSH_COUNT_DEC     (1u << 3) /* 1: decrement push count on press, 0: increment */
#define CFG_PUSH_STATE_FLIP    (1u << 4) /* 1: invert reported push button state */
#define CFG_LED_LINEAR         (1u << 5) /* 0: gamma-correct LED brightness, 1: linear */
/* bits 6-7: reserved, always read 0 */

/* Exported macro ------------------------------------------------------------*/
/* Exported functions prototypes ---------------------------------------------*/
void APP_ErrorHandler(void);
void APP_SlaveIRQCallback(void);
void APP_SlaveIRQCallback_NACK(void);
void APP_MarkRegsDirty(void);
void APP_ResetRequest(void);

/* Private defines -----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT Puya *****END OF FILE****/
