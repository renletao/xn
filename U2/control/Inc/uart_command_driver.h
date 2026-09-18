#ifndef UART_COMMAND_DRIVER_H
#define UART_COMMAND_DRIVER_H

#include <stdint.h>

/* U1 Display 发给 U2 的 LED 总开关命令。 */
#define UART_COMMAND_LED_CONTROL 0x01U

/* U1 Display 查询充电状态、电量、气压和压力泵状态的命令。请求无载荷，返回 7 字节。 */
#define UART_COMMAND_CHARGING_STATUS          0x02U
#define UART_COMMAND_CHARGING_STATUS_LENGTH   7U
#define UART_COMMAND_CHARGING_OFF             0x00U
#define UART_COMMAND_CHARGING_ON              0x01U
#define UART_COMMAND_BATTERY_MAX_PERCENT      100U
#define UART_COMMAND_LED_OFF     0x00U
#define UART_COMMAND_LED_ON      0x01U

/* 压力泵启动/暂停命令，与 U1 Display 协议保持一致。 */
#define UART_COMMAND_PUMP_START               0x03U
#define UART_COMMAND_PUMP_PAUSE               0x04U
#define UART_COMMAND_PUMP_PAYLOAD_LENGTH      5U
#define UART_COMMAND_PUMP_RESPONSE_LENGTH     1U
#define UART_COMMAND_PUMP_MAX_PRESSURE_PA     99900000U
/* 0.4 PSI = 2757.9 Pa，内部压力统一按四舍五入后的 2758 Pa 比较。 */
#define UART_COMMAND_PUMP_SWITCH_PRESSURE_PA  2758U
#define UART_COMMAND_PUMP_STOPPED             0x00U
#define UART_COMMAND_PUMP_RUNNING             0x01U
#define UART_COMMAND_PUMP_ERROR               0x02U
#define UART_COMMAND_MODE_RAFT                 0x00U
#define UART_COMMAND_MODE_AIR_BED              0x01U
#define UART_COMMAND_MODE_TIRE                 0x02U

/*
 * 模拟数据：电池尚未接入，CMD=0x02 使用全局电量值返回；气压已改为
 * 返回 WF183D 实时采集值。电量后续接入电池换算后可替换该变量。
 */
extern volatile uint8_t g_uart_simulated_battery_percent;
/* 仅在 WF183D_USE_REAL_SENSOR=0 时作为气压模拟值保留。 */
extern volatile uint32_t g_uart_simulated_pressure_pa;
/* 目标气压按“设置时加去皮值”保存，单位 Pa；协议范围为 1~99900000。 */
extern volatile uint32_t g_uart_pump_target_pressure_pa;
extern volatile uint8_t g_uart_pump_mode;
extern volatile uint8_t g_uart_pump_running;

/* 初始化 U2 半双工底层和 AA55 协议层。 */
void uart_command_init(void);
/* 主循环处理完整帧、超时半帧，并在有效命令后返回 ACK。 */
void uart_command_task(void);
/* 根据实时去皮气压、目标气压和模式更新两路压力泵。 */
void uart_command_pump_task(void);
/* USART1 中断入口调用。 */
void uart_command_irq_handler(void);

#endif /* UART_COMMAND_DRIVER_H */
