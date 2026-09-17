#ifndef MODE_LED_DRIVER_H
#define MODE_LED_DRIVER_H

#include <stdint.h>

/*
 * 三种工作模式按面板灯的排列顺序定义：
 * 0：皮筏艇，对应 L00-L01；
 * 1：充气床，对应 L02-L01；
 * 2：轮胎，对应 L03-L01。
 */
typedef enum
{
    MODE_LED_RAFT = 0,
    MODE_LED_AIR_BED,
    MODE_LED_TIRE,
    MODE_LED_COUNT
} ModeLedMode_t;

/* 将当前模式初始化为皮筏艇，并点亮 L00-L01。 */
void mode_led_init(void);
/* 按“皮筏艇 -> 充气床 -> 轮胎 -> 皮筏艇”的顺序循环切换。 */
void mode_led_next(void);
/* 返回驱动内部记录的当前工作模式。 */
ModeLedMode_t mode_led_get_current(void);
/* 返回当前模式对应的低边 bit 掩码。 */
uint8_t mode_led_get_mask(void);

#endif /* MODE_LED_DRIVER_H */
