/**
  * @file    soft_uart.c
  * @brief   PB4/PB5 上的阻塞式 9600 8N1 软件串口驱动。
  *
  * TIM14 作为 1 MHz 自由运行计数器，为发送和接收提供微秒级时基。
  * PB4 输出串口波形，PB5 轮询接收串口波形，不改变 PA6 上现有的 USART1。
  * 该实现面向低速的 WF183D 查询/应答通信，接收接口会阻塞等待起始位。
  */

#include "soft_uart.h"
#include "main.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32f002b_hal_tim.h"
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"

#define SOFT_UART_BIT_US       104U  /* 1 / 9600 s 约为 104.17 us，取整数 us。 */
#define SOFT_UART_HALF_BIT_US  (SOFT_UART_BIT_US / 2U) /* 起始位中心采样延时。 */

static TIM_HandleTypeDef htim14;
static uint8_t soft_uart_initialized;

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
  /* HAL_TIM_Base_Init() 的底层回调；这里只为软件串口时基打开 TIM14 时钟。 */
  if (htim->Instance == TIM14)
  {
    __HAL_RCC_TIM14_CLK_ENABLE();
  }
}

static uint16_t soft_uart_timer_now(void)
{
  /* TIM14 为 16 位计数器，返回当前 1 MHz 计数值。 */
  return (uint16_t)__HAL_TIM_GET_COUNTER(&htim14);
}

static void soft_uart_delay_us(uint16_t delay_us)
{
  uint16_t start = soft_uart_timer_now();

  /* 使用无符号差值处理计数器回绕；单次延时远小于 65535 us。 */
  while ((uint16_t)(soft_uart_timer_now() - start) < delay_us)
  {
  }
}

static uint8_t soft_uart_rx_level(void)
{
  /* 将 HAL 的 GPIO 状态转换为软件串口使用的逻辑电平 0/1。 */
  return (HAL_GPIO_ReadPin(PIN_SENSOR_RX_PORT, PIN_SENSOR_RX_PIN) ==
          GPIO_PIN_SET) ? 1U : 0U;
}

static void soft_uart_tx_level(uint8_t level)
{
  /* 输出一个串口逻辑位；串口空闲和停止位均为高电平。 */
  HAL_GPIO_WritePin(PIN_SENSOR_TX_PORT, PIN_SENSOR_TX_PIN,
                    (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void soft_uart_init(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* PB4 和 PB5 同属 GPIOB，打开 GPIOB 时钟即可完成收发引脚配置。 */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 先把 TX 锁存器置高，再切换输出模式，避免初始化时产生假起始位。 */
  HAL_GPIO_WritePin(PIN_SENSOR_TX_PORT, PIN_SENSOR_TX_PIN, GPIO_PIN_SET);

  gpio_init.Pin = (uint32_t)PIN_SENSOR_TX_PIN;
  /* TX 采用推挽输出，串口逻辑 0/1 都由 MCU 主动驱动。 */
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLUP;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(PIN_SENSOR_TX_PORT, &gpio_init);

  gpio_init.Pin = (uint32_t)PIN_SENSOR_RX_PIN;
  /* RX 采用上拉输入，线路空闲时默认读取为高电平。 */
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_PULLUP;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  gpio_init.Alternate = 0U;
  HAL_GPIO_Init(PIN_SENSOR_RX_PORT, &gpio_init);

  /* 24 MHz 主频经过 24 分频后得到 1 MHz，计数器每加 1 代表 1 us。 */
  htim14.Instance = TIM14;
  htim14.Init.Prescaler = 24U - 1U;
  htim14.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim14.Init.Period = 0xFFFFU;
  htim14.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  /* 启动自由运行计数器；发送和接收均通过读取 CNT 完成精确延时。 */
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK ||
      HAL_TIM_Base_Start(&htim14) != HAL_OK)
  {
    APP_ErrorHandler();
    return;
  }

  soft_uart_initialized = 1U;
}

void soft_uart_suspend(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (soft_uart_initialized == 0U)
  {
    return;
  }
  (void)HAL_TIM_Base_Stop(&htim14);
  (void)HAL_TIM_Base_DeInit(&htim14);
  __HAL_RCC_TIM14_CLK_DISABLE();
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = 0U;
  gpio.Pin = (uint32_t)(PIN_SENSOR_TX_PIN | PIN_SENSOR_RX_PIN);
  HAL_GPIO_Init(PIN_SENSOR_TX_PORT, &gpio);
  soft_uart_initialized = 0U;
}

void soft_uart_resume(void)
{
  soft_uart_init();
}

int32_t soft_uart_send_byte(uint8_t data, uint32_t timeout_ms)
{
  uint32_t start_ms;
  uint8_t bit;

  /* 未初始化时不能访问 GPIO 和 TIM14，直接报告失败。 */
  if (soft_uart_initialized == 0U)
  {
    return -1;
  }

  start_ms = HAL_GetTick();

  /* 1. 输出低电平起始位，并保持一个完整 bit 时间。 */
  soft_uart_tx_level(0U);
  soft_uart_delay_us(SOFT_UART_BIT_US);

  /* 2. 按 UART 约定低位先发，每一位保持约 104 us。 */
  for (bit = 0U; bit < 8U; ++bit)
  {
    soft_uart_tx_level((uint8_t)(data & 0x01U));
    soft_uart_delay_us(SOFT_UART_BIT_US);
    data >>= 1U;
  }

  /* 3. 输出高电平停止位，同时恢复线路空闲状态。 */
  soft_uart_tx_level(1U);
  soft_uart_delay_us(SOFT_UART_BIT_US);

  /* 发送是阻塞操作，结束后检查总耗时是否超过调用方给出的上限。 */
  if ((timeout_ms != 0U) && ((HAL_GetTick() - start_ms) > timeout_ms))
  {
    return -1;
  }

  return 1;
}

int32_t soft_uart_send(const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
  uint16_t index;
  uint32_t start_ms;

  /* 批量发送前检查初始化状态、数据指针和长度参数。 */
  if ((soft_uart_initialized == 0U) || (data == 0) || (length == 0U))
  {
    return -1;
  }

  start_ms = HAL_GetTick();
  /* 逐字节发送；超时按整帧累计，而不是每个字节重新计时。 */
  for (index = 0U; index < length; ++index)
  {
    if (soft_uart_send_byte(data[index], 0U) < 0)
    {
      return -1;
    }

    if ((timeout_ms != 0U) && ((HAL_GetTick() - start_ms) > timeout_ms))
    {
      return -1;
    }
  }

  return (int32_t)length;
}

int32_t soft_uart_receive_byte(uint8_t *data, uint32_t timeout_ms)
{
  uint32_t start_ms;
  uint8_t value = 0U;
  uint8_t bit;

  /* 接收函数会写入 data，因此先检查初始化状态和目标指针。 */
  if ((soft_uart_initialized == 0U) || (data == 0))
  {
    return -1;
  }

  start_ms = HAL_GetTick();
  /* UART 空闲为高；轮询等待下降沿，即起始位开始。 */
  while (soft_uart_rx_level() != 0U)
  {
    if ((timeout_ms != 0U) && ((HAL_GetTick() - start_ms) >= timeout_ms))
    {
      return 0;
    }
  }

  /* 延迟半个位到起始位中心，重新采样以滤除窄脉冲和线路噪声。 */
  soft_uart_delay_us(SOFT_UART_HALF_BIT_US);
  if (soft_uart_rx_level() != 0U)
  {
    return 0;
  }

  /* 每隔一个 bit 时间在数据位中心采样，数据按低位到高位重组。 */
  for (bit = 0U; bit < 8U; ++bit)
  {
    soft_uart_delay_us(SOFT_UART_BIT_US);
    if (soft_uart_rx_level() != 0U)
    {
      value |= (uint8_t)(1U << bit);
    }
  }

  /* 跳过剩余数据时间后检查停止位；低电平表示帧格式错误。 */
  soft_uart_delay_us(SOFT_UART_BIT_US);
  if (soft_uart_rx_level() == 0U)
  {
    return -2;
  }

  *data = value;
  return 1;
}

int32_t soft_uart_receive(uint8_t *data, uint16_t length,
                          uint32_t timeout_ms)
{
  uint16_t index;
  uint32_t start_ms;
  int32_t result;

  /* 批量接收参数检查；timeout_ms 为 0 表示一直等待数据。 */
  if ((soft_uart_initialized == 0U) || (data == 0) || (length == 0U))
  {
    return -1;
  }

  start_ms = HAL_GetTick();
  /* 按总超时时间接收多个字节，避免每个字节都重新获得完整等待时间。 */
  for (index = 0U; index < length; ++index)
  {
    /* 计算当前字节仍可使用的剩余超时时间。 */
    uint32_t remaining_ms = timeout_ms;

    if (timeout_ms != 0U)
    {
      uint32_t elapsed_ms = HAL_GetTick() - start_ms;
      if (elapsed_ms >= timeout_ms)
      {
        return (int32_t)index;
      }
      remaining_ms = timeout_ms - elapsed_ms;
    }

    result = soft_uart_receive_byte(&data[index], remaining_ms);
    if (result <= 0)
    {
      return (result < 0) ? result : (int32_t)index;
    }
  }

  return (int32_t)length;
}

uint8_t soft_uart_is_initialized(void)
{
  /* 返回软件状态标志，不额外访问硬件寄存器。 */
  return soft_uart_initialized;
}
