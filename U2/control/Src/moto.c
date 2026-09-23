/**
  * @file    moto.c
 * @brief   U1 两路压力泵 TIM1 PWM 驱动。
 *
 * PA0 是 TIM1_CH1，PA1 是 TIM1_CH2，均使用 AF2。MOTO1 控制抵压泵，
 * MOTO2 控制高压泵，两路输出相互独立。
  */

#include "moto.h"
#include "main.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32f002b_hal_tim.h"
#include "py32_u1_hw_config.h"

TIM_HandleTypeDef htim1;

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* HAL_TIM_PWM_Init() 的底层回调，只处理本驱动使用的 TIM1。 */
  if (htim->Instance != TIM1)
  {
    return;
  }

  /* 同时打开 TIM1 和 GPIOA 时钟，随后把 PA0/PA1 切换到定时器复用。 */
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

  gpio_init.Pin = (uint32_t)PIN_MOTO1_PIN;
  gpio_init.Alternate = PIN_MOTO1_AF;
  HAL_GPIO_Init(PIN_MOTO1_PORT, &gpio_init);

  gpio_init.Pin = (uint32_t)PIN_MOTO2_PIN;
  gpio_init.Alternate = PIN_MOTO2_AF;
  HAL_GPIO_Init(PIN_MOTO2_PORT, &gpio_init);
}

static uint16_t moto_limit_duty(uint16_t duty)
{
  /* 防止逻辑占空比超过 100%，避免比较值溢出。 */
  return (duty > MOTO_PWM_MAX) ? MOTO_PWM_MAX : duty;
}

static uint16_t moto_duty_to_compare(uint16_t duty)
{
  uint32_t limited_duty = moto_limit_duty(duty);

  /* 逻辑占空比使用 0~1000，硬件比较值使用 0~2000。 */
  return (uint16_t)(((uint32_t)limited_duty * MOTO_PWM_PERIOD_COUNTS) /
                    MOTO_PWM_MAX);
}

static void moto_error(void)
{
  /* 定时器初始化失败时进入统一错误处理，避免压力泵处于未知状态。 */
  APP_ErrorHandler();
}

void moto_init(void)
{
  TIM_OC_InitTypeDef oc_config = {0};

  /* 预分频后计数频率为 1 MHz，周期 2000 个计数，对应 500 Hz PWM。 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 24U - 1U;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = MOTO_PWM_PERIOD_COUNTS - 1U;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  /* 初始化定时器基础计数器，并由 HAL 回调配置 PA0/PA1 复用。 */
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    moto_error();
    return;
  }

  /* PWM1 模式下，比较值以内输出有效电平，适合高电平有效的泵驱动输入。 */
  oc_config.OCMode = TIM_OCMODE_PWM1;
  oc_config.Pulse = 0U;
  oc_config.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc_config.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  oc_config.OCFastMode = TIM_OCFAST_DISABLE;
  oc_config.OCIdleState = TIM_OCIDLESTATE_RESET;
  oc_config.OCNIdleState = TIM_OCNIDLESTATE_RESET;

  /* 两个通道使用相同的 PWM 极性和初始脉宽。 */
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc_config, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_ConfigChannel(&htim1, &oc_config, TIM_CHANNEL_2) != HAL_OK)
  {
    moto_error();
    return;
  }

  /* 启动两个通道后立即停止输出，确保上电不会误启动压力泵。 */
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
  {
    moto_error();
    return;
  }

  /* 初始化完成后的安全默认状态：两路泵占空比均为 0。 */
  moto_stop();
}

void moto_suspend(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  moto_stop();
  (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
  (void)HAL_TIM_PWM_DeInit(&htim1);
  __HAL_RCC_TIM1_CLK_DISABLE();
  gpio_init.Pin = (uint32_t)(PIN_MOTO1_PIN | PIN_MOTO2_PIN);
  gpio_init.Mode = GPIO_MODE_ANALOG;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(PIN_MOTO1_PORT, &gpio_init);
}

void moto_resume(void)
{
  moto_init();
}

void moto_set_moto1_pwm(uint16_t duty)
{
  /* duty 使用 0~1000 的逻辑刻度，再换算为 500 Hz PWM 的比较值。 */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                        moto_duty_to_compare(duty));
}

void moto_set_moto2_pwm(uint16_t duty)
{
  /* duty 使用 0~1000 的逻辑刻度，再换算为 500 Hz PWM 的比较值。 */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2,
                        moto_duty_to_compare(duty));
}

void moto_low_pressure_on(void)
{
  /* 抵压泵开启为 100%，不影响高压泵当前状态。 */
  moto_set_moto1_pwm(MOTO_PWM_MAX);
}

void moto_low_pressure_off(void)
{
  /* 抵压泵关闭为 0%，不影响高压泵当前状态。 */
  moto_set_moto1_pwm(0U);
}

void moto_high_pressure_on(void)
{
  /* 高压泵开启为 100%，不影响抵压泵当前状态。 */
  moto_set_moto2_pwm(MOTO_PWM_MAX);
}

void moto_high_pressure_off(void)
{
  /* 高压泵关闭为 0%，不影响抵压泵当前状态。 */
  moto_set_moto2_pwm(0U);
}

void moto_stop(void)
{
  /* 两路泵均输出 0，占空比为 0。 */
  moto_set_moto1_pwm(0U);
  moto_set_moto2_pwm(0U);
}
