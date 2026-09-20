/**
  * @file    usbin.h
  * @brief   USB 插入检测和 IP2326 联动驱动。
  *
  * PA5 为上拉输入，低电平表示 USB 已插入。驱动采用 20 ms 稳定判定，
  * 防止插拔抖动导致 IP2326_EN 频繁切换。
  */

#ifndef __U1_USBIN_H
#define __U1_USBIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USBIN_DEBOUNCE_MS          20U  /* 输入电平连续稳定达到该时间才确认。 */

/** 初始化 PA5 输入，并将 IP2326 设置为关闭。 */
void usbin_init(void);
/** USB 检测周期任务，应在主循环中持续调用。 */
void usbin_task(void);
/** 返回最近一次消抖确认的 USB 插入状态，1 表示已插入。 */
uint8_t usbin_is_inserted(void);
/** 立即读取 PA5 当前原始逻辑状态，不经过消抖。 */
uint8_t usbin_read_raw(void);

#ifdef __cplusplus
}
#endif

#endif /* __U1_USBIN_H */
