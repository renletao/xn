#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "main.h"
#include <stdint.h>
#include "py32f0xx_hal.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32_u1_hw_config.h"

/*
 * 按键与 LED 低边共用 GPIO，因此按键扫描时会暂时关闭 LED 输出，
 * 将这些引脚切换为上拉输入，读取完成后再恢复为 LED 推挽输出。
 */
#define KEY_SCAN_PERIOD_MS       10U   /* 按键采样周期。 */
#define KEY_DEBOUNCE_MS          30U   /* 输入稳定超过该时间才确认状态改变。 */
#define KEY_LONG_PRESS_MS        800U  /* S1 长按事件的触发时间。 */
#define KEY_LONG_HOLD_MS         200U  /* S1 长按后重复产生保持事件的周期。 */
#define KEY_S3_LONG_PRESS_MS     3000U /* S3 恢复编程模式的长按时间。 */

#define KEY_DEBOUNCE_TICKS       (KEY_DEBOUNCE_MS / KEY_SCAN_PERIOD_MS)
#define KEY_LONG_PRESS_TICKS     (KEY_LONG_PRESS_MS / KEY_SCAN_PERIOD_MS)
#define KEY_LONG_HOLD_TICKS      (KEY_LONG_HOLD_MS / KEY_SCAN_PERIOD_MS)
#define KEY_S3_LONG_PRESS_TICKS  (KEY_S3_LONG_PRESS_MS / KEY_SCAN_PERIOD_MS)

typedef enum
{
    /* 枚举值与 LED 低边数组中的按键顺序无关，只作为业务索引使用。 */
    KEY_S1 = 0,
    KEY_S2,
    KEY_S3,
    KEY_S4,
    KEY_S5,
    KEY_S6,
    KEY_COUNT
} KeyId_t;

typedef enum
{
    /* 事件采用位掩码，可在一次读取中同时返回多个事件。 */
    KEY_EVENT_NONE          = 0U,
    KEY_EVENT_PRESS         = (1U << 0),
    KEY_EVENT_SHORT_RELEASE = (1U << 1),
    KEY_EVENT_LONG_PRESS    = (1U << 2),
    KEY_EVENT_LONG_HOLD     = (1U << 3),
    KEY_EVENT_LONG_RELEASE  = (1U << 4)
} KeyEvent_t;

void key_init(void);
/* 主循环中持续调用；函数内部按 KEY_SCAN_PERIOD_MS 限制实际采样频率。 */
void key_scan(void);
/* 读取指定按键的事件并清空该按键已经报告的事件。 */
uint32_t key_get_event(KeyId_t key);
/* 读取指定按键当前事件但不清除，供多个应用模块协同判断。 */
uint32_t key_peek_event(KeyId_t key);
/* 查询指定按键当前是否处于按下状态，不清除任何事件。 */
uint8_t key_is_pressed(KeyId_t key);

#endif /* KEY_DRIVER_H */
