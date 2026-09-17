#include "main.h"
#include "key_driver.h"
#include "led_driver.h"
#include "display_control_driver.h"
#include "option_bytes_driver.h"
#include "uart_command_driver.h"

static void app_system_clock_config(void);

int main(void)
{
    /* 复位外设状态并初始化 HAL 的 1 ms SysTick 时间基准。 */
    if (HAL_Init() != HAL_OK)
    {
        app_error_handler();
    }

    /* 使用 HAL 配置 24 MHz HSI 系统时钟。 */
    app_system_clock_config();

    /* 上电稳定等待，避免刚复位时立即切换复用引脚。 */
    HAL_Delay(5000U);

    /* 读取并比较 Option Byte，仅在当前模式不一致时才更新。 */
    option_bytes_boot_check();

    /* 初始化按键、LED 和显示控制模块。 */
    key_init();  //不可以注释掉！！！！！！！！！！！！！！！！！！！！
    led_init();
    display_control_init();
    /* 初始化 PB4 单线半双工串口，主控默认处于接收方向。 */
    /* 统一初始化PB4半双工、AA55协议和通信事务锁。 */
    uart_control_init();
		
		
// led_all_off();
// led_set_pixel(0,4,1);
// led_all_on();

// battery_icon_set_level(1);
// battery_icon_set_charging(1);
// pressure_display_set_actual(259);
// pressure_display_set_target(999);

    while (1)
    {
        /* 按键扫描会暂时切换 LED 复用引脚为输入，完成后恢复输出。 */
        key_scan();
        /* 按 1 ms 时隙刷新 LED 矩阵，形成稳定的动态显示。 */
        led_scan();
        /* 分发按键事件，并推进需要定时更新的显示状态。 */
        display_control_scan();//不可以注释掉！！！！！！！！！！！！！！！！！！！！！
        /* 非阻塞处理完整返回帧和100ms超时；事务锁期间禁止新发送。 */
        uart_control_task();
        /* 每秒发起一次充电状态、电量、气压和压力泵状态查询；不阻塞主循环。 */
        uart_command_scan();
    }
}

static void app_system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    /* 使用芯片内部 24 MHz HSI，不对 HSI 进行分频。 */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSIDiv = RCC_HSI_DIV1;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        app_error_handler();
    }

    /* SYSCLK、AHB 和 APB1 均为 24 MHz，Flash 使用 0 等待周期。 */
    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) != HAL_OK)
    {
        app_error_handler();
    }
}

void app_error_handler(void)
{
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
