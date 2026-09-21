#ifndef DISPLAY_CONTROL_DRIVER_H
#define DISPLAY_CONTROL_DRIVER_H

#include <stdint.h>

/*
 * 应用层按键标志，表示最近一次扫描周期内对应按键产生了可处理事件。
 * S1/S2/S4/S5/S6 由短按释放置位；S3 由短按释放或长按事件置位。
 */
typedef enum
{
    DISPLAY_KEY_FLAG_NONE = 0U,
    DISPLAY_KEY_FLAG_S1   = (1U << 0),
    DISPLAY_KEY_FLAG_S2   = (1U << 1),
    DISPLAY_KEY_FLAG_S3   = (1U << 2),
    DISPLAY_KEY_FLAG_S4   = (1U << 3),
    DISPLAY_KEY_FLAG_S5   = (1U << 4),
    DISPLAY_KEY_FLAG_S6   = (1U << 5)
} DisplayKeyFlag_t;

/* 初始化所有由应用层统一管理的显示状态模块。 */
void display_control_init(void);

/* LED GPIO 重新初始化后，恢复压力和电池显示缓存。 */
void display_control_refresh(void);

/*
 * 主循环中的显示业务入口：
 * - 处理 S1 短按压力泵启动/暂停切换和 S2/S4 目标值调整；
 * - 处理 S5 模式切换和 S6 单位切换；
 * - 推进电池充电动画；
 * - 检查 S3 长按 3 秒恢复编程模式。
 */
void display_control_scan(void);

/* 读取并清除待处理的应用层按键标志，返回值可同时包含多个按键。 */
uint32_t display_control_get_key_flags(void);

#endif /* DISPLAY_CONTROL_DRIVER_H */
