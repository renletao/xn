#ifndef UART_COMMAND_DRIVER_H
#define UART_COMMAND_DRIVER_H

#include <stdint.h>
#include "uart_control_driver.h"

/* LED 总开关命令；请求和返回均使用 CMD=0x01。 */
#define UART_COMMAND_LED_CONTROL 0x01U

/* 充电状态、电量、气压和压力泵状态查询命令；请求无载荷，返回 7 字节载荷。 */
#define UART_COMMAND_CHARGING_STATUS 0x02U
#define UART_COMMAND_CHARGING_STATUS_LENGTH 7U
#define UART_COMMAND_CHARGING_OFF  0x00U
#define UART_COMMAND_CHARGING_ON   0x01U
#define UART_COMMAND_BATTERY_MAX_PERCENT 100U
#define UART_COMMAND_STATUS_POLL_PERIOD_MS 1000U

/* 压力泵控制命令；目标压力使用 Pa 的 uint32_t 小端格式。 */
#define UART_COMMAND_PUMP_START 0x03U
#define UART_COMMAND_PUMP_PAUSE 0x04U
#define UART_COMMAND_PUMP_PAYLOAD_LENGTH 5U
#define UART_COMMAND_PUMP_RESPONSE_LENGTH 1U
/* 与当前显示目标的最大范围 999.00 BAR 对齐：999.00 BAR = 99,900,000 Pa。 */
#define UART_COMMAND_PUMP_MAX_PRESSURE_PA 99900000U
#define UART_COMMAND_PUMP_STOPPED 0x00U
#define UART_COMMAND_PUMP_RUNNING 0x01U
#define UART_COMMAND_PUMP_ERROR   0x02U

/* 与模式指示灯枚举保持一致。 */
#define UART_COMMAND_MODE_RAFT    0x00U
#define UART_COMMAND_MODE_AIR_BED 0x01U
#define UART_COMMAND_MODE_TIRE    0x02U

/* CMD=0x01 的载荷取值。 */
#define UART_COMMAND_LED_OFF     0x00U
#define UART_COMMAND_LED_ON      0x01U

/* 每次发送请求后，等待匹配返回帧的最大时间。 */
#define UART_COMMAND_RESPONSE_TIMEOUT_MS 100U

/* 向 U2 提交PB3灯控制请求；1=已发送并进入等待，0=参数错误或串口忙。 */
uint8_t uart_command_set_remote_led(uint8_t enabled);

/* 提交切换U2 PB3灯的请求；实际结果由uart_control_task()完成。 */
uint8_t uart_command_toggle_remote_led(void);

/* 主循环周期调用；到期时提交一次 CMD=0x02 查询，不阻塞等待返回。 */
void uart_command_scan(void);

/* 读取并清除最近一次有效的 CMD=0x02 数据；1=有新数据，0=暂无。
 * 返回中的压力泵状态已同步到 uart_command_is_pump_running()。 */
uint8_t uart_command_take_charging_status(uint8_t *charging,
                                           uint8_t *battery_percent,
                                           uint32_t *pressure_pa);

/* 提交启动/暂停压力泵请求；实际状态由 uart_control_task() 的返回帧确认。 */
uint8_t uart_command_start_pump(uint32_t target_pressure_pa, uint8_t mode);
uint8_t uart_command_pause_pump(void);

/* S1 短按先查询 U2 当前泵状态，再提交启动/暂停切换；
 * 1=已提交查询或复用了进行中的查询，0=串口被其他命令占用。 */
uint8_t uart_command_toggle_pump(uint32_t target_pressure_pa, uint8_t mode);

/* 返回最近一次 U2 确认的压力泵状态：1=运行，0=停止/暂停。 */
uint8_t uart_command_is_pump_running(void);

#endif /* UART_COMMAND_DRIVER_H */
