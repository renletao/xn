#ifndef UART_CONTROL_DRIVER_H
#define UART_CONTROL_DRIVER_H

#include <stdint.h>
#include "uart_protocol_driver.h"

typedef enum
{
    UART_CONTROL_RESULT_IDLE = 0,
    UART_CONTROL_RESULT_PENDING,
    UART_CONTROL_RESULT_SUCCESS,
    UART_CONTROL_RESULT_BUSY,
    UART_CONTROL_RESULT_TIMEOUT,
    UART_CONTROL_RESULT_SEND_ERROR,
    UART_CONTROL_RESULT_BAD_RESPONSE,
    UART_CONTROL_RESULT_RX_ERROR,
    UART_CONTROL_RESULT_OVERFLOW
} UartControlResult_t;

/* 统一初始化半双工底层、协议解析器和事务控制状态。 */
void uart_control_init(void);
void uart_control_suspend(void);
void uart_control_resume(void);
/* 主循环任务：非阻塞处理完整返回帧和100ms超时。 */
void uart_control_task(void);
/* USART1中断入口调用。 */
void uart_control_irq_handler(void);

/* 提交请求；返回1表示已发送并进入等待，0表示参数错误或串口忙。 */
uint8_t uart_control_request(uint8_t cmd, const uint8_t *payload,
                             uint8_t length);
/* 查询事务锁：1表示正在发送或等待返回。 */
uint8_t uart_control_is_busy(void);
/* 查询最近一次事务结果。 */
UartControlResult_t uart_control_get_result(void);
/* 读取最近一次成功事务的完整返回数据。 */
uint8_t uart_control_get_response(uint8_t *cmd, uint8_t *payload,
                                  uint8_t *length);

#endif /* UART_CONTROL_DRIVER_H */
