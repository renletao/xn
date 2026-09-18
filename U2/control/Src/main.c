#include "main.h"      /* 芯片 LL 头文件、错误处理声明 */
#include "adc.h"
#include "charge.h"
#include "ip2326.h"
#include "led.h"
#include "moto.h"
#include "soft_uart.h"
#include "wf183d.h"
#include "uart_command_driver.h"
#include "usbin.h"
#include "power_manager.h"

/* 主时钟配置只在 main() 中调用一次，因此定义为本文件私有函数。 */
void APP_SystemClockConfig(void);

int main(void)
{
  /*
   * 系统启动顺序说明：先准备时钟，再初始化各个外设驱动。
   * 其中 USART、PWM、ADC 和软件串口的时序参数都依赖最终的 24 MHz 主频，
   * 因此所有驱动初始化必须放在 APP_SystemClockConfig() 之后执行。
   */

  /* 切换到 24 MHz HSI，并建立 1 ms SysTick 时间基准。 */
  APP_SystemClockConfig();

  /* 初始化 PB3/PC1 两路 LED，默认输出关闭电平，防止上电误亮。 */
  led_init();

  /* 初始化 PA2 快充芯片使能，默认关闭 IP2326。 */
  ip2326_init();

  /* 初始化 PA5 USB 插入输入，并让 USB 状态经过消抖后控制 IP2326。 */
  usbin_init();

  /* 初始化 PB4/PB5 上的 9600 8N1 软件串口和 TIM14 微秒时基。 */
  soft_uart_init();
  /* 初始化 WF183D 协议状态。 */
  wf183d_init();
  /* 上电先连续完成十次有效采样去皮，再开放 Display 控制业务。 */
  wf183d_startup_tare();

  /* 按 Display 最新框架初始化 PA6 半双工、AA55 协议和命令分发层。 */
  uart_command_init();

  /* 初始化 TIM1 两路压力泵 PWM；驱动完成后两路占空比均为 0。 */
  moto_init();

  /* 初始化四路 ADC 并执行校准，准备后续电压、电流周期采样。 */
  adc_init();

  /* 初始化充电策略：默认允许充电，但 USB 未插入时保持 IP2326 关闭。 */
  charge_init();
  /* 进入主循环前先完成 128 个原始电池采样，建立初始 BatLevel。 */
  adc_battery_startup_sample(0U, 0U);
  power_manager_init();

  while (1)
  {
    /*
     * 主循环采用轮询调度，不在这里直接阻塞等待固定周期。
     * 每个任务内部根据 HAL_GetTick() 判断是否到了自己的执行时间，
     * 这样 USB 检测可以持续消抖，电池采样保持 10 ms、其他工程量采样保持 100 ms。
    */
    /* 持续检测 USBIN；电平稳定 20 ms 后才更新 IP2326_EN。 */
    usbin_task();

    /* 电池和充电电流以 100 Hz 采样，内部完成 2 点/64 点平均。 */
    adc_battery_task_100hz(usbin_is_inserted(), g_uart_pump_running);

    /* 电机电压和 12 V 电压保持 100 ms 的低速采样。 */
    adc_task_100ms();

    /* 先更新充电和保护状态，再接受新的电机命令。 */
    charge_task();

    /* 接收 U1 发来的 AA55 控制帧，处理状态查询和压力泵控制。 */
    uart_command_task();

    /* WF183D 已完成上电去皮：每秒查询并更新内部传感器域气压。 */
#if WF183D_USE_REAL_SENSOR
    wf183d_task();
#endif

    /* 按模式、实时气压和目标气压选择压力泵或停止输出。 */
    uart_command_pump_task();

    power_manager_task();
  }
}

void APP_SystemClockConfig(void)
{
  /* 打开内部高速时钟 HSI，并等待硬件报告稳定。 */
  LL_RCC_HSI_Enable();
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  /* AHB 不分频，系统时钟直接作为 HCLK 使用。 */
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);

  /* 选择 HSI 系统时钟，并等待切换真正完成。 */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS)
  {
  }

  /* APB1 不分频，外设总线同样工作在 24 MHz。 */
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);

  /* 告诉 LL/CMSIS 当前频率，供串口波特率和其他库函数计算使用。 */
  LL_Init1msTick(24000000);
  LL_SetSystemCoreClock(24000000);

  /* LL_Init1msTick 默认只启动计数器；打开中断后每 1 ms 进入 SysTick_Handler。 */
  SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

void APP_ErrorHandler(void)
{
  /* 当前工程没有错误恢复策略，进入死循环方便调试器定位。 */
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* 开启 USE_FULL_ASSERT 后，参数检查失败会停在这里。 */
  while (1)
  {
  }
}
#endif
