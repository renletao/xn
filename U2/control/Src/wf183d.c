/**
  * @file    wf183d.c
  * @brief   WF183D pressure sensor driver over the PB4/PB5 software UART.
  */

#include "wf183d.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "soft_uart.h"

#define WF183D_COMMAND_LENGTH            4U
#define WF183D_TEMPERATURE_FRAME_LENGTH  6U
#define WF183D_PRESSURE_FRAME_LENGTH     8U
#define WF183D_FRAME_BUFFER_LENGTH       8U

volatile uint32_t g_wf183d_pressure_pa;
volatile uint32_t g_wf183d_pressure_tare_pa;
volatile uint8_t  g_wf183d_state = WF183D_STATE_TIMEOUT;
volatile uint8_t  g_wf183d_fault_count;

static uint32_t wf183d_next_sample_tick;
static uint8_t wf183d_started;
static uint8_t wf183d_tare_sample_count;
static uint64_t wf183d_tare_sample_sum;
static uint32_t wf183d_tare_sample_min;
static uint32_t wf183d_tare_sample_max;

static void wf183d_update_tare(uint32_t pressure)
{
  if (wf183d_tare_sample_count == 0U)
  {
    wf183d_tare_sample_min = pressure;
    wf183d_tare_sample_max = pressure;
  }
  else
  {
    if (pressure < wf183d_tare_sample_min)
    {
      wf183d_tare_sample_min = pressure;
    }
    if (pressure > wf183d_tare_sample_max)
    {
      wf183d_tare_sample_max = pressure;
    }
  }

  wf183d_tare_sample_sum += pressure;
  ++wf183d_tare_sample_count;

  if (wf183d_tare_sample_count == WF183D_TARE_SAMPLE_COUNT)
  {
    /* Ten samples are guaranteed here, so two extrema can be discarded. */
    g_wf183d_pressure_tare_pa =
      (wf183d_tare_sample_sum - wf183d_tare_sample_min -
       wf183d_tare_sample_max) / (WF183D_TARE_SAMPLE_COUNT - 2U);
  }
}

static uint8_t wf183d_crc8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0U;
  uint8_t bit;

  while (length-- != 0U)
  {
    crc ^= *data++;
    for (bit = 0U; bit < 8U; ++bit)
    {
      crc = (crc & 0x01U) ? (uint8_t)((crc >> 1U) ^ 0x8CU)
                           : (uint8_t)(crc >> 1U);
    }
  }

  return crc;
}

static uint8_t wf183d_elapsed_ms(uint32_t start_tick, uint32_t *remaining)
{
  uint32_t elapsed = HAL_GetTick() - start_tick;

  if (elapsed >= WF183D_RESPONSE_TIMEOUT_MS)
  {
    *remaining = 0U;
    return 0U;
  }

  *remaining = WF183D_RESPONSE_TIMEOUT_MS - elapsed;
  return 1U;
}

static uint8_t wf183d_send_command(uint8_t command, uint32_t *start_tick)
{
  uint8_t frame[WF183D_COMMAND_LENGTH] = {
    WF183D_CMD_HEAD, WF183D_COMMAND_LENGTH, command, 0U
  };

  *start_tick = HAL_GetTick();
  frame[3] = wf183d_crc8(frame, 3U);

  return (soft_uart_send(frame, WF183D_COMMAND_LENGTH,
                         WF183D_RESPONSE_TIMEOUT_MS) ==
          (int32_t)WF183D_COMMAND_LENGTH) ? 1U : 0U;
}

static uint8_t wf183d_receive_frame(uint8_t *frame, uint8_t capacity,
                                    uint32_t start_tick)
{
  uint32_t remaining;
  int32_t received;
  uint8_t length;

  if (wf183d_elapsed_ms(start_tick, &remaining) == 0U)
  {
    return 0U;
  }

  received = soft_uart_receive(frame, 2U, remaining);
  if (received != 2)
  {
    return 0U;
  }

  if ((frame[0] != WF183D_RET_HEAD) ||
      (frame[1] < 4U) || (frame[1] > capacity))
  {
    return 0U;
  }

  length = frame[1];
  if (wf183d_elapsed_ms(start_tick, &remaining) == 0U)
  {
    return 0U;
  }

  received = soft_uart_receive(&frame[2], (uint16_t)(length - 2U),
                               remaining);
  return (received == (int32_t)(length - 2U)) ? 1U : 0U;
}

static uint8_t wf183d_transaction(uint8_t command, uint8_t *frame)
{
  uint32_t start_tick;

  if (wf183d_send_command(command, &start_tick) == 0U)
  {
    return 0U;
  }

  return wf183d_receive_frame(frame, WF183D_FRAME_BUFFER_LENGTH, start_tick);
}

static uint8_t wf183d_validate_frame(const uint8_t *frame, uint8_t length,
                                     uint8_t type)
{
  if ((frame[0] != WF183D_RET_HEAD) || (frame[1] != length) ||
      (frame[2] != type))
  {
    return 0U;
  }

  return (wf183d_crc8(frame, (uint8_t)(length - 1U)) ==
          frame[length - 1U]) ? 1U : 0U;
}

static void wf183d_record_failure(WF183D_StateTypeDef state)
{
  g_wf183d_state = (uint8_t)state;
  if (g_wf183d_fault_count != 0xFFU)
  {
    ++g_wf183d_fault_count;
  }
}

static uint8_t wf183d_read_pressure(uint32_t *pressure)
{
  uint8_t frame[WF183D_FRAME_BUFFER_LENGTH];

  /* The temperature conversion is required for pressure compensation. */
  if (wf183d_transaction(WF183D_CMD_TEMPERATURE, frame) == 0U)
  {
    wf183d_record_failure(WF183D_STATE_TIMEOUT);
    return 0U;
  }
  if (wf183d_validate_frame(frame, WF183D_TEMPERATURE_FRAME_LENGTH,
                            WF183D_RET_TEMPERATURE) == 0U)
  {
    wf183d_record_failure(WF183D_STATE_CRC_ERROR);
    return 0U;
  }

  if (wf183d_transaction(WF183D_CMD_PRESSURE, frame) == 0U)
  {
    wf183d_record_failure(WF183D_STATE_TIMEOUT);
    return 0U;
  }
  if (wf183d_validate_frame(frame, WF183D_PRESSURE_FRAME_LENGTH,
                            WF183D_RET_PRESSURE) == 0U)
  {
    wf183d_record_failure(WF183D_STATE_CRC_ERROR);
    return 0U;
  }

  *pressure = ((uint32_t)frame[3]) |
              ((uint32_t)frame[4] << 8U) |
              ((uint32_t)frame[5] << 16U) |
              ((uint32_t)frame[6] << 24U);
  return 1U;
}

void wf183d_init(void)
{
  g_wf183d_pressure_pa = 0U;
  g_wf183d_pressure_tare_pa = 0U;
  g_wf183d_state = WF183D_STATE_TIMEOUT;
  g_wf183d_fault_count = 0U;
  wf183d_next_sample_tick = HAL_GetTick();
  wf183d_started = 0U;
  wf183d_tare_sample_count = 0U;
  wf183d_tare_sample_sum = 0ULL;
  wf183d_tare_sample_min = 0U;
  wf183d_tare_sample_max = 0U;
}

void wf183d_startup_tare(void)
{
  uint32_t pressure;

  /* Retry until ten valid temperature/pressure transactions are collected. */
  while (wf183d_tare_sample_count < WF183D_TARE_SAMPLE_COUNT)
  {
    if (wf183d_read_pressure(&pressure) == 0U)
    {
      continue;
    }

    wf183d_update_tare(pressure);
  }

  /* Do not expose a raw sample as the first post-boot display value. */
  g_wf183d_pressure_pa = 0U;
  g_wf183d_state = WF183D_STATE_OK;
  g_wf183d_fault_count = 0U;
}

void wf183d_set_pressure_pa(uint32_t pressure_pa)
{
  g_wf183d_pressure_pa = wf183d_add_tare_pa(pressure_pa);
}

uint32_t wf183d_add_tare_pa(uint32_t pressure_pa)
{
  /* Saturate the addition so a large caller value cannot wrap around. */
  if (pressure_pa > (0xFFFFFFFFUL - g_wf183d_pressure_tare_pa))
  {
    return 0xFFFFFFFFUL;
  }

  return pressure_pa + g_wf183d_pressure_tare_pa;
}

uint8_t wf183d_is_tare_ready(void)
{
  return (wf183d_tare_sample_count >= WF183D_TARE_SAMPLE_COUNT) ? 1U : 0U;
}

uint32_t wf183d_get_pressure_pa(void)
{
  uint32_t pressure_pa;

  /* No compensated value is valid until all ten tare samples are ready. */
  if (wf183d_tare_sample_count < WF183D_TARE_SAMPLE_COUNT)
  {
    return 0U;
  }

  pressure_pa = g_wf183d_pressure_pa;
  if (pressure_pa <= g_wf183d_pressure_tare_pa)
  {
    return 0U;
  }

  return pressure_pa - g_wf183d_pressure_tare_pa;
}

void wf183d_task(void)
{
  uint32_t now;
  uint32_t pressure;

  now = HAL_GetTick();
  if (wf183d_started == 0U)
  {
    wf183d_started = 1U;
    wf183d_next_sample_tick = now;
  }

  if ((int32_t)(now - wf183d_next_sample_tick) < 0)
  {
    return;
  }
  wf183d_next_sample_tick = now + WF183D_SAMPLE_PERIOD_MS;

  if (wf183d_read_pressure(&pressure) == 0U)
  {
    return;
  }

  if (wf183d_tare_sample_count < WF183D_TARE_SAMPLE_COUNT)
  {
    /* This is only reachable if normal sampling is called before startup tare. */
    wf183d_update_tare(pressure);
    g_wf183d_pressure_pa = 0U;
  }
  else
  {
    /* Store the sensor-domain value; the public getter removes tare. */
    g_wf183d_pressure_pa = pressure;
  }
  g_wf183d_state = WF183D_STATE_OK;
  g_wf183d_fault_count = 0U;
}
