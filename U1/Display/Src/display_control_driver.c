#include "display_control_driver.h"
#include "key_driver.h"
#include "mode_led_driver.h"
#include "unit_led_driver.h"
#include "pressure_display_driver.h"
#include "battery_icon_driver.h"
#include "dc_led_driver.h"
#include "option_bytes_driver.h"
#include "led_driver.h"
#include "uart_command_driver.h"

/* 保存尚未被上层业务读取的按键标志，避免事件在模块之间传递时丢失。 */
static uint32_t s_key_flags;

/*
 * 这是应用层显示功能的唯一汇总入口。
 * 各个具体模块仍然保留自己的状态和调用接口，
 * 本文件只负责初始化顺序以及按键事件的分发，不直接操作 GPIO。
 */
void display_control_init(void)
{
    /*
     * 这里仅初始化各模块的软件状态，不直接配置 GPIO。
     * GPIO 和矩阵扫描由 led_init() 完成，避免多个模块重复初始化同一引脚。
     */
    /* 模式灯：上电默认选择第一个模式。 */
    mode_led_init();
    /* 单位灯：上电默认选择第一个单位。 */
    unit_led_init();
    /* 压力显示：实际值和目标值都初始化为 000。 */
    pressure_display_init();
    /* 电池图标：清除电量/充电状态并点亮外框。 */
    battery_icon_init();
    /* DC 指示灯：上电默认关闭。 */
    dc_led_init();
    /* 清除上一次运行残留的按键标志。 */
    s_key_flags = DISPLAY_KEY_FLAG_NONE;
}

void display_control_scan(void)
{
    uint32_t s1_events;

    uint32_t s3_events;
    uint8_t charging;
    uint8_t battery_percent;
    uint32_t pressure_pa;

    /*
     * 短按功能：key_get_event() 会读取并清除对应按键的事件，
     * 因此每个按键只在这里消费一次，避免同一事件重复执行。
     */
    /* S1 短按切换压力泵，长按切换 U1 本地显示状态。 */
    s1_events = key_get_event(KEY_S1);
    if ((s1_events & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S1;
        /* 目标值统一换算为 Pa，当前模式值与 U2 协议枚举保持一致。 */
        (void)uart_command_toggle_pump(
            pressure_display_get_target_pa(),
            (uint8_t)mode_led_get_current());
    }
    if ((s1_events & KEY_EVENT_LONG_PRESS) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S1;
        /* 只改变输出开关，不清除显示缓存，便于再次长按后恢复原内容。 */
        led_set_enabled((led_is_enabled() == 0U) ? 1U : 0U);
    }

    /* S5 短按：皮筏艇、充气床、轮胎三个模式依次循环。 */
    if ((key_get_event(KEY_S5) & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S5;
        mode_led_next();
    }

    /* S6 短按：在单位指示灯之间循环切换。 */
    if ((key_get_event(KEY_S6) & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S6;
        unit_led_next();
        /* 单位改变后，实际值和目标值都按物理压力重新换算显示。 */
        pressure_display_on_unit_changed();
    }

    /* S4 短按：目标压力值加 1，最大保持在 999。 */
    if ((key_get_event(KEY_S4) & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S4;
        pressure_display_target_increase();
    }

    /* S2 短按：目标压力值减 1，最小保持在 000。 */
    if ((key_get_event(KEY_S2) & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S2;
        pressure_display_target_decrease();
    }

    /*
     * 周期任务：电池动画依赖 HAL 时间基准，Option Byte 恢复依赖 S3 长按事件。
     * 两者都不需要在主循环中额外添加定时器或中断。
     */
    /* 电池图标根据 HAL tick 推进充电动画。 */
    battery_icon_scan();

    /* 消费最近一次有效的 CMD=0x02 返回，更新本地充电和实际压力显示。 */
    if (uart_command_take_charging_status(&charging, &battery_percent,
                                          &pressure_pa) != 0U)
    {
        uint8_t battery_level = 0U;

        if (battery_percent > 80U)
        {
            battery_level = 3U;
        }
        else if (battery_percent > 60U)
        {
            battery_level = 2U;
        }
        else if (battery_percent > 30U)
        {
            battery_level = 1U;
        }

        dc_led_set_charging(charging);
        battery_icon_set_charging(charging);
        battery_icon_set_level(battery_level);
        pressure_display_set_actual_pa(pressure_pa);
    }
    /* S3 的事件先窥读并记录标志，再分别处理短按和长按功能。 */
    s3_events = key_peek_event(KEY_S3);
    if ((s3_events & (KEY_EVENT_SHORT_RELEASE | KEY_EVENT_LONG_PRESS)) != 0U)
    {
        s_key_flags |= DISPLAY_KEY_FLAG_S3;
    }
    if ((s3_events & KEY_EVENT_SHORT_RELEASE) != 0U)
    {
        /* S3 短按切换 U2 的 PB3 灯，并等待 U2 返回实际状态。 */
        (void)uart_command_toggle_remote_led();
    }
    /* S3 长按达到 3 秒后切回编程安全的 Option Byte 配置。 */
    option_bytes_scan();
}

uint32_t display_control_get_key_flags(void)
{
    uint32_t flags;

    /* 采用一次读取、一次清除，避免同一个按键事件被重复处理。 */
    flags = s_key_flags;
    s_key_flags = DISPLAY_KEY_FLAG_NONE;
    return flags;
}
