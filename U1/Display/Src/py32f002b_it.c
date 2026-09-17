/**
  ******************************************************************************
  * @file    py32f002b_it.c
  * @author  MCU Application Team
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "py32f0xx_hal.h"
#include "py32f002b_it.h"
#include "uart_control_driver.h"

/* Private includes ----------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private user code ---------------------------------------------------------*/
/* External variables --------------------------------------------------------*/

/******************************************************************************/
/*           Cortex-M0+ Processor Interruption and Exception Handlers         */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* 当前工程没有使用 NMI；保留空处理函数以匹配向量表。 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* 硬件异常后停机，便于通过调试器查看故障现场。 */
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* 当前工程未使用 SVC。 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* 当前工程未使用 PendSV/RTOS。 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* Keep the HAL time base running for led_scan(), key_scan() and delays. */
  HAL_IncTick();
}

/******************************************************************************/
/* PY32F002B Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file.                                          */
/******************************************************************************/
/* 当前已启用 USART1 接收中断；其他外设需要中断时可在此继续添加 ISR。 */

/**
  * @brief USART1 中断处理函数。
  *
  * 单线串口驱动在此接收字节，并在接收回调中推进 AA55 协议状态机；
  * 主循环只处理已经校验完成的完整数据包。
  */
void USART1_IRQHandler(void)
{
  uart_control_irq_handler();
}

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
