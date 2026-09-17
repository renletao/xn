/**
  * @file    charge.c
  * @brief   充电策略管理和电池电量转换接口实现。
  *
  * 本文件只调用已有的 usbin、ip2326、LED 和 ADC 应用驱动，不直接访问 GPIO。
  * 这样 USB 输入检测、快充芯片使能和状态灯的硬件细节仍分别由各自模块维护。
  */

#include "charge.h"
#include "adc.h"
#include "ip2326.h"
#include "led.h"
#include "usbin.h"

/* 软件策略状态：1 表示允许充电，0 表示禁止充电。默认允许。 */
static uint8_t charge_allowed;

/* 上一次已经写入硬件的输出状态，0xFF 表示尚未同步。 */
static uint8_t charge_output_state;

/* 最近一次根据 USB 和软件策略计算出的公开状态。 */
static ChargeStateTypeDef charge_state;

/* 用户后续注册的电量转换回调；NULL 表示实际算法尚未提供。 */
static ChargeBatteryPercentConverter battery_converter;

/**
  * 根据当前策略和 USB 状态同步 IP2326_EN、IP2326_LED。
  *
  * 只有“USB 已插入且软件允许充电”时才打开输出；任何其他情况都关闭。
  * 通过缓存避免每次主循环重复写 GPIO，同时仍能修正 usbin_task() 的初始状态。
  */
static void charge_apply_output(uint8_t usb_inserted)
{
  uint8_t should_charge = ((usb_inserted != 0U) &&
                           (charge_allowed != 0U)) ? 1U : 0U;

  if (should_charge != charge_output_state)
  {
    charge_output_state = should_charge;
    ip2326_set(should_charge);
    ip2326_led_set(should_charge);
  }
}

/** 根据输入状态和软件策略更新公开状态枚举。 */
static void charge_update_state(uint8_t usb_inserted)
{
  if (usb_inserted == 0U)
  {
    charge_state = CHARGE_STATE_IDLE;
  }
  else if (charge_allowed != 0U)
  {
    /* PA4 负电流表示电流流入电池，即正在充电。 */
    if (g_adc_data.current_ma < 0)
    {
      charge_state = CHARGE_STATE_CHARGING;
    }
    else
    {
      /* PA4 零电流及正电流统一按放电状态处理。 */
      charge_state = CHARGE_STATE_DISCHARGING;
    }
  }
  else
  {
    charge_state = CHARGE_STATE_DISABLED;
  }
}

void charge_init(void)
{
  /* 默认打开“允许充电”策略，但 USB 未插入时仍不会开启实际充电输出。 */
  charge_allowed = 1U;
  charge_output_state = 0xFFU;
  charge_state = CHARGE_STATE_IDLE;
  battery_converter = (ChargeBatteryPercentConverter)0;

  /* 初始化阶段强制关闭状态灯，避免上电时沿用未知的 GPIO 电平。 */
  ip2326_set(0U);
  ip2326_led_off();
}

void charge_task(void)
{
  /* usbin_task() 必须先运行，下面读取的状态才是最新的消抖结果。 */
  uint8_t usb_inserted = usbin_is_inserted();

  charge_apply_output(usb_inserted);
  charge_update_state(usb_inserted);
}

void charge_enable(void)
{
  uint8_t usb_inserted = usbin_is_inserted();

  /* 先改变策略，再立即按照当前消抖状态同步输出，避免调用后继续保持禁充。 */
  charge_allowed = 1U;
  charge_apply_output(usb_inserted);
  charge_update_state(usb_inserted);
}

void charge_disable(void)
{
  uint8_t usb_inserted;

  /* 禁止充电后立即关断输出，避免等待主循环下一次调度造成短暂继续充电。 */
  charge_allowed = 0U;
  charge_apply_output(0U);
  usb_inserted = usbin_is_inserted();
  charge_update_state(usb_inserted);
}

uint8_t charge_is_enabled(void)
{
  return charge_allowed;
}

uint8_t charge_is_usb_inserted(void)
{
  return usbin_is_inserted();
}

ChargeStateTypeDef charge_get_state(void)
{
  return charge_state;
}

void charge_set_battery_converter(ChargeBatteryPercentConverter converter)
{
  /* 允许传入 NULL，以便恢复为未实现状态并明确提示上层尚未配置曲线。 */
  battery_converter = converter;
}

uint8_t charge_battery_mv_to_percent(uint32_t battery_mv)
{
  if (battery_converter == (ChargeBatteryPercentConverter)0)
  {
    /* 未提供电池曲线时不能可靠推算电量，返回约定的未知值。 */
    (void)battery_mv;
    return CHARGE_BATTERY_PERCENT_UNKNOWN;
  }

  return battery_converter(battery_mv);
}

uint8_t charge_get_battery_percent(void)
{
  /* g_adc_data.battery_mv 已由 adc_task_100ms() 换算为实际电池电压。 */
  return charge_battery_mv_to_percent(g_adc_data.battery_mv);
}
