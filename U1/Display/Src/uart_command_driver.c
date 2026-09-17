#include "uart_command_driver.h"
#include "main.h"
#include "uart_halfduplex_driver.h"
#include "uart_protocol_driver.h"

/* 事务锁覆盖“发送请求到收到返回或超时”的完整过程。 */
static volatile uint8_t s_transaction_busy;
static uint8_t s_expected_cmd;
static uint32_t s_response_deadline;
static volatile UartControlResult_t s_result;

/* 保存最近一次成功返回，供后续扩展命令读取。 */
static uint8_t s_response_cmd;
static uint8_t s_response_length;
static uint8_t s_response_payload[UART_PROTOCOL_MAX_PAYLOAD];

/* 最近一次有效的 CMD=0x02 数据，由显示层在主循环中消费。 */
static uint8_t s_charging_status_pending;
static uint8_t s_charging_state;
static uint8_t s_battery_percent;
static uint32_t s_pressure_pa;
static uint32_t s_status_poll_tick;
static uint8_t s_pump_running;

/* LED状态只在收到U2确认后更新，避免发送失败时本地状态假成功。 */
static uint8_t s_remote_led_state = UART_COMMAND_LED_ON;

static uint8_t uart_control_try_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t acquired = 0U;

    /* 只在检查并设置锁的极短区间内关中断，不影响100ms接收等待。 */
    __disable_irq();
    if (s_transaction_busy == 0U)
    {
        s_transaction_busy = 1U;
        acquired = 1U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
    return acquired;
}

static void uart_control_unlock(void)
{
    s_transaction_busy = 0U;
}

static uint32_t uart_command_decode_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void uart_command_encode_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint8_t uart_command_validate_response(uint8_t cmd,
                                               const uint8_t *payload,
                                               uint8_t length)
{
    if (cmd != UART_COMMAND_LED_CONTROL)
    {
        if (cmd == UART_COMMAND_CHARGING_STATUS)
        {
            return (length == UART_COMMAND_CHARGING_STATUS_LENGTH) &&
                   ((payload[0] == UART_COMMAND_CHARGING_OFF) ||
                    (payload[0] == UART_COMMAND_CHARGING_ON)) &&
                   (payload[1] <= UART_COMMAND_BATTERY_MAX_PERCENT);
        }

        if ((cmd == UART_COMMAND_PUMP_START) ||
            (cmd == UART_COMMAND_PUMP_PAUSE))
        {
            return (length == UART_COMMAND_PUMP_RESPONSE_LENGTH) &&
                   (payload[0] <= UART_COMMAND_PUMP_ERROR);
        }

        /* 未实现的命令先按协议层合法帧交给上层，便于后续扩展。 */
        return 1U;
    }

    return (length == 1U) &&
           ((payload[0] == UART_COMMAND_LED_OFF) ||
            (payload[0] == UART_COMMAND_LED_ON));
}

void uart_control_init(void)
{
    uint8_t index;

    /* 协议状态先初始化，再启动底层接收中断，避免初始化期间使用未初始化邮箱。 */
    uart_protocol_init();
    uart_halfduplex_init();

    s_transaction_busy = 0U;
    s_expected_cmd = 0U;
    s_response_deadline = 0U;
    s_result = UART_CONTROL_RESULT_IDLE;
    s_response_cmd = 0U;
    s_response_length = 0U;
    s_remote_led_state = UART_COMMAND_LED_ON;
    s_charging_status_pending = 0U;
    s_charging_state = UART_COMMAND_CHARGING_OFF;
    s_battery_percent = 0U;
    s_pressure_pa = 0U;
    s_status_poll_tick = HAL_GetTick();
    s_pump_running = 0U;
    for (index = 0U; index < UART_PROTOCOL_MAX_PAYLOAD; ++index)
    {
        s_response_payload[index] = 0U;
    }
}

void uart_control_irq_handler(void)
{
    /* 底层HAL中断回调会把每个字节送入协议层状态机。 */
    uart_halfduplex_irq_handler();
}

uint8_t uart_control_request(uint8_t cmd, const uint8_t *payload,
                             uint8_t length)
{
    if ((length > UART_PROTOCOL_MAX_PAYLOAD) ||
        ((length > 0U) && (payload == 0)))
    {
        s_result = UART_CONTROL_RESULT_BAD_RESPONSE;
        return 0U;
    }

    if (uart_control_try_lock() == 0U)
    {
        s_result = UART_CONTROL_RESULT_BUSY;
        return 0U;
    }

    /* 新事务丢弃旧完整帧和半帧，避免旧数据被误认为当前返回。 */
    uart_protocol_discard_frame();
    s_expected_cmd = cmd;
    s_result = UART_CONTROL_RESULT_PENDING;

    if (uart_protocol_send(cmd, payload, length) < 0)
    {
        uart_control_unlock();
        s_result = UART_CONTROL_RESULT_SEND_ERROR;
        return 0U;
    }

    /* 发送完成后已经切回接收；从此时开始计算100ms返回窗口。 */
    s_response_deadline = HAL_GetTick() + UART_COMMAND_RESPONSE_TIMEOUT_MS;
    return 1U;
}

void uart_control_task(void)
{
    uint8_t command;
    uint8_t length;
    uint8_t index;
    uint8_t payload[UART_PROTOCOL_MAX_PAYLOAD];

    /* 主循环维护半帧超时，避免异常中断或断线后解析器长期卡在半帧状态。 */
    uart_protocol_timeout_scan();

    if (s_transaction_busy != 0U)
    {
        if (uart_protocol_frame_available() != 0U)
        {
            if (uart_protocol_get_frame(&command, payload, &length) != 0U)
            {
                if (command != s_expected_cmd)
                {
                    /* 不是当前事务的返回，丢弃后继续等待当前CMD。 */
                    return;
                }

                if (uart_command_validate_response(command, payload, length) == 0U)
                {
                    s_result = UART_CONTROL_RESULT_BAD_RESPONSE;
                    uart_control_unlock();
                    return;
                }

                s_response_cmd = command;
                s_response_length = length;
                for (index = 0U; index < length; ++index)
                {
                    s_response_payload[index] = payload[index];
                }

                if ((s_response_cmd == UART_COMMAND_LED_CONTROL) &&
                    (s_response_length == 1U))
                {
                    s_remote_led_state = s_response_payload[0];
                }
                else if ((s_response_cmd == UART_COMMAND_CHARGING_STATUS) &&
                         (s_response_length == UART_COMMAND_CHARGING_STATUS_LENGTH))
                {
                    s_charging_state = s_response_payload[0];
                    s_battery_percent = s_response_payload[1];
                    s_pressure_pa = uart_command_decode_u32_le(
                        &s_response_payload[2]);
                    s_charging_status_pending = 1U;
                }
                else if (((s_response_cmd == UART_COMMAND_PUMP_START) ||
                          (s_response_cmd == UART_COMMAND_PUMP_PAUSE)) &&
                         (s_response_length == UART_COMMAND_PUMP_RESPONSE_LENGTH))
                {
                    s_pump_running =
                        (s_response_payload[0] == UART_COMMAND_PUMP_RUNNING) ?
                        1U : 0U;
                }
                s_result = UART_CONTROL_RESULT_SUCCESS;
                uart_control_unlock();
                return;
            }
        }

        /* 使用无符号差值判断截止时间，兼容HAL Tick溢出。 */
        if ((int32_t)(HAL_GetTick() - s_response_deadline) >= 0)
        {
            s_result = UART_CONTROL_RESULT_TIMEOUT;
            uart_control_unlock();
        }
    }
    else
    {
        /* U1是主控，空闲时收到的无主数据包不参与任何业务处理。 */
        if (uart_protocol_frame_available() != 0U)
        {
            (void)uart_protocol_get_frame(&command, payload, &length);
        }
    }
}

uint8_t uart_control_is_busy(void)
{
    return s_transaction_busy;
}

UartControlResult_t uart_control_get_result(void)
{
    return s_result;
}

uint8_t uart_control_get_response(uint8_t *cmd, uint8_t *payload,
                                  uint8_t *length)
{
    uint8_t index;

    if ((cmd == 0) || (length == 0U) ||
        (s_result != UART_CONTROL_RESULT_SUCCESS) ||
        ((s_response_length > 0U) && (payload == 0)))
    {
        return 0U;
    }

    *cmd = s_response_cmd;
    *length = s_response_length;
    for (index = 0U; index < s_response_length; ++index)
    {
        payload[index] = s_response_payload[index];
    }
    return 1U;
}

uint8_t uart_command_set_remote_led(uint8_t enabled)
{
    if ((enabled != UART_COMMAND_LED_OFF) &&
        (enabled != UART_COMMAND_LED_ON))
    {
        return 0U;
    }

    return uart_control_request(UART_COMMAND_LED_CONTROL, &enabled, 1U);
}

uint8_t uart_command_toggle_remote_led(void)
{
    uint8_t desired_state;

    if (uart_control_is_busy() != 0U)
    {
        return 0U;
    }

    desired_state = (s_remote_led_state == UART_COMMAND_LED_ON) ?
                    UART_COMMAND_LED_OFF : UART_COMMAND_LED_ON;
    return uart_command_set_remote_led(desired_state);
}

void uart_command_scan(void)
{
    uint32_t now = HAL_GetTick();

    /* S3 手动命令或其他事务正在等待返回时，延后本次状态查询。 */
    if (uart_control_is_busy() != 0U)
    {
        return;
    }

    if ((uint32_t)(now - s_status_poll_tick) <
        UART_COMMAND_STATUS_POLL_PERIOD_MS)
    {
        return;
    }

    /* 无载荷提交查询；只有成功抢到事务锁后才推进下一个轮询周期。 */
    if (uart_control_request(UART_COMMAND_CHARGING_STATUS, 0, 0U) != 0U)
    {
        s_status_poll_tick = now;
    }
}

uint8_t uart_command_take_charging_status(uint8_t *charging,
                                           uint8_t *battery_percent,
                                           uint32_t *pressure_pa)
{
    if ((charging == 0) || (battery_percent == 0) || (pressure_pa == 0) ||
        (s_charging_status_pending == 0U))
    {
        return 0U;
    }

    *charging = s_charging_state;
    *battery_percent = s_battery_percent;
    *pressure_pa = s_pressure_pa;
    s_charging_status_pending = 0U;
    return 1U;
}

static uint8_t uart_command_valid_pump_mode(uint8_t mode)
{
    return (mode <= UART_COMMAND_MODE_TIRE) ? 1U : 0U;
}

uint8_t uart_command_start_pump(uint32_t target_pressure_pa, uint8_t mode)
{
    uint8_t payload[UART_COMMAND_PUMP_PAYLOAD_LENGTH];

    if ((target_pressure_pa == 0U) ||
        (target_pressure_pa > UART_COMMAND_PUMP_MAX_PRESSURE_PA) ||
        (uart_command_valid_pump_mode(mode) == 0U))
    {
        return 0U;
    }

    uart_command_encode_u32_le(payload, target_pressure_pa);
    payload[4] = mode;
    return uart_control_request(UART_COMMAND_PUMP_START, payload,
                                UART_COMMAND_PUMP_PAYLOAD_LENGTH);
}

uint8_t uart_command_pause_pump(void)
{
    return uart_control_request(UART_COMMAND_PUMP_PAUSE, 0, 0U);
}

uint8_t uart_command_toggle_pump(uint32_t target_pressure_pa, uint8_t mode)
{
    if (uart_control_is_busy() != 0U)
    {
        return 0U;
    }

    if (s_pump_running != 0U)
    {
        return uart_command_pause_pump();
    }

    return uart_command_start_pump(target_pressure_pa, mode);
}

uint8_t uart_command_is_pump_running(void)
{
    return s_pump_running;
}
