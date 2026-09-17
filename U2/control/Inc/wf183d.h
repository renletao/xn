/**
  * @file    wf183d.h
  * @brief   WF183D pressure sensor protocol driver.
  *
  * The sensor is accessed through the PB4/PB5 software UART. The driver
  * performs one temperature conversion before each pressure conversion, as
  * required by the sensor protocol, but exposes only the pressure result.
  */
#ifndef __U2_WF183D_H
#define __U2_WF183D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WF183D 已接入，启用 PB4/PB5 软件串口的真实温度/气压查询。 */
#define WF183D_USE_REAL_SENSOR       1U

#define WF183D_CMD_HEAD                 0x55U
#define WF183D_RET_HEAD                 0xAAU
#define WF183D_CMD_TEMPERATURE          0x0EU
#define WF183D_CMD_PRESSURE             0x0DU
#define WF183D_RET_TEMPERATURE          0x0AU
#define WF183D_RET_PRESSURE             0x09U

#define WF183D_RESPONSE_TIMEOUT_MS      100U
#define WF183D_SAMPLE_PERIOD_MS         1000U
#define WF183D_TARE_SAMPLE_COUNT        10U

typedef enum {
  WF183D_STATE_OK = 0U,
  WF183D_STATE_TIMEOUT,
  WF183D_STATE_FRAME_ERROR,
  WF183D_STATE_CRC_ERROR,
  WF183D_STATE_UART_ERROR
} WF183D_StateTypeDef;

/* Latest valid pressure stored with the tare offset applied, in Pa. */
extern volatile uint32_t g_wf183d_pressure_pa;
/* Startup tare value: the average of ten samples after removing max/min, in Pa. */
extern volatile uint32_t g_wf183d_pressure_tare_pa;
extern volatile uint8_t  g_wf183d_state;
extern volatile uint8_t  g_wf183d_fault_count;

void wf183d_init(void);
/* Perform the ten-sample startup tare before normal application tasks run. */
void wf183d_startup_tare(void);
void wf183d_task(void);
/* Store a user pressure value after adding the startup tare offset. */
void wf183d_set_pressure_pa(uint32_t pressure_pa);
/* Return the stored pressure after removing the startup tare offset. */
uint32_t wf183d_get_pressure_pa(void);
/* Convert a user-domain pressure to the internally stored tare-added value. */
uint32_t wf183d_add_tare_pa(uint32_t pressure_pa);
/* Return 1 after ten valid startup samples have established the tare value. */
uint8_t wf183d_is_tare_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __U2_WF183D_H */
