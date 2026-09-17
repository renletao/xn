#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"

#define LED_SCAN_STEP_MS       1U  /* 每 1 ms 切换一次扫描时隙。 */
#define LED_MATRIX_ROWS        8U  /* Q1~Q8 共 8 路高边公共端。 */
#define LED_MATRIX_COLS        8U  /* L00~L14 共 8 路低边吸电流线。 */

/* 初始化 LED GPIO、显示缓存和扫描状态。 */
void led_init(void);
/* 周期调用，完成一整个矩阵的动态扫描。 */
void led_scan(void);
/* 设置整个 LED 矩阵是否输出；0=关闭显示，非 0=恢复显示。 */
void led_set_enabled(uint8_t enabled);
/* 查询当前 LED 矩阵是否处于显示状态。 */
uint8_t led_is_enabled(void);
/* 清空所有显示缓存，下一次扫描时所有 LED 熄灭。 */
void led_clear(void);
/* 将 8 行显示缓存全部设置为点亮状态，主要用于硬件测试。 */
void led_all_on(void);
/* 将 8 行显示缓存全部清零。 */
void led_all_off(void);
/* 一次性更新完整的 8 行矩阵数据。bit=1 表示对应 LED 点亮。 */
void led_set_frame(const uint8_t frame[LED_MATRIX_ROWS]);
/* 只更新指定行，不影响其他显示模块已经保存的行数据。 */
void led_set_row(uint8_t row, uint8_t data);
/* 设置或清除一个矩阵交点。row 为 Q1~Q8 的索引，col 为低边索引。 */
void led_set_pixel(uint8_t row, uint8_t col, uint8_t on);

#endif /* LED_DRIVER_H */
