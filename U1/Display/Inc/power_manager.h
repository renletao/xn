#ifndef U1_POWER_MANAGER_H
#define U1_POWER_MANAGER_H

#include <stdint.h>

void power_manager_init(void);
void power_manager_task(void);
void power_manager_request_sleep(void);
uint8_t power_manager_is_sleeping(void);
/*
 * 过滤刚刚用于唤醒 U1 的 S1 事件：短按和释放事件丢弃，
 * 但保留同一次按键产生的 LONG_PRESS，使一次长按即可开机。
 */
uint32_t power_manager_filter_s1_events(uint32_t events);

#endif /* U1_POWER_MANAGER_H */
