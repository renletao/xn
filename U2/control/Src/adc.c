/**
  * @file    adc.c
  * @brief   U1 ADC1 驱动：采集四路模拟量并换算为工程单位。
  *
  * PA3/PA4/PB0/PB1 分别连接电池电压、电流检测、电机电压和 12 V 电压。
  * 每次采样前动态选择一个通道，完成单次转换后立即停止 ADC，避免不同
  * 通道之间残留配置影响下一次采样。
  */

#include "adc.h"
#include "py32f002b_hal_adc.h"
#include "py32f002b_hal_gpio.h"
#include "py32f002b_hal_rcc.h"
#include "py32f0xx_hal.h"

ADC_HandleTypeDef hadc1;
volatile ADC_U1_DataTypeDef g_adc_data;

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
  GPIO_InitTypeDef gpio_init = {0};

  /* HAL_ADC_Init() 会回调本函数；只为 ADC1 打开本工程需要的时钟和引脚。 */
  if (hadc->Instance != ADC1)
  {
    return;
  }

  __HAL_RCC_ADC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 模拟输入模式关闭数字输入通路，降低 ADC 输入漏电和数字噪声。 */
  gpio_init.Mode = GPIO_MODE_ANALOG;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Pin = (uint32_t)(PIN_BAT_ADC_PIN | PIN_CURR_ADC_PIN);
  HAL_GPIO_Init(PIN_BAT_ADC_PORT, &gpio_init);

  gpio_init.Pin = (uint32_t)(PIN_MOTO_ADC_PIN | PIN_12V_ADC_PIN);
  HAL_GPIO_Init(PIN_MOTO_ADC_PORT, &gpio_init);
}

void adc_init(void)
{
  hadc1.Instance = ADC1;

  /* 采用 12 位右对齐、软件触发、单次转换；采样时间取较长值以适应外部电阻网络。 */
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.SamplingTimeCommon = ADC_SAMPLETIME_239CYCLES_5;

  /* HAL_ADC_Init() 同时完成 ADC 寄存器配置，并触发 HAL_ADC_MspInit()。 */
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  /* 校准可降低 ADC 固有偏移，是后续电压/电流换算的前置步骤。 */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    APP_ErrorHandler();
  }
}

static uint16_t adc_convert(uint32_t channel)
{
  ADC_ChannelConfTypeDef channel_config = {0};
  uint16_t value;

  /* 每次只挂载一个通道，避免扫描序列顺序与结果对应关系产生歧义。 */
  channel_config.Channel = channel;
  channel_config.Rank = ADC_RANK_CHANNEL_NUMBER;
  channel_config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  /* 先配置通道，再启动一次软件触发转换。 */
  if (HAL_ADC_ConfigChannel(&hadc1, &channel_config) != HAL_OK)
  {
    APP_ErrorHandler();
    return 0U;
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    channel_config.Rank = ADC_RANK_NONE;
    (void)HAL_ADC_ConfigChannel(&hadc1, &channel_config);
    APP_ErrorHandler();
    return 0U;
  }

  /* 阻塞等待转换完成，10 ms 超时用于避免 ADC 异常时卡死主循环。 */
  if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    channel_config.Rank = ADC_RANK_NONE;
    (void)HAL_ADC_ConfigChannel(&hadc1, &channel_config);
    APP_ErrorHandler();
    return 0U;
  }

  /* 读取 12 位结果，然后停止本次转换并清除通道配置。 */
  value = (uint16_t)HAL_ADC_GetValue(&hadc1);
  (void)HAL_ADC_Stop(&hadc1);
  channel_config.Rank = ADC_RANK_NONE;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel_config) != HAL_OK)
  {
    APP_ErrorHandler();
    return 0U;
  }
  return value;
}

uint16_t get_vref(void)
{
  /* 内部 VREFINT 的标称电压已知，因此可反推出当前实际供电参考电压。 */
  uint16_t raw = adc_convert(ADC_CHANNEL_VREFINT);

  if (raw == 0U)
  {
    return 3300U;
  }

  return (uint16_t)(((uint32_t)PY32_U1_VREFINT_MV * ADC_U1_MAX_VALUE) /
                    raw);
}

uint16_t adc_read_channel_raw(uint32_t channel)
{
  return adc_convert(channel);
}

uint16_t adc_raw_to_mv(uint16_t raw, uint16_t vref_mv)
{
  /* 加半个量化单位后再整数除法，实现四舍五入而不是直接截断。 */
  return (uint16_t)(((uint32_t)raw * vref_mv + (ADC_U1_MAX_VALUE / 2U)) /
                    ADC_U1_MAX_VALUE);
}

void adc_task_100ms(void)
{
  static uint32_t last_sample_ms;
  uint32_t now = HAL_GetTick();
  uint16_t vref_mv;

  /* 任务可以每圈调用，但只有间隔达到 100 ms 才真正访问 ADC。 */
  if ((now - last_sample_ms) < 100U)
  {
    return;
  }
  last_sample_ms = now;

  /* 先更新参考电压，再使用同一次参考值换算四个外部通道。 */
  vref_mv = get_vref();
  g_adc_data.vref_mv = vref_mv;

  g_adc_data.battery.raw = adc_read_bat_raw();
  g_adc_data.current.raw = adc_read_current_raw();
  g_adc_data.motor.raw = adc_read_moto_raw();
  g_adc_data.input_12v.raw = adc_read_12v_raw();

  g_adc_data.battery.pin_mv = adc_raw_to_mv(g_adc_data.battery.raw, vref_mv);
  g_adc_data.current.pin_mv = adc_raw_to_mv(g_adc_data.current.raw, vref_mv);
  g_adc_data.motor.pin_mv = adc_raw_to_mv(g_adc_data.motor.raw, vref_mv);
  g_adc_data.input_12v.pin_mv = adc_raw_to_mv(g_adc_data.input_12v.raw, vref_mv);

  /* PA3：100 k/(1 M+100 k) 分压，外部电池电压约为引脚电压的 11 倍。 */
  g_adc_data.battery_mv = (uint32_t)g_adc_data.battery.pin_mv * 11U;

  /*
   * PA4：以零电流基准电压为中心换算有符号电流。
   * 低于零点得到负值（充电），高于或等于零点得到非负值（放电）。
   * 零点和比例必须依据实际采样电阻、放大器增益及方向进行校准。
   */
  g_adc_data.current_ma = ((int32_t)g_adc_data.current.pin_mv -
                           (int32_t)PY32_U1_CURRENT_ZERO_MV) *
                          (int32_t)PY32_U1_CURRENT_MA_PER_MV;

  /* PB0/PB1 的分压比例由硬件决定，当前配置宏默认为 1:1。 */
  g_adc_data.motor_mv = ((uint32_t)g_adc_data.motor.pin_mv *
                         PY32_U1_MOTO_DIVIDER_NUM) /
                        PY32_U1_MOTO_DIVIDER_DEN;
  g_adc_data.input_12v_mv = ((uint32_t)g_adc_data.input_12v.pin_mv *
                             PY32_U1_12V_DIVIDER_NUM) /
                            PY32_U1_12V_DIVIDER_DEN;
}

uint16_t adc_read_bat_raw(void)
{
  /* 使用硬件配置中的电池 ADC 通道，避免业务代码直接依赖通道号。 */
  return adc_convert(PIN_BAT_ADC_CHANNEL);
}

uint16_t adc_read_current_raw(void)
{
  /* 使用硬件配置中的电流 ADC 通道。 */
  return adc_convert(PIN_CURR_ADC_CHANNEL);
}

uint16_t adc_read_moto_raw(void)
{
  /* 使用硬件配置中的电机电压 ADC 通道。 */
  return adc_convert(PIN_MOTO_ADC_CHANNEL);
}

uint16_t adc_read_12v_raw(void)
{
  /* 使用硬件配置中的 12 V ADC 通道。 */
  return adc_convert(PIN_12V_ADC_CHANNEL);
}

uint16_t b_adc_read_raw(void)
{
  /* 兼容旧工程的电池原始值接口。 */
  return adc_read_bat_raw();
}

uint16_t bta_adc_read_raw(void)
{
  /* 兼容旧工程的电流原始值接口。 */
  return adc_read_current_raw();
}

uint16_t moto_adc_read_raw(void)
{
  /* 兼容旧工程的电机电压原始值接口。 */
  return adc_read_moto_raw();
}

uint16_t adc_12v_read_raw(void)
{
  /* 兼容旧工程的 12 V 原始值接口。 */
  return adc_read_12v_raw();
}

uint16_t b_adc_read_voltage(uint16_t vref_mv)
{
  /* 兼容旧工程：读取一次电池值并转换为 ADC 引脚电压。 */
  return adc_raw_to_mv(b_adc_read_raw(), vref_mv);
}

uint16_t bta_adc_read_voltage(uint16_t vref_mv)
{
  /* 兼容旧工程：读取一次电流值并转换为 ADC 引脚电压。 */
  return adc_raw_to_mv(bta_adc_read_raw(), vref_mv);
}

uint16_t moto_adc_read_voltage(uint16_t vref_mv)
{
  /* 兼容旧工程：读取一次电机通道并转换为 ADC 引脚电压。 */
  return adc_raw_to_mv(moto_adc_read_raw(), vref_mv);
}

uint16_t adc_12v_read_voltage(uint16_t vref_mv)
{
  /* 兼容旧工程：读取一次 12 V 通道并转换为 ADC 引脚电压。 */
  return adc_raw_to_mv(adc_12v_read_raw(), vref_mv);
}
