/**
  * @file    py32_u1_hw_config.h
  * @brief   U1 board GPIO and peripheral pin definitions.
  */

#ifndef __PY32_U1_HW_CONFIG_H
#define __PY32_U1_HW_CONFIG_H

#include "py32f002b_ll_gpio.h"
#include "py32f002b_hal_adc.h"
#include "py32f002b_hal_gpio_ex.h"

/* Physical GPIO enumeration for the PY32F002Bx5 package. */
#define PIN_PA0_PORT                 GPIOA
#define PIN_PA0_PIN                  LL_GPIO_PIN_0
#define PIN_PA1_PORT                 GPIOA
#define PIN_PA1_PIN                  LL_GPIO_PIN_1
#define PIN_PA2_PORT                 GPIOA
#define PIN_PA2_PIN                  LL_GPIO_PIN_2
#define PIN_PA3_PORT                 GPIOA
#define PIN_PA3_PIN                  LL_GPIO_PIN_3
#define PIN_PA4_PORT                 GPIOA
#define PIN_PA4_PIN                  LL_GPIO_PIN_4
#define PIN_PA5_PORT                 GPIOA
#define PIN_PA5_PIN                  LL_GPIO_PIN_5
#define PIN_PA6_PORT                 GPIOA
#define PIN_PA6_PIN                  LL_GPIO_PIN_6
#define PIN_PA7_PORT                 GPIOA
#define PIN_PA7_PIN                  LL_GPIO_PIN_7

#define PIN_PB0_PORT                 GPIOB
#define PIN_PB0_PIN                  LL_GPIO_PIN_0
#define PIN_PB1_PORT                 GPIOB
#define PIN_PB1_PIN                  LL_GPIO_PIN_1
#define PIN_PB2_PORT                 GPIOB
#define PIN_PB2_PIN                  LL_GPIO_PIN_2
#define PIN_PB3_PORT                 GPIOB
#define PIN_PB3_PIN                  LL_GPIO_PIN_3
#define PIN_PB4_PORT                 GPIOB
#define PIN_PB4_PIN                  LL_GPIO_PIN_4
#define PIN_PB5_PORT                 GPIOB
#define PIN_PB5_PIN                  LL_GPIO_PIN_5
#define PIN_PB6_PORT                 GPIOB
#define PIN_PB6_PIN                  LL_GPIO_PIN_6
#define PIN_PB7_PORT                 GPIOB
#define PIN_PB7_PIN                  LL_GPIO_PIN_7

#define PIN_PC0_PORT                 GPIOC
#define PIN_PC0_PIN                  LL_GPIO_PIN_0
#define PIN_PC1_PORT                 GPIOC
#define PIN_PC1_PIN                  LL_GPIO_PIN_1

/* U1 board signals. */
#define PIN_MOTO1_PORT               PIN_PA0_PORT
#define PIN_MOTO1_PIN                PIN_PA0_PIN
#define PIN_MOTO1_AF                 GPIO_AF2_TIM1

#define PIN_MOTO2_PORT               PIN_PA1_PORT
#define PIN_MOTO2_PIN                PIN_PA1_PIN
#define PIN_MOTO2_AF                 GPIO_AF2_TIM1

#define PIN_IP2326_EN_PORT           PIN_PA2_PORT
#define PIN_IP2326_EN_PIN            PIN_PA2_PIN
#define PIN_IP2326_EN_ACTIVE_LEVEL   1U

#define PIN_BAT_ADC_PORT             PIN_PA3_PORT
#define PIN_BAT_ADC_PIN              PIN_PA3_PIN
#define PIN_BAT_ADC_CHANNEL          ADC_CHANNEL_1

#define PIN_CURR_ADC_PORT            PIN_PA4_PORT
#define PIN_CURR_ADC_PIN             PIN_PA4_PIN
#define PIN_CURR_ADC_CHANNEL         ADC_CHANNEL_2

#define PIN_USBIN_PORT               PIN_PA5_PORT
#define PIN_USBIN_PIN                PIN_PA5_PIN
#define PIN_USBIN_ACTIVE_LEVEL       0U

#define PIN_DISP_TX_PORT             PIN_PA6_PORT
#define PIN_DISP_TX_PIN              PIN_PA6_PIN
#define PIN_DISP_TX_AF               GPIO_AF1_USART1

#define PIN_SENSOR_RX_PORT           PIN_PB5_PORT
#define PIN_SENSOR_RX_PIN            PIN_PB5_PIN

#define PIN_MOTO_ADC_PORT            PIN_PB0_PORT
#define PIN_MOTO_ADC_PIN             PIN_PB0_PIN
#define PIN_MOTO_ADC_CHANNEL         ADC_CHANNEL_7

#define PIN_12V_ADC_PORT             PIN_PB1_PORT
#define PIN_12V_ADC_PIN              PIN_PB1_PIN
#define PIN_12V_ADC_CHANNEL          ADC_CHANNEL_0

/* ADC conversion and board scaling parameters. */
#define PY32_U1_VREFINT_MV          1200U
#define PY32_U1_MOTO_DIVIDER_NUM    1U
#define PY32_U1_MOTO_DIVIDER_DEN    1U
#define PY32_U1_12V_DIVIDER_NUM     1U
#define PY32_U1_12V_DIVIDER_DEN     1U

/* PA4 电流检测的零电流基准电压和换算比例，必须按实际硬件校准。 */
#define PY32_U1_CURRENT_ZERO_MV     1650U
#define PY32_U1_CURRENT_MA_PER_MV   10U

/* PB2 and PB6 are currently reserved and have no board function. */

#define PIN_LED_PORT                 PIN_PB3_PORT
#define PIN_LED_PIN                  PIN_PB3_PIN

#define PIN_SENSOR_TX_PORT           PIN_PB4_PORT
#define PIN_SENSOR_TX_PIN            PIN_PB4_PIN

#define PIN_WKUP_PORT                PIN_PB7_PORT
#define PIN_WKUP_PIN                 PIN_PB7_PIN
#define PIN_WKUP_ACTIVE_LEVEL        1U

/* PC0 is NRST by default and requires Option Byte configuration before use. */
#define PIN_BAT_COM_PORT             PIN_PC0_PORT
#define PIN_BAT_COM_PIN              PIN_PC0_PIN

#define PIN_IP2326_LED_PORT          PIN_PC1_PORT
#define PIN_IP2326_LED_PIN           PIN_PC1_PIN

#endif /* __PY32_U1_HW_CONFIG_H */
