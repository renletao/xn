/**
  * @file    led.h
  * @brief   两路高电平点亮 LED 控制接口。
  *
  * PB3 控制主 LED，PC1 控制 IP2326 状态 LED。两个引脚均为高电平点亮、
  * 低电平熄灭，初始化后默认关闭。
  */

#ifndef __U1_LED_H
#define __U1_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 PB3 和 PC1，并将两路 LED 设置为熄灭。 */
void led_init(void);
/** 设置主 LED 状态，非 0 点亮，0 熄灭。 */
void led_set(uint8_t on);
/** 点亮 PB3 主 LED。 */
void led_on(void);
/** 熄灭 PB3 主 LED。 */
void led_off(void);

/** 设置 PC1 状态 LED，非 0 点亮，0 熄灭。 */
void ip2326_led_set(uint8_t on);
/** 点亮 PC1 状态 LED。 */
void ip2326_led_on(void);
/** 熄灭 PC1 状态 LED。 */
void ip2326_led_off(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_LED_H */
