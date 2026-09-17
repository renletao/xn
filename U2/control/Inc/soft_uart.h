/**
  * @file    soft_uart.h
  * @brief   PB4/PB5 上的 9600 8N1 软件串口驱动。
  *
  * PB4 为发送脚，PB5 为接收脚，使用 TIM14 的 1 MHz 自由运行计数器
  * 生成约 104 us 的 bit 时序。收发接口是阻塞式，适合查询/应答型传感器。
  */

#ifndef __U1_SOFT_UART_H
#define __U1_SOFT_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_UART_BAUDRATE       9600U  /* 1 起始位 + 8 数据位 + 1 停止位。 */
#define SOFT_UART_FRAME_BITS     10U

/** 初始化 PB4、PB5 和 TIM14，串口空闲状态为高电平。 */
void soft_uart_init(void);
/** 阻塞发送数据，成功返回发送字节数，失败返回 -1。timeout_ms 为 0 表示不超时。 */
int32_t soft_uart_send(const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms);
/** 阻塞发送单字节，成功返回 1，失败返回 -1。 */
int32_t soft_uart_send_byte(uint8_t data, uint32_t timeout_ms);
/** 阻塞接收指定长度数据，返回已接收字节数；超时未收满时返回已收数量。 */
int32_t soft_uart_receive(uint8_t *data, uint16_t length,
                          uint32_t timeout_ms);
/** 接收单字节：1 表示成功，0 表示超时/起始位无效，-2 表示停止位错误。 */
int32_t soft_uart_receive_byte(uint8_t *data, uint32_t timeout_ms);
/** 返回软件串口底层是否已经初始化成功。 */
uint8_t soft_uart_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_SOFT_UART_H */
