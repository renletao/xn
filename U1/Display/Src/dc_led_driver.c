#include "dc_led_driver.h"

static uint8_t s_charging;

void dc_led_init(void)
{
    /* 这里只初始化软件标志，实际 GPIO 输出由 led_scan() 的 Q1 时隙完成。 */
    s_charging = 0U;
}

void dc_led_set_charging(uint8_t charging)
{
    /* 将输入状态规范化为 0/1，避免保存任意非零数值。 */
    s_charging = (charging != 0U) ? 1U : 0U;
}

uint8_t dc_led_is_charging(void)
{
    /* LED 扫描模块通过该接口读取状态，决定是否拉低 L04/L05。 */
    return s_charging;
}
