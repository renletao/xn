/**
  * @file    charge.h
  * @brief   充电策略管理和电池电量转换接口。
  *
  * 本模块位于 USBIN、IP2326 和 ADC 驱动之上，只负责决定当前是否允许充电。
  * PA5 的 USB 插入检测仍由 usbin.c 完成，PA2 的芯片使能仍由 ip2326.c 完成。
  *
  * 电池电量百分比与电池化学体系、满放电压、负载电流和温度有关，因此这里
  * 只预留转换接口，不假定某一种电池曲线。后续可通过 charge_set_battery_converter()
  * 注入实际的查表或分段线性算法。
  */

#ifndef __U1_CHARGE_H
#define __U1_CHARGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 电量转换尚未提供有效结果时返回的特殊值。有效百分比范围为 0~100。 */
#define CHARGE_BATTERY_PERCENT_UNKNOWN  0xFFU

/** 充电管理的公开状态。 */
typedef enum
{
  CHARGE_STATE_IDLE = 0,       /* 未插入 USB，充电输出关闭。 */
  CHARGE_STATE_CHARGING,       /* USB 已插入、允许充电且 PA4 电流小于 0。 */
  CHARGE_STATE_DISCHARGING,    /* USB 已插入但 PA4 电流大于等于 0。 */
  CHARGE_STATE_DISABLED,       /* USB 已插入，但被软件策略禁止充电。 */
  CHARGE_STATE_FAULT           /* 预留给后续充电故障检测。 */
} ChargeStateTypeDef;

/**
  * 电池电压到百分比的转换函数类型。
  * battery_mv 为 ADC 驱动还原后的电池实际电压，单位 mV。
  */
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

/**
  * 注册电池电压到电量百分比的转换函数。
  * 传入 NULL 可恢复为“未实现”状态，charge_get_battery_percent() 将返回 0xFF。
  */
void charge_set_battery_converter(ChargeBatteryPercentConverter converter);
/** 使用当前注册的转换函数换算电量；未注册转换函数时返回 0xFF。 */
uint8_t charge_battery_mv_to_percent(uint32_t battery_mv);
/** 从 ADC 全局结果读取当前电池电压并换算电量；未实现时返回 0xFF。 */
uint8_t charge_get_battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_CHARGE_H */
