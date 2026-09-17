#include "uart_halfduplex_driver.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "uart_protocol_driver.h"

/* PB4 使用 USART1/AF1；在半双工模式下同一引脚分时承担发送和接收。 */
#define UART_HALF_DUPLEX_PORT GPIOB
#define UART_HALF_DUPLEX_PIN  GPIO_PIN_4
#define UART_HALF_DUPLEX_AF   GPIO_AF1_USART1

static UART_HandleTypeDef s_uart_handle;
static uint8_t s_initialized;
/* 记录当前半双工方向，避免主循环轮询时重复关闭并重开接收器。 */
static volatile uint8_t s_tx_active;
/* HAL 每次中断接收 1 字节，完成后交给协议状态机。 */
static uint8_t s_rx_irq_byte;

static uint8_t uart_halfduplex_start_receive_interrupt(void)
{
    /* HAL 一次接收 1 字节，完成回调会重新启动下一字节接收。 */
    if (s_uart_handle.RxState == HAL_UART_STATE_READY)
    {
        if (HAL_UART_Receive_IT(&s_uart_handle, &s_rx_irq_byte, 1U) != HAL_OK)
        {
            return 0U;
        }
    }
    return 1U;
}

void uart_halfduplex_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使用 HAL RCC 宏打开 GPIOB 和 USART1 外设时钟。 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* PB4 配置为 USART1/AF1 复用推挽，并用上拉保持总线空闲为高。 */
    gpio.Pin = UART_HALF_DUPLEX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = UART_HALF_DUPLEX_AF;
    HAL_GPIO_Init(UART_HALF_DUPLEX_PORT, &gpio);

    /* 使用 HAL Handle 配置 USART1：115200 baud、8N1、无流控。 */
    __HAL_UART_RESET_HANDLE_STATE(&s_uart_handle);
    s_uart_handle.Instance = USART1;
    s_uart_handle.Init.BaudRate = UART_HALF_DUPLEX_BAUDRATE;
    s_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart_handle.Init.StopBits = UART_STOPBITS_1;
    s_uart_handle.Init.Parity = UART_PARITY_NONE;
    s_uart_handle.Init.Mode = UART_MODE_TX_RX;
    s_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    s_uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_HalfDuplex_Init(&s_uart_handle) != HAL_OK)
    {
        app_error_handler();
    }

    s_initialized = 1U;

    /* 默认进入接收方向，并启动第一个字节的中断接收。 */
    if (HAL_HalfDuplex_EnableReceiver(&s_uart_handle) != HAL_OK)
    {
        app_error_handler();
    }
    s_tx_active = 0U;
    HAL_NVIC_SetPriority(USART1_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    if (uart_halfduplex_start_receive_interrupt() == 0U)
    {
        app_error_handler();
    }
}

void uart_halfduplex_set_tx(void)
{
    if ((s_initialized != 0U) && (s_tx_active == 0U))
    {
        /* 发送前终止正在等待的单字节接收，并切换半双工方向。 */
        if (HAL_UART_AbortReceive(&s_uart_handle) != HAL_OK)
        {
            app_error_handler();
        }
        if (HAL_HalfDuplex_EnableTransmitter(&s_uart_handle) != HAL_OK)
        {
            app_error_handler();
        }
        s_tx_active = 1U;
    }
}

void uart_halfduplex_set_rx(void)
{
    if (s_initialized != 0U)
    {
        /* 只有当前确实为发送方向时才切换，避免轮询读取反复扰动接收器。 */
        if (s_tx_active != 0U)
        {
            if (HAL_HalfDuplex_EnableReceiver(&s_uart_handle) != HAL_OK)
            {
                app_error_handler();
            }
            s_tx_active = 0U;
        }
        if (uart_halfduplex_start_receive_interrupt() == 0U)
        {
            app_error_handler();
        }
    }
}

void uart_halfduplex_irq_handler(void)
{
    if (s_initialized != 0U)
    {
        /* USART1 的标志判断、清除和接收状态更新统一交给 HAL。 */
        HAL_UART_IRQHandler(&s_uart_handle);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart == &s_uart_handle) && (huart->Instance == USART1))
    {
        /* 接收字节由协议层在中断中组装为完整数据包。 */
        uart_protocol_irq_feed_byte(s_rx_irq_byte);
        (void)uart_halfduplex_start_receive_interrupt();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == &s_uart_handle) && (huart->Instance == USART1))
    {
        /* 清除错误状态并重新进入接收，避免一次溢出导致后续通信停止。 */
        (void)HAL_UART_AbortReceive(huart);
        __HAL_UART_CLEAR_OREFLAG(huart);
        (void)HAL_HalfDuplex_EnableReceiver(huart);
        s_tx_active = 0U;
        (void)uart_halfduplex_start_receive_interrupt();
    }
}

int32_t uart_halfduplex_send(const uint8_t *data, uint16_t length,
                             uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    if ((s_initialized == 0U) || (data == 0) || (length == 0U))
    {
        return -1;
    }

    /* 主控开始新事务时先停止正在等待的单字节接收。 */
    uart_halfduplex_set_tx();

    /* HAL 阻塞发送会等待最后一个停止位发送完成，然后才能释放总线。 */
    status = HAL_UART_Transmit(&s_uart_handle, (uint8_t *)data, length,
                               timeout_ms);
    uart_halfduplex_set_rx();

    return (status == HAL_OK) ? (int32_t)length : -1;
}
