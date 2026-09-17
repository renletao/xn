/**
  * @file    ip2326.h
  * @brief   IP2326 快充芯片使能引脚驱动。
  *
  * PA2 为高电平有效：输出高电平表示开启 IP2326，输出低电平表示关闭。
  * 初始化时会先写入关闭电平，再配置为推挽输出，避免上电瞬间误开启。
  */

#ifndef __U1_IP2326_H
#define __U1_IP2326_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 PA2，并将 IP2326 设置为关闭状态。 */
void ip2326_init(void);
/** 按 enable 参数设置芯片使能状态，非 0 开启，0 关闭。 */
void ip2326_set(uint8_t enable);
/** 开启 IP2326，PA2 输出有效电平。 */
void ip2326_enable(void);
/** 关闭 IP2326，PA2 输出无效电平。 */
void ip2326_disable(void);
/** 读取 PA2 当前电平并转换为逻辑使能状态，返回 1 表示开启。 */
uint8_t ip2326_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_IP2326_H */
