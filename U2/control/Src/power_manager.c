#include "power_manager.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"
#include "py32f002b_hal_pwr.h"
#include "adc.h"
#include "charge.h"
#include "ip2326.h"
#include "led.h"
#include "moto.h"
#include "soft_uart.h"
#include "uart_command_driver.h"
#include "usbin.h"
#include "wf183d.h"

#define U2_WAKE_LOCAL_PIN       GPIO_PIN_5
#define U2_WAKE_LINK_PIN        GPIO_PIN_7
#define U2_WAKE_LOCAL_EVENT     0x01U
#define U2_WAKE_LINK_EVENT      0x02U

static volatile uint8_t s_wake_flags;
static uint8_t s_sleep_committed;
static uint8_t s_sleeping;
static uint8_t s_usb_notify_pending;

static void power_gpio_config(GPIO_TypeDef *port, uint16_t pin,
                              uint32_t mode, uint32_t pull)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = pin;
  gpio.Mode = mode;
  gpio.Pull = pull;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = 0U;
  HAL_GPIO_Init(port, &gpio);
}

static void power_config_wake_inputs(void)
{
  power_gpio_config(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN,
                    GPIO_MODE_IT_FALLING, GPIO_PULLUP);
  power_gpio_config(PIN_WKUP_PORT, U2_WAKE_LINK_PIN,
                    GPIO_MODE_IT_RISING, GPIO_PULLDOWN);
  __HAL_GPIO_EXTI_CLEAR_IT(U2_WAKE_LOCAL_PIN);
  __HAL_GPIO_EXTI_CLEAR_IT(U2_WAKE_LINK_PIN);
  HAL_NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void power_config_running_inputs(void)
{
  HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
  power_gpio_config(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN,
                    GPIO_MODE_INPUT, GPIO_PULLUP);
  power_gpio_config(PIN_WKUP_PORT, U2_WAKE_LINK_PIN,
                    GPIO_MODE_INPUT, GPIO_PULLDOWN);
  __HAL_GPIO_EXTI_CLEAR_IT(U2_WAKE_LOCAL_PIN);
  __HAL_GPIO_EXTI_CLEAR_IT(U2_WAKE_LINK_PIN);
  HAL_NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
}

static void power_config_low_leakage(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = 0U;
  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_1;
  HAL_GPIO_Init(GPIOC, &gpio);
}

static uint8_t power_pulse_u1(void)
{
  if (HAL_GPIO_ReadPin(PIN_WKUP_PORT, U2_WAKE_LINK_PIN) != GPIO_PIN_RESET)
  {
    return 0U;
  }
  HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
  HAL_GPIO_WritePin(PIN_WKUP_PORT, U2_WAKE_LINK_PIN, GPIO_PIN_SET);
  power_gpio_config(PIN_WKUP_PORT, U2_WAKE_LINK_PIN,
                    GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
  HAL_Delay(5U);
  power_gpio_config(PIN_WKUP_PORT, U2_WAKE_LINK_PIN,
                    GPIO_MODE_INPUT, GPIO_PULLDOWN);
  __HAL_GPIO_EXTI_CLEAR_IT(U2_WAKE_LINK_PIN);
  return 1U;
}

static void power_restore(uint8_t reason)
{
  APP_SystemClockConfig();
  power_config_running_inputs();
  led_init();
  ip2326_init();
  usbin_init();
  soft_uart_resume();
  wf183d_resume();
  uart_command_resume();
  moto_resume();
  adc_resume();
  charge_init();
  s_sleeping = 0U;
  s_sleep_committed = 0U;
  if ((reason & U2_WAKE_LOCAL_EVENT) != 0U)
  {
    s_usb_notify_pending = 1U;
  }
  /* A wake event is consumed by this restore sequence. */
  s_wake_flags = 0U;
}

static void power_enter_stop(void)
{
  uint8_t reason;
  uint8_t local_wake;
  uint32_t primask;

  s_wake_flags = 0U;
  moto_suspend();
  adc_suspend();
  soft_uart_suspend();
  uart_command_suspend();
  ip2326_disable();
  led_off();
  ip2326_led_off();
  power_config_low_leakage();
  power_config_wake_inputs();

  if ((s_wake_flags != 0U) ||
      (HAL_GPIO_ReadPin(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN) == GPIO_PIN_RESET) ||
      (HAL_GPIO_ReadPin(PIN_WKUP_PORT, U2_WAKE_LINK_PIN) == GPIO_PIN_SET))
  {
    reason = s_wake_flags;
    local_wake = (HAL_GPIO_ReadPin(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN) ==
                  GPIO_PIN_RESET);
    if (local_wake != 0U)
    {
      reason |= U2_WAKE_LOCAL_EVENT;
    }
    power_restore(reason);
    return;
  }

  /* Close the last check-to-WFI race: an edge during this window remains
   * pending in EXTI and wakes WFI, while a flag already serviced aborts. */
  primask = __get_PRIMASK();
  __disable_irq();
  if ((s_wake_flags != 0U) ||
      (HAL_GPIO_ReadPin(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN) == GPIO_PIN_RESET) ||
      (HAL_GPIO_ReadPin(PIN_WKUP_PORT, U2_WAKE_LINK_PIN) == GPIO_PIN_SET))
  {
    reason = s_wake_flags;
    local_wake = (HAL_GPIO_ReadPin(PIN_USBIN_PORT, U2_WAKE_LOCAL_PIN) ==
                  GPIO_PIN_RESET);
    if (local_wake != 0U)
    {
      reason |= U2_WAKE_LOCAL_EVENT;
    }
    if (primask == 0U)
    {
      __enable_irq();
    }
    power_restore(reason);
    return;
  }

  s_sleeping = 1U;
  HAL_SuspendTick();
  __DSB();
  if (primask == 0U)
  {
    __enable_irq();
  }
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  __ISB();
  HAL_ResumeTick();
  reason = s_wake_flags;
  power_restore(reason);
}

void power_manager_init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
  power_config_running_inputs();
  s_wake_flags = 0U;
  s_sleep_committed = 0U;
  s_sleeping = 0U;
  s_usb_notify_pending = 0U;
}

uint8_t power_manager_can_sleep(void)
{
  return (usbin_read_raw() == 0U) && (usbin_is_inserted() == 0U) &&
         (g_uart_pump_running == 0U) &&
         (charge_get_state() != CHARGE_STATE_CHARGING);
}

void power_manager_commit_sleep(void)
{
  s_sleep_committed = 1U;
}

void power_manager_task(void)
{
  if ((s_sleep_committed != 0U) && (s_sleeping == 0U))
  {
    s_sleep_committed = 0U;
    if (power_manager_can_sleep() && (s_wake_flags == 0U))
    {
      power_enter_stop();
    }
    else
    {
      /* READY may already have put U1 into its final STOP preparation. */
      (void)power_pulse_u1();
    }
  }

  if (s_usb_notify_pending != 0U && usbin_is_inserted() != 0U)
  {
    if (power_pulse_u1() != 0U)
    {
      s_usb_notify_pending = 0U;
    }
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == U2_WAKE_LOCAL_PIN)
  {
    s_wake_flags |= U2_WAKE_LOCAL_EVENT;
  }
  else if (GPIO_Pin == U2_WAKE_LINK_PIN)
  {
    s_wake_flags |= U2_WAKE_LINK_EVENT;
  }
}
