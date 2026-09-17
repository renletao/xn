/**
  ******************************************************************************
  * @file    py32f002b_hal_conf.h
  * @brief   HAL driver configuration for the PY32F002B project.
  *
  * The application uses the PY32 HAL drivers for clocks, GPIO, Flash and
  * UART. This header enables the HAL modules required by the application.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __PY32F002B_HAL_CONF_H
#define __PY32F002B_HAL_CONF_H

/* 保持动态回调注册和 RTOS 支持关闭，使用 HAL 提供的弱回调函数。 */
#define USE_RTOS                         0U
#define USE_HAL_ADC_REGISTER_CALLBACKS   0U
#define USE_HAL_COMP_REGISTER_CALLBACKS  0U
#define USE_HAL_I2C_REGISTER_CALLBACKS   0U
#define USE_HAL_LPTIM_REGISTER_CALLBACKS 0U
#define USE_HAL_SPI_REGISTER_CALLBACKS   0U
#define USE_HAL_TIM_REGISTER_CALLBACKS   0U
#define USE_HAL_UART_REGISTER_CALLBACKS  0U
#define USE_HAL_USART_REGISTER_CALLBACKS 0U
#define TICK_INT_PRIORITY                0U

/* 当前应用使用的 HAL 模块。 */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* HAL RCC 计算系统和外设时钟时使用的振荡器标称值。 */
#if !defined(HSI_VALUE)
#define HSI_VALUE             24000000U
#endif
#if !defined(HSE_VALUE)
#define HSE_VALUE             24000000U
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT   200U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE             32768U
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE             32768U
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT   5000U
#endif

/* py32f0xx_hal.h declares HAL types and functions immediately after including
   this configuration file, so the common HAL definitions must be available. */
#include "py32f002b_hal_def.h"
#include "py32_assert.h"
#include "py32f002b_hal_cortex.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32f002b_hal_flash.h"
#include "py32f002b_hal_uart.h"

#endif /* __PY32F002B_HAL_CONF_H */
