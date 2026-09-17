#ifndef UNIT_LED_DRIVER_H
#define UNIT_LED_DRIVER_H

#include <stdint.h>

/*
 * 四种压力单位按面板灯的排列顺序定义：
 * 0：BAR，对应 L12-L01；
 * 1：PSI，对应 L13-L01；
 * 2：KPA，对应 L14-L01；
 * 3：kg/cm2，对应 L14-L08。
 */
typedef enum
{
    UNIT_LED_BAR = 0,
    UNIT_LED_PSI,
    UNIT_LED_KPA,
    UNIT_LED_KG_CM2,
    UNIT_LED_COUNT
} UnitLedUnit_t;

/* 将当前单位初始化为 BAR，并点亮 L12-L01。 */
void unit_led_init(void);
/* 按“BAR -> PSI -> KPA -> kg/cm2 -> BAR”的顺序循环切换。 */
void unit_led_next(void);
/* 返回驱动内部记录的当前压力单位。 */
UnitLedUnit_t unit_led_get_current(void);
/* 返回当前单位索引，供 LED 扫描选择低边和公共端。 */
uint8_t unit_led_get_index(void);

#endif /* UNIT_LED_DRIVER_H */
