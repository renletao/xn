/**
  * @file    uart_protocol_driver.c
  * @brief   与 U1 Display 最新框架一致的 AA55 协议状态机。
  *
  * 接收状态机在 USART 中断回调中运行；校验成功的帧写入单帧邮箱，
  * 主循环通过 uart_command_task() 读取并处理，避免在中断中操作业务 GPIO。
  */

#include "uart_protocol_driver.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "uart_halfduplex_driver.h"

typedef enum
{
  UART_PROTOCOL_STATE_WAIT_HEADER_1 = 0,
  UART_PROTOCOL_STATE_WAIT_HEADER_2,
  UART_PROTOCOL_STATE_READ_LENGTH,
  UART_PROTOCOL_STATE_READ_COMMAND,
  UART_PROTOCOL_STATE_READ_PAYLOAD,
  UART_PROTOCOL_STATE_READ_CHECKSUM
} UartProtocolStateTypeDef;

static UartProtocolStateTypeDef parser_state;
static uint8_t parser_length;
static uint8_t parser_command;
static uint8_t parser_index;
static uint8_t parser_checksum;
static uint8_t parser_payload[UART_PROTOCOL_MAX_PAYLOAD];
static uint32_t parser_last_tick;
static volatile uint8_t frame_ready;
static uint8_t frame_command;
static uint8_t frame_length;
static uint8_t frame_payload[UART_PROTOCOL_MAX_PAYLOAD];

static uint8_t protocol_checksum(uint8_t length, uint8_t command,
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

static void protocol_parser_reset(void)
{
  parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_1;
  parser_length = 0U;
  parser_command = 0U;
  parser_index = 0U;
  parser_checksum = 0U;
  parser_last_tick = HAL_GetTick();
}

static void protocol_restart_from_byte(uint8_t byte)
{
  parser_state = (byte == UART_PROTOCOL_HEADER_1) ?
                 UART_PROTOCOL_STATE_WAIT_HEADER_2 :
                 UART_PROTOCOL_STATE_WAIT_HEADER_1;
  parser_length = 0U;
  parser_command = 0U;
  parser_index = 0U;
  parser_checksum = 0U;
  parser_last_tick = HAL_GetTick();
}

void uart_protocol_irq_feed_byte(uint8_t byte)
{
  switch (parser_state)
  {
    case UART_PROTOCOL_STATE_WAIT_HEADER_1:
      if (byte == UART_PROTOCOL_HEADER_1)
      {
        parser_state = UART_PROTOCOL_STATE_WAIT_HEADER_2;
      }
      break;

    case UART_PROTOCOL_STATE_WAIT_HEADER_2:
      if (byte == UART_PROTOCOL_HEADER_2)
      {
        parser_state = UART_PROTOCOL_STATE_READ_LENGTH;
      }
      else
      {
        protocol_restart_from_byte(byte);
      }
      break;

    case UART_PROTOCOL_STATE_READ_LENGTH:
      if (byte > UART_PROTOCOL_MAX_PAYLOAD)
      {
        protocol_restart_from_byte(byte);
      }
      else
      {
        parser_length = byte;
        parser_index = 0U;
        parser_checksum = byte;
        parser_state = UART_PROTOCOL_STATE_READ_COMMAND;
      }
      break;

    case UART_PROTOCOL_STATE_READ_COMMAND:
      parser_command = byte;
      parser_checksum = (uint8_t)(parser_checksum + byte);
      parser_state = (parser_length == 0U) ?
                     UART_PROTOCOL_STATE_READ_CHECKSUM :
                     UART_PROTOCOL_STATE_READ_PAYLOAD;
      break;

    case UART_PROTOCOL_STATE_READ_PAYLOAD:
      parser_payload[parser_index] = byte;
      parser_checksum = (uint8_t)(parser_checksum + byte);
      ++parser_index;
      if (parser_index >= parser_length)
      {
        parser_state = UART_PROTOCOL_STATE_READ_CHECKSUM;
      }
      break;

    case UART_PROTOCOL_STATE_READ_CHECKSUM:
      if (byte == parser_checksum)
      {
        /* 邮箱已有旧帧时保留旧帧，防止尚未处理的数据被覆盖。 */
        if (frame_ready == 0U)
        {
          uint8_t index;
          frame_command = parser_command;
          frame_length = parser_length;
          for (index = 0U; index < parser_length; ++index)
          {
            frame_payload[index] = parser_payload[index];
          }
          frame_ready = 1U;
        }
      }
      protocol_restart_from_byte(0U);
      break;

    default:
      protocol_parser_reset();
      break;
  }

  parser_last_tick = HAL_GetTick();
}

void uart_protocol_init(void)
{
  uint8_t index;

  protocol_parser_reset();
  frame_ready = 0U;
  frame_command = 0U;
  frame_length = 0U;
  for (index = 0U; index < UART_PROTOCOL_MAX_PAYLOAD; ++index)
  {
    parser_payload[index] = 0U;
    frame_payload[index] = 0U;
  }
}

void uart_protocol_timeout_scan(void)
{
  uint32_t primask;

  if ((parser_state != UART_PROTOCOL_STATE_WAIT_HEADER_1) &&
      ((uint32_t)(HAL_GetTick() - parser_last_tick) >=
       UART_PROTOCOL_FRAME_TIMEOUT_MS))
  {
    primask = __get_PRIMASK();
    __disable_irq();
    protocol_parser_reset();
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
  uint16_t frame_length;

  if ((payload_length > UART_PROTOCOL_MAX_PAYLOAD) ||
      ((payload_length > 0U) && (payload == 0)))
  {
    return -1;
  }

  frame[0] = UART_PROTOCOL_HEADER_1;
  frame[1] = UART_PROTOCOL_HEADER_2;
  frame[2] = payload_length;
  frame[3] = cmd;
  for (index = 0U; index < payload_length; ++index)
  {
    frame[4U + index] = payload[index];
  }
  frame[4U + payload_length] = protocol_checksum(payload_length, cmd, payload);
  frame_length = (uint16_t)(payload_length + 5U);

  return uart_halfduplex_send(frame, frame_length,
                              UART_HALF_DUPLEX_TIMEOUT_MS);
}

uint8_t uart_protocol_frame_available(void)
{
  return frame_ready;
}

void uart_protocol_discard_frame(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  frame_ready = 0U;
  protocol_parser_reset();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

uint8_t uart_protocol_get_frame(uint8_t *cmd, uint8_t *payload,
                                uint8_t *payload_length)
{
  uint8_t index;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if ((cmd == 0) || (payload_length == 0U) || (frame_ready == 0U) ||
      ((frame_length > 0U) && (payload == 0)))
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return 0U;
  }

  *cmd = frame_command;
  *payload_length = frame_length;
  for (index = 0U; index < frame_length; ++index)
  {
    payload[index] = frame_payload[index];
  }
  frame_ready = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return 1U;
}
