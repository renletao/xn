#ifndef UART_PROTOCOL_DRIVER_H
#define UART_PROTOCOL_DRIVER_H

#include <stdint.h>

#define UART_PROTOCOL_HEADER_1          0xAAU
#define UART_PROTOCOL_HEADER_2          0x55U
#define UART_PROTOCOL_MAX_PAYLOAD       48U
#define UART_PROTOCOL_FRAME_TIMEOUT_MS  100U

/* 初始化 AA55 协议状态机；应在启动底层接收中断前调用。 */
void uart_protocol_init(void);
/* USART 接收完成回调送入一个字节，函数在中断上下文运行。 */
void uart_protocol_irq_feed_byte(uint8_t byte);
/* 主循环清理超时半帧。 */
void uart_protocol_timeout_scan(void);
/* 组装并发送一帧 AA55 数据。 */
int32_t uart_protocol_send(uint8_t cmd, const uint8_t *payload,
                           uint8_t payload_length);
/* 查询是否存在校验成功的完整帧。 */
uint8_t uart_protocol_frame_available(void);
/* 清除完整帧和半帧状态。 */
void uart_protocol_discard_frame(void);
/* 读取一帧完整数据，成功返回 1。 */
uint8_t uart_protocol_get_frame(uint8_t *cmd, uint8_t *payload,
                                uint8_t *payload_length);

#endif /* UART_PROTOCOL_DRIVER_H */
