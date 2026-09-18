#ifndef UART_HALF_DUPLEX_DRIVER_H
#define UART_HALF_DUPLEX_DRIVER_H

#include <stdint.h>

/* U2 PA6 使用 USART1/AF1 单线半双工，参数与 Display 最新框架一致。 */
#define UART_HALF_DUPLEX_BAUDRATE       115200U
#define UART_HALF_DUPLEX_TIMEOUT_MS     100U

/* 初始化 USART1 和 PA6，并默认进入接收方向。 */
void uart_halfduplex_init(void);
void uart_halfduplex_suspend(void);
void uart_halfduplex_resume(void);
/* 切换到发送方向；发送数据前调用。 */
void uart_halfduplex_set_tx(void);
/* 切换到接收方向；发送完成后调用。 */
void uart_halfduplex_set_rx(void);
/* USART1 中断入口调用的接收处理函数。 */
void uart_halfduplex_irq_handler(void);
/* 发送指定长度的数据，返回实际发送长度，失败返回 -1。 */
int32_t uart_halfduplex_send(const uint8_t *data, uint16_t length,
                             uint32_t timeout_ms);

#endif /* UART_HALF_DUPLEX_DRIVER_H */
