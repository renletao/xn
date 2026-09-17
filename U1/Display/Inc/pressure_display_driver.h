#ifndef PRESSURE_DISPLAY_DRIVER_H
#define PRESSURE_DISPLAY_DRIVER_H

#include <stdint.h>

/* 上排和下排均为百位、十位、个位三个数字。 */
#define PRESSURE_DISPLAY_MAX 999U /* 显示编码范围：000~999。 */

/* 设置参数按 100 倍传入，999.00 对应 99900。 */
#define PRESSURE_INPUT_MAX (PRESSURE_DISPLAY_MAX * 100U)

/* 内部压力统一使用 0.01 BAR，避免单位切换时反复换算产生累计误差。 */
#define PRESSURE_BASE_UNIT_CBAR 1U

/* 初始化实际值和目标值，并刷新显示缓存。 */
void pressure_display_init(void);
/* 按当前单位设置上排实际值，输入为当前单位数值放大 100 倍后的整数。 */
void pressure_display_set_actual(uint32_t value);
/* 按 U2 返回的 Pa 数值设置上排实际压力，并按当前单位显示。 */
void pressure_display_set_actual_pa(uint32_t pressure_pa);
/* 读取当前单位下的上排实际显示编码。 */
uint16_t pressure_display_get_actual(void);
/* 按当前单位设置下排目标值，输入为当前单位数值放大 100 倍后的整数。 */
void pressure_display_set_target(uint32_t value);
/* 读取当前单位下的下排目标显示编码。 */
uint16_t pressure_display_get_target(void);
/* 获取当前目标压力的 Pa 整数值，供 CMD=0x03 压力泵控制使用。 */
uint32_t pressure_display_get_target_pa(void);
/* 按当前单位的最小显示步进增加目标值，显示为 999 时保持不变。 */
void pressure_display_target_increase(void);
/* 按当前单位的最小显示步进减少目标值，显示为 000 时保持不变。 */
void pressure_display_target_decrease(void);

/* 单位切换后重新换算并刷新实际值、目标值的显示缓存。 */
void pressure_display_on_unit_changed(void);

#endif /* PRESSURE_DISPLAY_DRIVER_H */
