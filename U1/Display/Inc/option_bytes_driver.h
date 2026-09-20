#ifndef OPTION_BYTES_DRIVER_H
#define OPTION_BYTES_DRIVER_H

#include <stdint.h>

/*
 * 上电时检查 Option Byte：
 * 1. 当前已经是正常显示模式时立即返回，不产生上电延时；
 * 2. 当前不是正常显示模式时保留 5 秒烧录窗口；
 * 3. 窗口结束时按住 S1 将继续保留烧录配置；
 * 4. 未按住 S1 时切换到正常显示模式并由 Option Byte 重载触发复位。
 */
void option_bytes_boot_check(void);

/*
 * 周期调用该函数检查 S3 长按事件。
 * S3 持续按下约 3 秒后，切换到 PB6=SWD、PC0=GPIO 的编程安全模式。
 * Option Byte 重载会触发芯片复位，函数在正常情况下不会返回。
 */
void option_bytes_scan(void);

#endif /* OPTION_BYTES_DRIVER_H */
