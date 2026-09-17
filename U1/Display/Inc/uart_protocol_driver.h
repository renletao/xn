#ifndef UART_PROTOCOL_DRIVER_H
#define UART_PROTOCOL_DRIVER_H

#include <stdint.h>

/*
 * 应用层串口协议格式：
 *   AA 55 LEN CMD PAYLOAD[LEN] CHK
 * LEN 只表示有效载荷长度，不包含帧头、CMD 和 CHK。
 */
#define UART_PROTOCOL_HEADER_1       0xAAU
#define UART_PROTOCOL_HEADER_2       0x55U
#define UART_PROTOCOL_MAX_PAYLOAD    48U
#define UART_PROTOCOL_FRAME_TIMEOUT_MS 100U

/* 初始化协议解析器；统一串口入口在启动接收中断前调用。 */
void uart_protocol_init(void);

/*
 * 在 USART 接收完成回调中送入一个字节。
 * 协议状态机在中断上下文中运行，收到完整帧后写入接收邮箱。
 */
void uart_protocol_irq_feed_byte(uint8_t byte);

/* 主循环调用，清理超过100ms仍未完成的半帧。 */
void uart_protocol_timeout_scan(void);

/*
 * 组装并发送一帧协议数据。
 * 返回实际发送的帧长度，参数错误或底层发送失败返回 -1。
 */
int32_t uart_protocol_send(uint8_t cmd, const uint8_t *payload,
                           uint8_t payload_length);

/* 查询是否有一帧校验成功、尚未读取的接收数据。 */
uint8_t uart_protocol_frame_available(void);

/* 丢弃尚未处理的数据包和半帧，开始新的通信事务前调用。 */
void uart_protocol_discard_frame(void);

/*
 * 读取一帧已经校验成功的数据。
 * 返回 1 表示读取成功，返回 0 表示暂无完整帧或参数错误。
 */
uint8_t uart_protocol_get_frame(uint8_t *cmd, uint8_t *payload,
                                uint8_t *payload_length);

#endif /* UART_PROTOCOL_DRIVER_H */
