/**
  * @file    usbin.c
  * @brief   USB 插入检测和 IP2326 使能联动驱动。
  *
  * PA5 配置为内部下拉输入，高电平表示 USB 插入。驱动把原始电平先放入
  * 候选状态，连续稳定 USBIN_DEBOUNCE_MS 后才更新稳定状态并控制 PA2。
  */

#include "usbin.h"
#include "ip2326.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"

static uint8_t usb_stable_state;
static uint8_t usb_candidate_state;
static uint32_t usb_candidate_since;

uint8_t usbin_read_raw(void)
{
  /* 读取当前 PA5 电平，并根据硬件有效电平宏转换为统一的逻辑 0/1。 */
  GPIO_PinState level = HAL_GPIO_ReadPin(PIN_USBIN_PORT, PIN_USBIN_PIN);
  GPIO_PinState active = (PIN_USBIN_ACTIVE_LEVEL != 0U) ?
                         GPIO_PIN_SET : GPIO_PIN_RESET;

  return (level == active) ? 1U : 0U;
}

void usbin_init(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* PA5 使用下拉输入，未检测到外部 USB 信号时保持确定的低电平。 */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio_init.Pin = (uint32_t)PIN_USBIN_PIN;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_PULLDOWN;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(PIN_USBIN_PORT, &gpio_init);

  /* 初始化消抖状态：稳定状态默认未插入，候选状态取当前原始电平。 */
  usb_stable_state = 0U;
  usb_candidate_state = usbin_read_raw();
  usb_candidate_since = HAL_GetTick();

  /* 在输入完成稳定确认前，快充芯片保持关闭。 */
  ip2326_disable();
}

void usbin_task(void)
{
  /* 本任务采用“两阶段”消抖：先记录变化时刻，再等待候选电平持续稳定。 */
  uint8_t raw_state = usbin_read_raw();
  uint32_t now = HAL_GetTick();

  if (raw_state != usb_candidate_state)
  {
    /* 检测到新电平，重新开始稳定计时。 */
    usb_candidate_state = raw_state;
    usb_candidate_since = now;
    return;
  }

  if ((usb_candidate_state != usb_stable_state) &&
      ((now - usb_candidate_since) >= USBIN_DEBOUNCE_MS))
  {
    /* 候选电平保持足够时间，提交状态并同步更新 IP2326_EN。 */
    usb_stable_state = usb_candidate_state;
    ip2326_set(usb_stable_state);
  }
}

uint8_t usbin_is_inserted(void)
{
  /* 返回已经过消抖的状态，避免上层直接使用抖动的原始输入。 */
  return usb_stable_state;
}
