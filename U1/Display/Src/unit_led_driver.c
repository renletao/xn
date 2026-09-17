#include "unit_led_driver.h"

/* 保存当前选择，切换灯的同时更新，业务层可通过接口随时读取。 */
static UnitLedUnit_t s_current_unit;

void unit_led_init(void)
{
    /* 上电默认选择 BAR，对应面板上的 L12-L01。 */
    s_current_unit = UNIT_LED_BAR;
}

void unit_led_next(void)
{
    /* 每次调用切换一个单位，越过 kg/cm2 后回到 BAR。 */
    s_current_unit = (UnitLedUnit_t)((uint8_t)s_current_unit + 1U);
    if (s_current_unit >= UNIT_LED_COUNT)
    {
        s_current_unit = UNIT_LED_BAR;
    }
}

UnitLedUnit_t unit_led_get_current(void)
{
    /* 返回当前枚举值：BAR、PSI、KPA 或 kg/cm2。 */
    return s_current_unit;
}

uint8_t unit_led_get_index(void)
{
    /* 返回索引本身，不在本模块直接操作 GPIO。 */
    return (uint8_t)s_current_unit;
}
