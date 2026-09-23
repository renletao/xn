/**
  * @file    uart_command_driver.c
  * @brief   U2 命令分发：处理 U1 的 CMD=0x01~0x04 并控制压力泵。
  */

#include "uart_command_driver.h"
#include "charge.h"
#include "adc.h"
#include "led.h"
#include "moto.h"
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"
#include "uart_halfduplex_driver.h"
#include "uart_protocol_driver.h"
#include "wf183d.h"
#include "power_manager.h"

/* 兼容保留的模拟气压变量；WF183D_USE_REAL_SENSOR=1 时不参与返回。 */
volatile uint32_t g_uart_simulated_pressure_pa = 101325U;
volatile uint32_t g_uart_pump_target_pressure_pa;
volatile uint8_t g_uart_pump_mode = UART_COMMAND_MODE_RAFT;
volatile uint8_t g_uart_pump_running;

#define UART_COMMAND_PUMP_START_DUTY             MOTO_PUMP_START_DUTY
#define UART_COMMAND_PUMP_KICK_PHASE_COUNT       4U
#define UART_COMMAND_PUMP_NORMAL_KICK_TOTAL_MS   23U
#define UART_COMMAND_PUMP_SUP_HIGH_KICK_TOTAL_MS 256U
#define UART_COMMAND_PUMP_SUP_SWITCH_PAUSE_MS    1000U

typedef enum
{
  PUMP_CONTROL_IDLE = 0,
  PUMP_CONTROL_HIGH_KICK,
  PUMP_CONTROL_HIGH_RUN,
  PUMP_CONTROL_LOW_KICK,
  PUMP_CONTROL_LOW_RUN,
  PUMP_CONTROL_SUP_SWITCH_WAIT,
  PUMP_CONTROL_SUP_HIGH_KICK,
  PUMP_CONTROL_SUP_HIGH_RUN
} PumpControlState_t;

static PumpControlState_t s_pump_control_state;
static uint8_t s_pump_kick_phase;
static uint32_t s_pump_control_deadline;

static void uart_command_pump_reset_state(void)
{
  s_pump_control_state = PUMP_CONTROL_IDLE;
  s_pump_kick_phase = 0U;
  s_pump_control_deadline = 0U;
}

static void uart_command_pump_set_outputs(uint16_t low_duty,
                                           uint16_t high_duty)
{
  moto_set_moto1_pwm(low_duty);
  moto_set_moto2_pwm(high_duty);
}

static void uart_command_pump_stop(void)
{
  uart_command_pump_reset_state();
  moto_stop();
}

static uint8_t uart_command_pump_uses_low_pressure(uint8_t mode)
{
  /* 当前协议的皮筏艇模式对应“低压泵后切高压泵”的 SUP/气垫流程。 */
  return (mode == UART_COMMAND_MODE_RAFT) ? 1U : 0U;
}

static uint32_t uart_command_pump_kick_phase_ms(uint8_t high_kick,
                                                 uint8_t phase)
{
  uint32_t total_ms = (high_kick != 0U) ?
                      UART_COMMAND_PUMP_SUP_HIGH_KICK_TOTAL_MS :
                      UART_COMMAND_PUMP_NORMAL_KICK_TOTAL_MS;
  uint32_t base_ms = total_ms / UART_COMMAND_PUMP_KICK_PHASE_COUNT;
  uint32_t remainder_ms = total_ms % UART_COMMAND_PUMP_KICK_PHASE_COUNT;

  /* 将余数分配到前几个阶段，保证所有阶段之和严格等于总时长。 */
  return base_ms + ((phase < remainder_ms) ? 1U : 0U);
}

static void uart_command_pump_apply_kick_phase(uint8_t high_pressure,
                                                uint8_t phase)
{
  uint16_t duty = (phase < (UART_COMMAND_PUMP_KICK_PHASE_COUNT - 1U)) ?
                  UART_COMMAND_PUMP_START_DUTY : 0U;

  if (high_pressure != 0U)
  {
    uart_command_pump_set_outputs(0U, duty);
  }
  else
  {
    uart_command_pump_set_outputs(duty, 0U);
  }
}

static void uart_command_pump_start_kick(PumpControlState_t state,
                                          uint8_t high_pressure,
                                          uint8_t high_kick,
                                          uint32_t now)
{
  s_pump_control_state = state;
  s_pump_kick_phase = 0U;
  s_pump_control_deadline = now +
                            uart_command_pump_kick_phase_ms(high_kick, 0U);
  uart_command_pump_apply_kick_phase(high_pressure, 0U);
}

static void uart_command_pump_advance_kick(uint8_t high_pressure,
                                            uint8_t high_kick,
                                            uint32_t now)
{
  /* deadline 使用绝对时序；任务偶发延迟时补齐已到期的阶段，避免拉长软启动。 */
  while ((int32_t)(now - s_pump_control_deadline) >= 0)
  {
    if (s_pump_kick_phase < (UART_COMMAND_PUMP_KICK_PHASE_COUNT - 1U))
    {
      ++s_pump_kick_phase;
      s_pump_control_deadline +=
        uart_command_pump_kick_phase_ms(high_kick, s_pump_kick_phase);
      uart_command_pump_apply_kick_phase(high_pressure, s_pump_kick_phase);
      continue;
    }

    /* 预启动脉冲完成后进入持续全开状态。 */
    if (high_pressure != 0U)
    {
      uart_command_pump_set_outputs(0U, MOTO_PWM_MAX);
    }
    else
    {
      uart_command_pump_set_outputs(MOTO_PWM_MAX, 0U);
    }

    if (high_kick != 0U)
    {
      s_pump_control_state = PUMP_CONTROL_SUP_HIGH_RUN;
    }
    else if (high_pressure != 0U)
    {
      s_pump_control_state = PUMP_CONTROL_HIGH_RUN;
    }
    else
    {
      s_pump_control_state = PUMP_CONTROL_LOW_RUN;
    }
    return;
  }
}

static void uart_command_enforce_motor_protection(void)
{
  if (charge_is_motor_start_allowed() == 0U)
  {
    g_uart_pump_running = 0U;
    uart_command_pump_stop();
  }
}

static uint32_t uart_command_decode_u32_le(const uint8_t *data)
{
  return ((uint32_t)data[0]) |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

void uart_command_pump_task(void)
{
  uint32_t pressure_pa;
  uint32_t now = HAL_GetTick();

  if (charge_is_motor_start_allowed() == 0U)
  {
    g_uart_pump_running = 0U;
    uart_command_pump_stop();
    return;
  }

  if (g_uart_pump_running == 0U)
  {
    uart_command_pump_stop();
    return;
  }

  /* Both values include tare, so the target comparison stays in one domain. */
  if (g_wf183d_pressure_pa >= g_uart_pump_target_pressure_pa)
  {
    g_uart_pump_running = 0U;
    uart_command_pump_stop();
    return;
  }

  if (uart_command_pump_uses_low_pressure(g_uart_pump_mode) != 0U)
  {
    pressure_pa = wf183d_get_pressure_pa();

    switch (s_pump_control_state)
    {
      case PUMP_CONTROL_IDLE:
        if (pressure_pa <= UART_COMMAND_PUMP_SWITCH_PRESSURE_PA)
        {
          /* SUP/气垫模式先对低压泵执行 23 ms 预启动脉冲。 */
          uart_command_pump_start_kick(PUMP_CONTROL_LOW_KICK, 0U, 0U,
                                        now);
        }
        else
        {
          /* 已经超过切换压力时跳过低压泵，直接启动高压泵。 */
          uart_command_pump_start_kick(PUMP_CONTROL_SUP_HIGH_KICK, 1U, 1U,
                                        now);
        }
        break;

      case PUMP_CONTROL_LOW_KICK:
        uart_command_pump_advance_kick(0U, 0U, now);
        break;

      case PUMP_CONTROL_LOW_RUN:
        uart_command_pump_set_outputs(MOTO_PWM_MAX, 0U);
        if (pressure_pa > UART_COMMAND_PUMP_SWITCH_PRESSURE_PA)
        {
          /* 切换泵前两路都关闭，并保持 1 s。 */
          uart_command_pump_set_outputs(0U, 0U);
          s_pump_control_state = PUMP_CONTROL_SUP_SWITCH_WAIT;
          s_pump_control_deadline = now +
                                     UART_COMMAND_PUMP_SUP_SWITCH_PAUSE_MS;
        }
        break;

      case PUMP_CONTROL_SUP_SWITCH_WAIT:
        uart_command_pump_set_outputs(0U, 0U);
        if ((int32_t)(now - s_pump_control_deadline) >= 0)
        {
          /* 1 s 停止完成后，对高压泵执行 256 ms 预启动脉冲。 */
          uart_command_pump_start_kick(PUMP_CONTROL_SUP_HIGH_KICK, 1U, 1U,
                                        now);
        }
        break;

      case PUMP_CONTROL_SUP_HIGH_KICK:
        uart_command_pump_advance_kick(1U, 1U, now);
        break;

      case PUMP_CONTROL_SUP_HIGH_RUN:
        uart_command_pump_set_outputs(0U, MOTO_PWM_MAX);
        break;

      default:
        /* 状态异常时回到安全停止，再由下一轮重新选择启动阶段。 */
        uart_command_pump_stop();
        break;
    }
  }
  else
  {
    /* 普通模式只使用高压泵，并先执行 23 ms 预启动脉冲。 */
    switch (s_pump_control_state)
    {
      case PUMP_CONTROL_IDLE:
        uart_command_pump_start_kick(PUMP_CONTROL_HIGH_KICK, 1U, 0U, now);
        break;

      case PUMP_CONTROL_HIGH_KICK:
        uart_command_pump_advance_kick(1U, 0U, now);
        break;

      case PUMP_CONTROL_HIGH_RUN:
        uart_command_pump_set_outputs(0U, MOTO_PWM_MAX);
        break;

      default:
        /* 模式内状态不匹配时先清零，避免两路泵保持未知输出。 */
        uart_command_pump_stop();
        break;
    }
  }
}

void uart_command_init(void)
{
  g_uart_pump_target_pressure_pa = 0U;
  g_uart_pump_mode = UART_COMMAND_MODE_RAFT;
  g_uart_pump_running = 0U;
  uart_command_pump_reset_state();

  /* 先初始化协议状态，再启动底层接收中断，避免中断使用未初始化状态。 */
  uart_protocol_init();
  uart_halfduplex_init();
}

void uart_command_suspend(void)
{
  uart_halfduplex_suspend();
}

void uart_command_resume(void)
{
  uart_command_init();
}

void uart_command_irq_handler(void)
{
  /* HAL 回调会在中断上下文中把收到的字节送入协议状态机。 */
  uart_halfduplex_irq_handler();
}

void uart_command_task(void)
{
  uint8_t command;
  uint8_t length;
  uint8_t payload[UART_PROTOCOL_MAX_PAYLOAD];
  uint8_t response_payload[UART_COMMAND_CHARGING_STATUS_LENGTH];

  /* 主循环负责清理未完成超时帧，不在中断中调用 HAL_GetTick 以外的业务。 */
  uart_protocol_timeout_scan();
  /* 先同步本地保护状态，保证随后返回的泵状态不会滞后一轮。 */
  uart_command_enforce_motor_protection();

  if (uart_protocol_frame_available() == 0U)
  {
    return;
  }

  if (uart_protocol_get_frame(&command, payload, &length) == 0U)
  {
    return;
  }

  if (command == UART_COMMAND_LED_CONTROL)
  {
    /* CMD=0x01 要求 1 字节载荷：0=关灯，1=开灯。 */
    if ((length != 1U) ||
        ((payload[0] != UART_COMMAND_LED_OFF) &&
         (payload[0] != UART_COMMAND_LED_ON)))
    {
      return;
    }

    /* PB3 高电平点亮，led_set() 已封装实际 GPIO 操作。 */
    led_set(payload[0]);
    response_payload[0] = (HAL_GPIO_ReadPin(PIN_LED_PORT, PIN_LED_PIN) ==
                           GPIO_PIN_SET) ? UART_COMMAND_LED_ON :
                                          UART_COMMAND_LED_OFF;

    /* 返回执行后的实际状态，帧格式与 U1 Display 最新协议完全一致。 */
    (void)uart_protocol_send(UART_COMMAND_LED_CONTROL, response_payload, 1U);
    return;
  }

  if ((command == UART_COMMAND_CHARGING_STATUS) && (length == 0U))
  {
    uint8_t battery_level = adc_get_battery_level();
    uint32_t pressure_pa = g_uart_simulated_pressure_pa;

#if WF183D_USE_REAL_SENSOR
    /* 获取时由驱动统一减去上电去皮值，并将负值钳制为 0。 */
    pressure_pa = wf183d_get_pressure_pa();
#endif

    /* 充电状态使用实时策略状态；只有明确处于 CHARGING 才返回 1。 */
    response_payload[0] = (charge_get_state() == CHARGE_STATE_CHARGING) ?
                           UART_COMMAND_CHARGING_ON :
                           UART_COMMAND_CHARGING_OFF;

    /* Display 约定 BatLevel 范围为 0~3，0 为满电、3 为低电。 */
    if (battery_level > UART_COMMAND_BATTERY_MAX_LEVEL)
    {
      battery_level = UART_COMMAND_BATTERY_MAX_LEVEL;
    }
    response_payload[1] = battery_level;

    /* 气压按 Display 约定使用 32 位小端 Pa 值。 */
    response_payload[2] = (uint8_t)(pressure_pa & 0xFFU);
    response_payload[3] = (uint8_t)((pressure_pa >> 8U) & 0xFFU);
    response_payload[4] = (uint8_t)((pressure_pa >> 16U) & 0xFFU);
    response_payload[5] = (uint8_t)((pressure_pa >> 24U) & 0xFFU);
    /* 返回当前压力泵开关状态：1=运行，0=停止/暂停。 */
    response_payload[6] = (g_uart_pump_running != 0U) ?
                          UART_COMMAND_PUMP_RUNNING :
                          UART_COMMAND_PUMP_STOPPED;

    (void)uart_protocol_send(UART_COMMAND_CHARGING_STATUS,
                             response_payload,
                             UART_COMMAND_CHARGING_STATUS_LENGTH);
    return;
  }

  if (command == UART_COMMAND_PUMP_START)
  {
    uint8_t status = UART_COMMAND_PUMP_ERROR;

    if (length == UART_COMMAND_PUMP_PAYLOAD_LENGTH)
    {
      uint32_t target_pressure_pa = uart_command_decode_u32_le(payload);
      uint8_t mode = payload[4];

      if ((charge_is_motor_start_allowed() != 0U) &&
          (target_pressure_pa > 0U) &&
          (target_pressure_pa <= UART_COMMAND_PUMP_MAX_PRESSURE_PA) &&
          (mode <= UART_COMMAND_MODE_TIRE) &&
          (wf183d_is_tare_ready() != 0U))
      {
        /* Setting a pressure adds tare before it enters the control domain. */
        g_uart_pump_target_pressure_pa =
          wf183d_add_tare_pa(target_pressure_pa);
        g_uart_pump_mode = mode;
        g_uart_pump_running = 1U;
        /* 每次有效启动命令都从对应泵的软启动阶段重新开始。 */
        uart_command_pump_reset_state();
        uart_command_pump_task();
        status = (g_uart_pump_running != 0U) ?
                 UART_COMMAND_PUMP_RUNNING : UART_COMMAND_PUMP_STOPPED;
      }
    }

    response_payload[0] = status;
    (void)uart_protocol_send(UART_COMMAND_PUMP_START, response_payload,
                             UART_COMMAND_PUMP_RESPONSE_LENGTH);
    return;
  }

  if (command == UART_COMMAND_PUMP_PAUSE)
  {
    uint8_t status = UART_COMMAND_PUMP_ERROR;

    if (length == 0U)
    {
      g_uart_pump_running = 0U;
      uart_command_pump_stop();
      status = UART_COMMAND_PUMP_STOPPED;
    }

    response_payload[0] = status;
    (void)uart_protocol_send(UART_COMMAND_PUMP_PAUSE, response_payload,
                             UART_COMMAND_PUMP_RESPONSE_LENGTH);
    return;
  }

  if ((command == UART_COMMAND_SLEEP) && (length == 0U))
  {
    uint8_t status = UART_COMMAND_SLEEP_BUSY;

    /* Reply first; U1 must receive a complete ACK before U2 stops UART. */
    if (power_manager_can_sleep() != 0U)
    {
      status = UART_COMMAND_SLEEP_READY;
    }
    response_payload[0] = status;
    if (uart_protocol_send(UART_COMMAND_SLEEP, response_payload, 1U) >= 0)
    {
      if (status == UART_COMMAND_SLEEP_READY)
      {
        power_manager_commit_sleep();
      }
    }
  }
}
