/**
  * @file    moto.h
 * @brief   U1 两路压力泵 TIM1 PWM 驱动。
 *
 * PA0 为 MOTO1（抵压泵），PA1 为 MOTO2（高压泵），PWM 高电平有效。
 * 两路泵独立控制，允许同时输出。
  */

#ifndef __U1_MOTO_H
#define __U1_MOTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTO_PWM_MAX                1000U  /* PWM 周期计数，约对应 0~100.0%。 */

/** 初始化 TIM1、PA0/PA1 复用，并以停止状态启动 PWM。 */
void moto_init(void);
/** 设置 MOTO1（抵压泵）的比较值，占空比超过上限时自动限幅。 */
void moto_set_moto1_pwm(uint16_t duty);
/** 设置 MOTO2（高压泵）的比较值，占空比超过上限时自动限幅。 */
void moto_set_moto2_pwm(uint16_t duty);
/** 开启抵压泵：MOTO1 输出 100%，不改变 MOTO2。 */
void moto_low_pressure_on(void);
/** 关闭抵压泵：MOTO1 输出 0%，不改变 MOTO2。 */
void moto_low_pressure_off(void);
/** 开启高压泵：MOTO2 输出 100%，不改变 MOTO1。 */
void moto_high_pressure_on(void);
/** 关闭高压泵：MOTO2 输出 0%，不改变 MOTO1。 */
void moto_high_pressure_off(void);
/** 停止：两路 PWM 比较值均置 0。 */
void moto_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_MOTO_H */
