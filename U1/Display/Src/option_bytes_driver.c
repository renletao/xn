#include "option_bytes_driver.h"
#include "main.h"
#include "py32f0xx_hal.h"
#include "py32_u1_hw_config.h"
#include "key_driver.h"

#define OPTION_BYTES_PROGRAMMING_WINDOW_MS  5000U

/*
 * 根据目标模式更新 Option Byte。
 *
 * PY32F002B 的 SWD/NRST 复用由用户 Option Byte 配置决定。
 * 写入前必须先解锁 Flash 和 Option Byte，写入完成后重新加锁，
 * 最后调用 HAL_FLASH_OB_Launch() 让新配置生效。该操作会引起系统复位。
 *
 * 该函数只在本文件内部使用，外部统一通过 boot_check()/scan() 调用，
 * 这样可以避免在业务代码中到处直接操作 Flash 配置。
 */
static void option_bytes_configure_if_needed(uint32_t desired_mode)
{
    FLASH_OBProgramInitTypeDef ob = {0};

    /*
     * 通过 HAL 读取当前 Option Byte 的 SWD/NRST 配置并比较目标值。
     * 模式一致时直接返回，避免每次上电都重复擦写 Option Byte。
     */
    HAL_FLASH_OBGetConfig(&ob);
    if ((ob.USERConfig & OB_USER_SWD_NRST_MODE) == desired_mode)
    {
        /* 当前复用模式已经满足要求，不进入解锁和写入流程。 */
        return;
    }

    /* Flash 和 Option Byte 必须分别解锁，任一步失败都停止在错误处理函数。 */
    if (HAL_FLASH_Unlock() != HAL_OK || HAL_FLASH_OB_Unlock() != HAL_OK)
    {
        app_error_handler();
    }

    /* 只修改用户 Option Byte 中的 SWD/NRST 复用字段。 */
    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType = OB_USER_SWD_NRST_MODE;
    ob.USERConfig = desired_mode;

    /* 执行实际写入；失败时先恢复锁定状态，再进入错误处理。 */
    if (HAL_FLASH_OBProgram(&ob) != HAL_OK)
    {
        (void)HAL_FLASH_OB_Lock();
        (void)HAL_FLASH_Lock();
        app_error_handler();
    }

    /* 无论后续是否复位，都先锁回 Option Byte 和 Flash。 */
    (void)HAL_FLASH_OB_Lock();
    (void)HAL_FLASH_Lock();

    /* 重新加载 Option Byte 会触发系统复位，正常情况下不会返回。 */
    /*
     * 重新加载 Option Byte。芯片会在这里执行系统复位，
     * 因此下面的死循环只是防止编译器认为函数可能继续执行。
     */
    (void)HAL_FLASH_OB_Launch();
    while (1)
    {
    }
}

static uint8_t option_bytes_is_s1_held(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * S1 与 LED 低边线 P00/PA0 复用。
     * 上电初始化 LED 之前，先临时把该引脚配置为内部上拉输入，
     * 按键按下时被拉到低电平，因此读到 RESET 表示 S1 被按住。
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = P00_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(P00_GPIO_PORT, &gpio);

    /* 低电平表示按键闭合，转换成统一的 1=按下、0=释放返回值。 */
    return (HAL_GPIO_ReadPin(P00_GPIO_PORT, P00_GPIO_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

void option_bytes_boot_check(void)
{
    FLASH_OBProgramInitTypeDef ob = {0};

    /*
     * 正常显示配置已经生效时直接启动，不增加固定的上电延时。
     * 只有 PB6 仍为 SWD（或配置为其他非显示模式）时，才为烧录器
     * 保留 5 秒连接窗口。窗口结束后按住 S1 可继续保留当前配置，
     * 否则切回 PC0=SWD、PB6=GPIO 的正常显示配置。
     */
    HAL_FLASH_OBGetConfig(&ob);
    if ((ob.USERConfig & OB_USER_SWD_NRST_MODE) == OB_SWD_PC0_GPIO_PB6)
    {
        return;
    }

    HAL_Delay(OPTION_BYTES_PROGRAMMING_WINDOW_MS);

    if (option_bytes_is_s1_held() == 0U)
    {
        option_bytes_configure_if_needed(OB_SWD_PC0_GPIO_PB6);
    }
}

void option_bytes_scan(void)
{
    /*
     * key_scan() 已经负责消抖和 3 秒长按计时，这里只消费 S3 的事件。
     * 短按事件不会触发 Option Byte 恢复，保留给其他业务使用。
     */
    /* 只有收到一次长按事件才执行，避免在按住期间重复写入。 */
    if ((key_get_event(KEY_S3) & KEY_EVENT_LONG_PRESS) != 0U)
    {
        option_bytes_configure_if_needed(OB_SWD_PB6_GPIO_PC0);
    }
}
