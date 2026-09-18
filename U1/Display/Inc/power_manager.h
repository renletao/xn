#ifndef U1_POWER_MANAGER_H
#define U1_POWER_MANAGER_H

#include <stdint.h>

void power_manager_init(void);
void power_manager_task(void);
void power_manager_request_sleep(void);
uint8_t power_manager_is_sleeping(void);
/* 丢弃刚刚用于唤醒 U1 的 S1 按键事件，防止误触发压力泵切换。 */
uint8_t power_manager_suppress_s1_events(void);

#endif /* U1_POWER_MANAGER_H */
