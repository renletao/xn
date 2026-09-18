#ifndef U2_POWER_MANAGER_H
#define U2_POWER_MANAGER_H

#include <stdint.h>

void power_manager_init(void);
void power_manager_task(void);
uint8_t power_manager_can_sleep(void);
void power_manager_commit_sleep(void);

#endif /* U2_POWER_MANAGER_H */
