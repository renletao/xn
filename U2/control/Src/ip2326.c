/**
  * @file    ip2326.c
  * @brief   IP2326 快充芯片使能引脚驱动。
  *
  * PA2 为推挽输出，板级定义为高电平有效。所有上层状态最终都通过
  * ip2326_set() 转换为实际 GPIO 电平，便于以后修改有效电平而不影响业务接口。
  */

#include "ip2326.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32_u1_hw_config.h"

static GPIO_PinState ip2326_level(uint8_t enable)
{
  /* 将逻辑使能状态转换为实际 GPIO 电平，兼容高有效/低有效硬件。 */
  if (enable != 0U)
  {
    return (PIN_IP2326_EN_ACTIVE_LEVEL != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
  }

  return (PIN_IP2326_EN_ACTIVE_LEVEL != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

void ip2326_set(uint8_t enable)
{
  /* 测试期间屏蔽所有上层开启请求，只允许输出关闭电平。 */
#if U2_IP2326_DISABLED
  enable = 0U;
#endif

  /* 只负责写电平，不改变 GPIO 模式；模式由 ip2326_init() 统一配置。 */
  HAL_GPIO_WritePin(PIN_IP2326_EN_PORT, PIN_IP2326_EN_PIN,
                    ip2326_level(enable));
}

void ip2326_init(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* PA2 属于 GPIOA，先打开端口时钟才能访问配置寄存器。 */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* 先写关闭电平，再切换为输出，避免切换瞬间产生错误使能脉冲。 */
  ip2326_set(0U);

  gpio_init.Pin = (uint32_t)PIN_IP2326_EN_PIN;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(PIN_IP2326_EN_PORT, &gpio_init);

  /* 再次明确写入关闭状态，保证初始化结束时状态确定。 */
  ip2326_disable();
}

void ip2326_enable(void)
{
  /* 通过统一入口输出有效电平，避免业务层直接操作 PA2。 */
  ip2326_set(1U);
}

void ip2326_disable(void)
{
  /* 通过统一入口输出无效电平。 */
  ip2326_set(0U);
}

uint8_t ip2326_is_enabled(void)
{
  /* 读取实际引脚电平，再按配置的有效电平还原为逻辑状态。 */
  GPIO_PinState level = HAL_GPIO_ReadPin(PIN_IP2326_EN_PORT,
                                         PIN_IP2326_EN_PIN);
  GPIO_PinState active = (PIN_IP2326_EN_ACTIVE_LEVEL != 0U) ?
                         GPIO_PIN_SET : GPIO_PIN_RESET;

  return (level == active) ? 1U : 0U;
}
