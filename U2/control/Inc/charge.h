/**
  * @file    charge.h
  * @brief   充电策略管理和电池电量转换接口。
  *
  * 本模块位于 USBIN、IP2326 和 ADC 驱动之上，负责充电状态、电机保护和低电保护。
  * PA5 的 USB 插入检测仍由 usbin.c 完成，PA2 的芯片使能仍由 ip2326.c 完成。
  *
  * 电量由 adc.c 按 ADC 原始码分档得到，不使用百分比换算。
  */

#ifndef __U1_CHARGE_H
#define __U1_CHARGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 保留旧百分比接口时使用的未知值；当前 CMD=0x02 不再发送百分比。 */
#define CHARGE_BATTERY_PERCENT_UNKNOWN  0xFFU
#define CHARGE_TIMEOUT_STEP_MS          1024U
#define CHARGE_TIMEOUT_MAX_TICKS        21600U

/** 充电管理的公开状态。 */
typedef enum
{
  CHARGE_STATE_IDLE = 0,       /* 未检测到充电。 */
  CHARGE_STATE_CHARGING,       /* USB 已插入、允许充电且 PA4 电流小于 0。 */
  CHARGE_STATE_DISCHARGING,    /* USB 已插入但 PA4 电流大于等于 0。 */
  CHARGE_STATE_DISABLED,       /* USB 已插入，但被软件策略禁止充电。 */
  CHARGE_STATE_FAULT           /* 预留给后续充电故障检测。 */
} ChargeStateTypeDef;

/** 兼容旧扩展接口的电池电压转换函数类型；当前电量协议不使用百分比。 */
typedef uint8_t (*ChargeBatteryPercentConverter)(uint32_t battery_mv);

/** 初始化充电管理，默认允许充电，但实际输出仍需等待 USB 插入确认。 */
void charge_init(void);
/** 充电策略周期任务，应在主循环中持续调用。 */
void charge_task(void);

/** 允许充电；若 USB 已插入，会立即开启 IP2326。 */
void charge_enable(void);
/** 禁止充电，并立即关闭 IP2326_EN 和 IP2326_LED。 */
void charge_disable(void);
/** 返回软件策略是否允许充电，返回 1 表示允许。 */
uint8_t charge_is_enabled(void);
/** 返回消抖后的 USB 插入状态，返回 1 表示已插入。 */
uint8_t charge_is_usb_inserted(void);
/** 返回最近一次 charge_task() 更新出的充电状态。 */
ChargeStateTypeDef charge_get_state(void);
/** USB 或低电保护有效时返回 0，电机不得启动。 */
uint8_t charge_is_motor_start_allowed(void);
/** 返回低电保护锁定状态。 */
uint8_t charge_is_low_protection_active(void);

/** 注册兼容旧接口的电池电压转换函数；传入 NULL 表示未实现。 */
void charge_set_battery_converter(ChargeBatteryPercentConverter converter);
/** 使用旧转换接口读取电量；未注册时返回 0xFF，CMD=0x02 不调用此接口。 */
uint8_t charge_battery_mv_to_percent(uint32_t battery_mv);
/** 兼容旧接口；当前协议电量由 adc_get_battery_level() 提供。 */
uint8_t charge_get_battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_CHARGE_H */
