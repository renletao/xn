#ifndef DC_LED_DRIVER_H
#define DC_LED_DRIVER_H

#include <stdint.h>

/* DC 指示灯由 L04-L01 和 L05-L01 两只 LED 组成，充电时同时点亮。 */
/* 初始化 DC 指示状态，默认关闭。 */
void dc_led_init(void);
/* 设置充电状态：非 0 点亮 DC 指示灯，0 关闭。 */
void dc_led_set_charging(uint8_t charging);
/* 查询当前 DC 指示灯是否处于充电显示状态。 */
uint8_t dc_led_is_charging(void);

#endif /* DC_LED_DRIVER_H */
