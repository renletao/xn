/**
  * @file    led.c
  * @brief   两路高电平有效 LED 控制驱动。
  *
  * PB3 控制主 LED，PC1 控制 IP2326 状态 LED。两个输出均为高电平点亮，
  * 低电平熄灭；初始化阶段先写入熄灭电平，避免 GPIO 切换时 LED 闪烁。
  */

#include "led.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32_u1_hw_config.h"

static void led_configure_pin(GPIO_TypeDef *port, uint32_t pin)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* 两路 LED 都是普通推挽输出，不需要复用功能和内部上下拉。 */
  gpio_init.Pin = pin;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(port, &gpio_init);
}

void led_init(void)
{
  /* PB3 和 PC1 分属不同 GPIO 端口，需要分别打开端口时钟。 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* 先设置输出锁存器为低电平，再切换输出模式，确保上电默认熄灭。 */
  HAL_GPIO_WritePin(PIN_LED_PORT, PIN_LED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(PIN_IP2326_LED_PORT, PIN_IP2326_LED_PIN, GPIO_PIN_RESET);

  led_configure_pin(PIN_LED_PORT, (uint32_t)PIN_LED_PIN);
  led_configure_pin(PIN_IP2326_LED_PORT, (uint32_t)PIN_IP2326_LED_PIN);

  /* 用公开接口再次写入默认状态，保证初始化行为与运行时接口一致。 */
  led_off();
  ip2326_led_off();
}

void led_set(uint8_t on)
{
  /* 非 0 统一视为点亮请求；HAL 会把 GPIO_PIN_SET 输出为高电平。 */
  HAL_GPIO_WritePin(PIN_LED_PORT, PIN_LED_PIN,
                    (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void led_on(void)
{
  /* 主 LED 的便捷点亮接口。 */
  led_set(1U);
}

void led_off(void)
{
  /* 主 LED 的便捷熄灭接口。 */
  led_set(0U);
}

void ip2326_led_set(uint8_t on)
{
  /* PC1 状态 LED 与主 LED 使用相同的高有效逻辑。 */
  HAL_GPIO_WritePin(PIN_IP2326_LED_PORT, PIN_IP2326_LED_PIN,
                    (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ip2326_led_on(void)
{
  /* IP2326 状态 LED 的便捷点亮接口。 */
  ip2326_led_set(1U);
}

void ip2326_led_off(void)
{
  /* IP2326 状态 LED 的便捷熄灭接口。 */
  ip2326_led_set(0U);
}
