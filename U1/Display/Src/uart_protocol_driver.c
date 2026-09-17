#include "uart_protocol_driver.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "uart_halfduplex_driver.h"

/* 协议解析状态机在 USART 接收中断中运行，主循环只读取完整帧邮箱。 */
typedef enum
{
    UART_PROTOCOL_STATE_WAIT_HEADER_1 = 0,
    UART_PROTOCOL_STATE_WAIT_HEADER_2,
    UART_PROTOCOL_STATE_READ_LENGTH,
    UART_PROTOCOL_STATE_READ_COMMAND,
    UART_PROTOCOL_STATE_READ_PAYLOAD,
    UART_PROTOCOL_STATE_READ_CHECKSUM
} uart_protocol_state_t;

static uart_protocol_state_t s_parser_state;
static uint8_t s_parser_length;
static uint8_t s_parser_command;
static uint8_t s_parser_index;
static uint8_t s_parser_checksum;
static uint8_t s_parser_payload[UART_PROTOCOL_MAX_PAYLOAD];
static uint32_t s_parser_last_tick;

/* 只保留一帧待业务读取的数据，避免增加不必要的协议队列层。 */
static uint8_t s_frame_ready;
static uint8_t s_frame_command;
static uint8_t s_frame_length;
static uint8_t s_frame_payload[UART_PROTOCOL_MAX_PAYLOAD];

static void uart_protocol_parser_reset(void)
{
    /* 回到搜索帧头状态；之前未完成的帧会被丢弃。 */
    s_parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_1;
    s_parser_length = 0U;
    s_parser_command = 0U;
    s_parser_index = 0U;
    s_parser_checksum = 0U;
    s_parser_last_tick = HAL_GetTick();
}

static uint8_t uart_protocol_checksum(uint8_t length, uint8_t command,
                                       const uint8_t *payload)
{
    uint8_t checksum = (uint8_t)(length + command);
    uint8_t index;

    for (index = 0U; index < length; ++index)
    {
        checksum = (uint8_t)(checksum + payload[index]);
    }
    return checksum;
}

static void uart_protocol_restart_from_byte(uint8_t byte)
{
    /* 当前字节若是 AA，可直接作为下一帧的第一个帧头字节。 */
    if (byte == UART_PROTOCOL_HEADER_1)
    {
        s_parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_2;
    }
    else
    {
        s_parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_1;
    }
    s_parser_length = 0U;
    s_parser_command = 0U;
    s_parser_index = 0U;
    s_parser_checksum = 0U;
    s_parser_last_tick = HAL_GetTick();
}

void uart_protocol_irq_feed_byte(uint8_t byte)
{
    switch (s_parser_state)
    {
        case UART_PROTOCOL_STATE_WAIT_HEADER_1:
            if (byte == UART_PROTOCOL_HEADER_1)
            {
                s_parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_2;
            }
            break;

        case UART_PROTOCOL_STATE_WAIT_HEADER_2:
            if (byte == UART_PROTOCOL_HEADER_2)
            {
                s_parser_state = UART_PROTOCOL_STATE_READ_LENGTH;
            }
            else
            {
                uart_protocol_restart_from_byte(byte);
            }
            break;

        case UART_PROTOCOL_STATE_READ_LENGTH:
            if (byte > UART_PROTOCOL_MAX_PAYLOAD)
            {
                uart_protocol_restart_from_byte(byte);
            }
            else
            {
                s_parser_length = byte;
                s_parser_index = 0U;
                s_parser_checksum = byte;
                s_parser_state = UART_PROTOCOL_STATE_READ_COMMAND;
            }
            break;

        case UART_PROTOCOL_STATE_READ_COMMAND:
            s_parser_command = byte;
            s_parser_checksum = (uint8_t)(s_parser_checksum + byte);
            s_parser_state = (s_parser_length == 0U) ?
                             UART_PROTOCOL_STATE_READ_CHECKSUM :
                             UART_PROTOCOL_STATE_READ_PAYLOAD;
            break;

        case UART_PROTOCOL_STATE_READ_PAYLOAD:
            s_parser_payload[s_parser_index] = byte;
            s_parser_checksum = (uint8_t)(s_parser_checksum + byte);
            ++s_parser_index;
            if (s_parser_index >= s_parser_length)
            {
                s_parser_state = UART_PROTOCOL_STATE_READ_CHECKSUM;
            }
            break;

        case UART_PROTOCOL_STATE_READ_CHECKSUM:
            if (byte == s_parser_checksum)
            {
                /* 有旧帧尚未读取时，保留旧帧，丢弃后续帧避免覆盖业务数据。 */
                if (s_frame_ready == 0U)
                {
                    uint8_t index;
                    s_frame_command = s_parser_command;
                    s_frame_length = s_parser_length;
                    for (index = 0U; index < s_parser_length; ++index)
                    {
                        s_frame_payload[index] = s_parser_payload[index];
                    }
                    s_frame_ready = 1U;
                }
            }
            /* 校验成功或失败后都重新搜索下一帧。 */
            uart_protocol_restart_from_byte(0U);
            break;

        default:
            uart_protocol_parser_reset();
            break;
    }

    s_parser_last_tick = HAL_GetTick();
}

void uart_protocol_init(void)
{
    uint8_t index;

    uart_protocol_parser_reset();
    s_frame_ready = 0U;
    s_frame_command = 0U;
    s_frame_length = 0U;
    for (index = 0U; index < UART_PROTOCOL_MAX_PAYLOAD; ++index)
    {
        s_parser_payload[index] = 0U;
        s_frame_payload[index] = 0U;
    }
}

void uart_protocol_timeout_scan(void)
{
    uint32_t primask;

    if ((s_parser_state != UART_PROTOCOL_STATE_WAIT_HEADER_1) &&
        ((uint32_t)(HAL_GetTick() - s_parser_last_tick) >=
         UART_PROTOCOL_FRAME_TIMEOUT_MS))
    {
        primask = __get_PRIMASK();
        __disable_irq();
        uart_protocol_parser_reset();
        if (primask == 0U)
        {
            __enable_irq();
        }
    }
}

int32_t uart_protocol_send(uint8_t cmd, const uint8_t *payload,
                           uint8_t payload_length)
{
    uint8_t frame[5U + UART_PROTOCOL_MAX_PAYLOAD];
    uint8_t index;
    uint8_t checksum;
    uint16_t frame_length;

    if (payload_length > UART_PROTOCOL_MAX_PAYLOAD)
    {
        return -1;
    }
    if ((payload_length > 0U) && (payload == 0))
    {
        return -1;
    }

    /* 新事务开始前清除半帧解析状态，避免拼接上一事务的残留数据。 */
    uart_protocol_parser_reset();
    frame[0] = UART_PROTOCOL_HEADER_1;
    frame[1] = UART_PROTOCOL_HEADER_2;
    frame[2] = payload_length;
    frame[3] = cmd;
    for (index = 0U; index < payload_length; ++index)
    {
        frame[4U + index] = payload[index];
    }
    checksum = uart_protocol_checksum(payload_length, cmd, payload);
    frame[4U + payload_length] = checksum;
    frame_length = (uint16_t)(5U + payload_length);

    return uart_halfduplex_send(frame, frame_length,
                                UART_HALF_DUPLEX_TIMEOUT_MS);
}

uint8_t uart_protocol_frame_available(void)
{
    return s_frame_ready;
}

void uart_protocol_discard_frame(void)
{
    uint32_t primask = __get_PRIMASK();

    /* 防止清除邮箱时接收中断同时写入完整帧。 */
    __disable_irq();
    s_frame_ready = 0U;
    uart_protocol_parser_reset();
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t uart_protocol_get_frame(uint8_t *cmd, uint8_t *payload,
                                uint8_t *payload_length)
{
    uint8_t index;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if ((cmd == 0) || (payload_length == 0) ||
        (s_frame_ready == 0U) ||
        ((s_frame_length > 0U) && (payload == 0)))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }
    *cmd = s_frame_command;
    *payload_length = s_frame_length;
    for (index = 0U; index < s_frame_length; ++index)
    {
        payload[index] = s_frame_payload[index];
    }
    s_frame_ready = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return 1U;
}
