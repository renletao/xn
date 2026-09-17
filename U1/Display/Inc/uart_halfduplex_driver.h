#ifndef UART_HALF_DUPLEX_DRIVER_H
#define UART_HALF_DUPLEX_DRIVER_H

#include <stdint.h>

/* USART1 单线半双工默认通信参数：PB4、AF1、115200 baud、8N1。 */
#define UART_HALF_DUPLEX_BAUDRATE       115200U
#define UART_HALF_DUPLEX_TIMEOUT_MS     100U

/* 初始化 USART1 和 PB4，并默认进入接收方向。 */
void uart_halfduplex_init(void);
/* 切换到发送方向；发送数据前调用。 */
void uart_halfduplex_set_tx(void);
/* 切换到接收方向；等待从设备响应前调用。 */
void uart_halfduplex_set_rx(void);
/* USART1 中断入口调用的接收处理函数。 */
void uart_halfduplex_irq_handler(void);
/* 发送指定长度的数据，返回实际发送长度，失败返回 -1。 */
int32_t uart_halfduplex_send(const uint8_t *data, uint16_t length,
                             uint32_t timeout_ms);

#endif /* UART_HALF_DUPLEX_DRIVER_H */
