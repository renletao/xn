#ifndef OPTION_BYTES_DRIVER_H
#define OPTION_BYTES_DRIVER_H

#include <stdint.h>

/*
 * 上电时检查 Option Byte：
 * 1. 先读取 S1，按住 S1 表示进入编程/恢复模式；
 * 2. 未按住 S1 时，检查当前 SWD/NRST 复用模式；
 * 3. 只有当前模式与显示模式不一致时才写入 Option Byte。
 * 如果模式已经正确，则不会重复写入 Flash，也不会触发复位。
 */
void option_bytes_boot_check(void);

/*
 * 周期调用该函数检查 S3 长按事件。
 * S3 持续按下约 3 秒后，切换到 PB6=SWD、PC0=GPIO 的编程安全模式。
 * Option Byte 重载会触发芯片复位，函数在正常情况下不会返回。
 */
void option_bytes_scan(void);

#endif /* OPTION_BYTES_DRIVER_H */
