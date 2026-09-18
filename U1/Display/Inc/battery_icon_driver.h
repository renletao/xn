#ifndef BATTERY_ICON_DRIVER_H
#define BATTERY_ICON_DRIVER_H

#include <stdint.h>

/* 电池图标使用 Q8/P15（PC0）作为公共端扫描线。
   要正常显示，Option Byte 必须将 PC0 配置为普通 GPIO。 */
#define BATTERY_ICON_MAX_LEVEL       3U   /* 电量格数上限。 */
#define BATTERY_ICON_ANIMATION_MS    500U /* 充电动画每一帧持续约 500 ms。 */

/* 初始化电池图标状态，默认电量 0 格、非充电。 */
void battery_icon_init(void);
/* 主循环周期调用，推进未满电时的充电动画。 */
void battery_icon_scan(void);
/* 设置电量格数，输入范围为 0~3，超出范围会被限制。 */
void battery_icon_set_level(uint8_t level);
/* 获取业务设置的实际电量格数。 */
uint8_t battery_icon_get_level(void);
/* 设置充电状态：非 0 表示充电，0 表示未充电。 */
void battery_icon_set_charging(uint8_t charging);
/* 获取当前充电状态。 */
uint8_t battery_icon_is_charging(void);
/* 获取当前实际显示的格数，可反映充电动画当前帧。 */
uint8_t battery_icon_get_displayed_level(void);

#endif /* BATTERY_ICON_DRIVER_H */
