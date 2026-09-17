#include "mode_led_driver.h"

/* 保存当前选择，切换灯的同时更新，业务层可通过接口随时读取。 */
static ModeLedMode_t s_current_mode;

void mode_led_init(void)
{
    /* 上电默认选择皮筏艇，对应面板上的 L00-L01。 */
    s_current_mode = MODE_LED_RAFT;
}

void mode_led_next(void)
{
    /* 先切换到下一种模式，越过轮胎模式后回到皮筏艇模式。 */
    s_current_mode = (ModeLedMode_t)((uint8_t)s_current_mode + 1U);
    if (s_current_mode >= MODE_LED_COUNT)
    {
        s_current_mode = MODE_LED_RAFT;
    }
}

ModeLedMode_t mode_led_get_current(void)
{
    /* 返回当前枚举值：皮筏艇、充气床或轮胎。 */
    return s_current_mode;
}

uint8_t mode_led_get_mask(void)
{
    /* 当前模式只允许一个 bit 有效，LED 扫描据此点亮一只模式灯。 */
    return (uint8_t)(1U << (uint8_t)s_current_mode);
}
