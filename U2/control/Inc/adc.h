/**
  * @file    adc.h
  * @brief   U1 板四路 ADC 初始化、采样和电压/电流换算接口。
  *
  * ADC 通道分配如下：PA3 为电池电压，PA4 为电流检测，PB0 为电机电压，
  * PB1 为 12 V 电压。adc_task_100ms() 会每 100 ms 更新一次全局结果结构体。
  */

#ifndef __U1_ADC_H
#define __U1_ADC_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_U1_MAX_VALUE             4095U

typedef struct
{
  uint16_t raw;       /* ADC 原始转换值，12 位范围为 0~4095。 */
  uint16_t pin_mv;    /* 按实际 VREF 换算后的 MCU ADC 引脚电压，单位 mV。 */
} ADC_ChannelValueTypeDef;

typedef struct
{
  uint16_t vref_mv;   /* 本次采样使用的实际参考电压，单位 mV。 */
  ADC_ChannelValueTypeDef battery;
  ADC_ChannelValueTypeDef current;
  ADC_ChannelValueTypeDef motor;
  ADC_ChannelValueTypeDef input_12v;
  uint32_t battery_mv;  /* 根据电阻分压还原后的电池电压，单位 mV。 */
  int32_t current_ma;   /* PA4 换算后的有符号电流，单位 mA；负值表示充电。 */
  uint32_t motor_mv;    /* 根据配置的分压比例还原后的电机电压，单位 mV。 */
  uint32_t input_12v_mv;/* 根据配置的分压比例还原后的 12 V 电压，单位 mV。 */
} ADC_U1_DataTypeDef;

extern volatile ADC_U1_DataTypeDef g_adc_data;

/** 初始化 ADC、模拟输入引脚，并执行一次 ADC 校准。 */
void adc_init(void);
void adc_suspend(void);
void adc_resume(void);
/** 周期任务入口；函数可在主循环中高频调用，内部自动限制为 100 ms 执行一次。 */
void adc_task_100ms(void);
/** 读取指定 ADC 通道的一次原始转换结果。 */
uint16_t adc_read_channel_raw(uint32_t channel);
/** 将 ADC 原始值按实际参考电压换算为引脚电压，单位 mV。 */
uint16_t adc_raw_to_mv(uint16_t raw, uint16_t vref_mv);
/** 读取内部参考源并估算当前实际 VREF，单位 mV。 */
uint16_t get_vref(void);

/** 读取电池电压通道的原始 ADC 值。 */
uint16_t adc_read_bat_raw(void);
/** 读取电流检测通道的原始 ADC 值。 */
uint16_t adc_read_current_raw(void);
/** 读取电机电压通道的原始 ADC 值。 */
uint16_t adc_read_moto_raw(void);
/** 读取 12 V 电压通道的原始 ADC 值。 */
uint16_t adc_read_12v_raw(void);

/* 与参考 CMake 工程兼容的旧接口名称。 */
uint16_t b_adc_read_raw(void);
uint16_t bta_adc_read_raw(void);
uint16_t moto_adc_read_raw(void);
uint16_t adc_12v_read_raw(void);
uint16_t b_adc_read_voltage(uint16_t vref_mv);
uint16_t bta_adc_read_voltage(uint16_t vref_mv);
uint16_t moto_adc_read_voltage(uint16_t vref_mv);
uint16_t adc_12v_read_voltage(uint16_t vref_mv);

#ifdef __cplusplus
}
#endif

#endif /* __U1_ADC_H */
