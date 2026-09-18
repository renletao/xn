/**
  * @file    uart_halfduplex_driver.c
  * @brief   U2 PA6 USART1 单线半双工底层驱动。
  *
  * 接收回调只把字节转交给协议状态机，业务处理在主循环完成；发送 ACK
  * 时临时切换为发送方向，发送结束后恢复接收，避免总线长期被 U2 占用。
  */

#include "uart_halfduplex_driver.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "uart_protocol_driver.h"
#include "py32_u1_hw_config.h"

static UART_HandleTypeDef uart_handle;
static uint8_t uart_initialized;
static volatile uint8_t uart_tx_active;
static uint8_t uart_rx_byte;

static uint8_t uart_start_receive_interrupt(void)
{
  if (uart_handle.RxState == HAL_UART_STATE_READY)
  {
    return (HAL_UART_Receive_IT(&uart_handle, &uart_rx_byte, 1U) == HAL_OK) ?
           1U : 0U;
  }
  return 1U;
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio = {0};

  if (huart->Instance != USART1)
  {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  /* PA6 在半双工模式下同时承担 U1->U2 接收和 U2->U1 发送。 */
  gpio.Pin = (uint32_t)PIN_DISP_TX_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = PIN_DISP_TX_AF;
  HAL_GPIO_Init(PIN_DISP_TX_PORT, &gpio);
}

void uart_halfduplex_init(void)
{
  __HAL_UART_RESET_HANDLE_STATE(&uart_handle);
  uart_handle.Instance = USART1;
  uart_handle.Init.BaudRate = UART_HALF_DUPLEX_BAUDRATE;
  uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
  uart_handle.Init.StopBits = UART_STOPBITS_1;
  uart_handle.Init.Parity = UART_PARITY_NONE;
  uart_handle.Init.Mode = UART_MODE_TX_RX;
  uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
  uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_HalfDuplex_Init(&uart_handle) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  uart_initialized = 1U;
  uart_tx_active = 0U;
  if (HAL_HalfDuplex_EnableReceiver(&uart_handle) != HAL_OK ||
      uart_start_receive_interrupt() == 0U)
  {
    APP_ErrorHandler();
  }

  HAL_NVIC_SetPriority(USART1_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void uart_halfduplex_suspend(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (uart_initialized == 0U)
  {
    return;
  }
  (void)HAL_UART_AbortReceive(&uart_handle);
  __HAL_UART_RESET_HANDLE_STATE(&uart_handle);
  HAL_NVIC_DisableIRQ(USART1_IRQn);
  __HAL_RCC_USART1_CLK_DISABLE();
  gpio.Pin = (uint32_t)PIN_DISP_TX_PIN;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = 0U;
  HAL_GPIO_Init(PIN_DISP_TX_PORT, &gpio);
  uart_tx_active = 0U;
  uart_initialized = 0U;
}

void uart_halfduplex_resume(void)
{
  uart_halfduplex_init();
}

void uart_halfduplex_set_tx(void)
{
  if ((uart_initialized != 0U) && (uart_tx_active == 0U))
  {
    (void)HAL_UART_AbortReceive(&uart_handle);
    if (HAL_HalfDuplex_EnableTransmitter(&uart_handle) != HAL_OK)
    {
      APP_ErrorHandler();
    }
    uart_tx_active = 1U;
  }
}

void uart_halfduplex_set_rx(void)
{
  if (uart_initialized == 0U)
  {
    return;
  }

  if (uart_tx_active != 0U)
  {
    if (HAL_HalfDuplex_EnableReceiver(&uart_handle) != HAL_OK)
    {
      APP_ErrorHandler();
    }
    uart_tx_active = 0U;
  }

  if (uart_start_receive_interrupt() == 0U)
  {
    APP_ErrorHandler();
  }
}

void uart_halfduplex_irq_handler(void)
{
  if (uart_initialized != 0U)
  {
    HAL_UART_IRQHandler(&uart_handle);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart == &uart_handle) && (huart->Instance == USART1))
  {
    /* 与 Display 最新框架一致：在中断回调中直接推进协议状态机。 */
    uart_protocol_irq_feed_byte(uart_rx_byte);
    (void)uart_start_receive_interrupt();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((huart == &uart_handle) && (huart->Instance == USART1))
  {
    (void)HAL_UART_AbortReceive(huart);
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_HalfDuplex_EnableReceiver(huart);
    uart_tx_active = 0U;
    (void)uart_start_receive_interrupt();
  }
}

int32_t uart_halfduplex_send(const uint8_t *data, uint16_t length,
                             uint32_t timeout_ms)
{
  HAL_StatusTypeDef status;

  if ((uart_initialized == 0U) || (data == 0) || (length == 0U))
  {
    return -1;
  }

  uart_halfduplex_set_tx();
  status = HAL_UART_Transmit(&uart_handle, (uint8_t *)data, length,
                             timeout_ms);
  uart_halfduplex_set_rx();

  return (status == HAL_OK) ? (int32_t)length : -1;
}
