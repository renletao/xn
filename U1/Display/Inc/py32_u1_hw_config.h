#ifndef PY32_U1_HW_CONFIG_H
#define PY32_U1_HW_CONFIG_H

#include "main.h"

/*
 * PY32 hardware mapping extracted from the schematic net names.
 * Include the PY32 HAL header before using these definitions so that
 * GPIOA/GPIOB/GPIOC and GPIO_PIN_x are available.
 */

/* Schematic net -> PY32 GPIO */
#define P14_GPIO_PORT        GPIOA
#define P14_GPIO_PIN         GPIO_PIN_5

#define P13_GPIO_PORT        GPIOA
#define P13_GPIO_PIN         GPIO_PIN_6

#define P11_GPIO_PORT        GPIOA
#define P11_GPIO_PIN         GPIO_PIN_7

/* P15 is marked PC0(NRST) in the supplied mapping. */
#define P15_GPIO_PORT        GPIOC       //RST
#define P15_GPIO_PIN         GPIO_PIN_0


#define P12_GPIO_PORT        GPIOC
#define P12_GPIO_PIN         GPIO_PIN_1

#define P05_GPIO_PORT        GPIOB
#define P05_GPIO_PIN         GPIO_PIN_7  ///1111111111待确认

#define P01_GPIO_PORT        GPIOB       //SWD
#define P01_GPIO_PIN         GPIO_PIN_6

#define WK_UP_GPIO_PORT      GPIOB
#define WK_UP_GPIO_PIN       GPIO_PIN_5

#define P08_GPIO_PORT        GPIOA
#define P08_GPIO_PIN         GPIO_PIN_4

#define P09_GPIO_PORT        GPIOA
#define P09_GPIO_PIN         GPIO_PIN_3

#define P07_GPIO_PORT        GPIOA     //SWC
#define P07_GPIO_PIN         GPIO_PIN_2

#define P06_GPIO_PORT        GPIOA
#define P06_GPIO_PIN         GPIO_PIN_1

#define P00_GPIO_PORT        GPIOA
#define P00_GPIO_PIN         GPIO_PIN_0

#define P10_GPIO_PORT        GPIOB
#define P10_GPIO_PIN         GPIO_PIN_0

#define P02_GPIO_PORT        GPIOB
#define P02_GPIO_PIN         GPIO_PIN_1

#define P03_GPIO_PORT        GPIOB
#define P03_GPIO_PIN         GPIO_PIN_3

#define P04_GPIO_PORT        GPIOB
#define P04_GPIO_PIN         GPIO_PIN_2

/*
 * RX/TX is assigned to PB2.
 */
#define RX_TX_GPIO_PORT      GPIOB
#define RX_TX_GPIO_PIN       GPIO_PIN_2

/* Optional generic helpers for the PY32 HAL. */
#define HW_GPIO_WRITE(port, pin, state) HAL_GPIO_WritePin((port), (pin), (state))
#define HW_GPIO_READ(port, pin)         HAL_GPIO_ReadPin((port), (pin))

/* LED matrix high-side control nets driven through SS8550 bases. */
#define LED_HIGH_Q1_PORT     P01_GPIO_PORT
#define LED_HIGH_Q1_PIN      P01_GPIO_PIN
#define LED_HIGH_Q2_PORT     P06_GPIO_PORT
#define LED_HIGH_Q2_PIN      P06_GPIO_PIN
#define LED_HIGH_Q3_PORT     P07_GPIO_PORT
#define LED_HIGH_Q3_PIN      P07_GPIO_PIN
#define LED_HIGH_Q4_PORT     P08_GPIO_PORT
#define LED_HIGH_Q4_PIN      P08_GPIO_PIN
#define LED_HIGH_Q5_PORT     P09_GPIO_PORT
#define LED_HIGH_Q5_PIN      P09_GPIO_PIN
#define LED_HIGH_Q6_PORT     P10_GPIO_PORT
#define LED_HIGH_Q6_PIN      P10_GPIO_PIN
#define LED_HIGH_Q7_PORT     P11_GPIO_PORT
#define LED_HIGH_Q7_PIN      P11_GPIO_PIN
#define LED_HIGH_Q8_PORT     P15_GPIO_PORT
#define LED_HIGH_Q8_PIN      P15_GPIO_PIN

/* LED/key shared low-side nets. */
#define LED_KEY_S1_PORT      P00_GPIO_PORT
#define LED_KEY_S1_PIN       P00_GPIO_PIN
#define LED_KEY_S2_PORT      P12_GPIO_PORT
#define LED_KEY_S2_PIN       P12_GPIO_PIN
#define LED_KEY_S3_PORT      GPIOB
#define LED_KEY_S3_PIN       GPIO_PIN_7
#define LED_KEY_S4_PORT      P04_GPIO_PORT
#define LED_KEY_S4_PIN       P04_GPIO_PIN
#define LED_KEY_S5_PORT      P02_GPIO_PORT
#define LED_KEY_S5_PIN       P02_GPIO_PIN
#define LED_KEY_S6_PORT      P03_GPIO_PORT
#define LED_KEY_S6_PIN       P03_GPIO_PIN

#endif /* PY32_U1_HW_CONFIG_H */
