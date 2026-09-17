#include "key_driver.h"
#include "py32f002b_hal_gpio.h"

typedef struct
{
    /* 每个按键都保存独立的消抖、长按和待处理事件状态。 */
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t pressed;          /* 已确认的稳定状态：1=按下，0=释放。 */
    uint8_t debounce_ticks;  /* 输入变化后连续稳定的采样次数。 */
    uint16_t hold_ticks;     /* 当前连续按下的采样次数。 */
    uint8_t long_sent;       /* 是否已经发送过长按事件。 */
    uint32_t events;         /* 尚未被业务读取的事件位。 */
} KeyState_t;

/*
 * 按键硬件映射。按下按键时对应网络被拉低，输入上拉模式下读到 RESET。
 */
static KeyState_t s_keys[KEY_COUNT] =
{
    { LED_KEY_S1_PORT, LED_KEY_S1_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE },
    { LED_KEY_S2_PORT, LED_KEY_S2_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE },
    { LED_KEY_S3_PORT, LED_KEY_S3_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE },
    { LED_KEY_S4_PORT, LED_KEY_S4_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE },
    { LED_KEY_S5_PORT, LED_KEY_S5_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE },
    { LED_KEY_S6_PORT, LED_KEY_S6_PIN, 0U, 0U, 0U, 0U, KEY_EVENT_NONE }
};

static uint32_t s_last_scan_tick;

static void key_config_pin_input(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    /* 输入采样使用上拉，按键闭合后由外部开关把引脚拉到 GND。 */
    gpio.Pin = pin;
    /* 释放 LED 低边输出，使用内部上拉避免按键松开时输入悬空。 */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

static void key_config_pin_output(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    /* 输出模式不改变 ODR，保证切回 LED 驱动时不会产生额外闪烁。 */
    gpio.Pin = pin;
    /* 按键采样结束后恢复为 LED 低边推挽输出。 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

static void key_config_input(void)
{
    uint32_t i;

    /* 统一切换 6 个复用引脚，保证采样期间 LED 不再驱动这些线。 */
    for (i = 0U; i < KEY_COUNT; ++i)
    {
        key_config_pin_input(s_keys[i].port, s_keys[i].pin);
    }
}

static void key_config_output(void)
{
    uint32_t i;

    /* 采样完成后恢复 LED 输出模式；GPIO 输出寄存器原值不会被初始化覆盖。 */
    for (i = 0U; i < KEY_COUNT; ++i)
    {
        /* GPIO 初始化不会改变输出数据寄存器，原有显示缓存可以继续使用。 */
        key_config_pin_output(s_keys[i].port, s_keys[i].pin);
    }
}

void key_init(void)
{
    uint32_t i;

    /* 按键使用 PA/PB/PC 复用线，先打开对应端口时钟。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* 初始化事件状态，并记录时间基准，避免第一次调用立即误采样。 */
    s_last_scan_tick = HAL_GetTick();

    /* 首次启动时清空所有历史状态，避免误产生按下或长按事件。 */
    for (i = 0U; i < KEY_COUNT; ++i)
    {
        s_keys[i].pressed = 0U;
        s_keys[i].debounce_ticks = 0U;
        s_keys[i].hold_ticks = 0U;
        s_keys[i].long_sent = 0U;
        s_keys[i].events = KEY_EVENT_NONE;
    }

    /* 完成一次输入/输出切换，最终恢复到 LED 输出状态。 */
    key_config_input();
    key_config_output();
}

void key_scan(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t i;

    /* 主循环可以高频调用，只有达到采样周期才真正访问 GPIO。 */
    if ((uint32_t)(now - s_last_scan_tick) < KEY_SCAN_PERIOD_MS)
    {
        return;
    }
    s_last_scan_tick = now;

    /* 低边线与按键复用，采样前必须全部切换为输入上拉。 */
    key_config_input();

    for (i = 0U; i < KEY_COUNT; ++i)
    {
        KeyState_t *key = &s_keys[i];
        /* S3 使用 3 秒门限，其余按键沿用默认长按门限。 */
        uint16_t long_press_ticks = (i == KEY_S3) ?
                                    KEY_S3_LONG_PRESS_TICKS :
                                    KEY_LONG_PRESS_TICKS;
        /* 低电平有效，先转换成统一的 1=按下、0=释放。 */
        uint8_t raw_pressed =
            (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET) ? 1U : 0U;

        /*
         * 只有同一个电平连续稳定达到消抖次数，才更新 pressed 状态。
         * 这样可以过滤机械按键在按下/释放瞬间产生的抖动。
         */
        if (raw_pressed != key->pressed)
        {
            /* 输入变化后累计稳定次数，防止计数器溢出。 */
            if (key->debounce_ticks < KEY_DEBOUNCE_TICKS)
            {
                ++key->debounce_ticks;
            }

            if (key->debounce_ticks >= KEY_DEBOUNCE_TICKS)
            {
                /* 达到消抖门限，正式提交新的稳定状态。 */
                key->pressed = raw_pressed;
                key->debounce_ticks = 0U;

                if (key->pressed != 0U)
                {
                    /* 确认按下：报告 PRESS，并开始计算长按时间。 */
                    key->hold_ticks = 0U;
                    key->long_sent = 0U;
                    key->events |= KEY_EVENT_PRESS;
                }
                else
                {
                    /*
                     * 释放时根据是否已经触发长按，区分短按释放和长按释放。
                     * S1、S3 支持长按；其他按键只会产生短按释放事件。
                     */
                    key->events |= (((i == KEY_S1) || (i == KEY_S3)) &&
                                    (key->long_sent != 0U)) ?
                                   KEY_EVENT_LONG_RELEASE : KEY_EVENT_SHORT_RELEASE;
                    key->hold_ticks = 0U;
                    key->long_sent = 0U;
                }
            }
        }
        else
        {
            key->debounce_ticks = 0U;
        }

        if (key->pressed != 0U)
        {
            /* 连续按下期间累计保持时间，饱和到 16 位最大值。 */
            if (key->hold_ticks < 0xFFFFU)
            {
                ++key->hold_ticks;
            }

            /* S3 的 3 秒长按用于恢复编程模式，S1 保留原有长按功能。 */
            if (((i == KEY_S1) || (i == KEY_S3)) &&
                (key->long_sent == 0U) &&
                (key->hold_ticks >= long_press_ticks))
            {
                /* 长按只上报一次，直到按键释放后才允许下一次触发。 */
                key->long_sent = 1U;
                key->events |= KEY_EVENT_LONG_PRESS;
            }
            /* 只有 S1 需要周期性的 LONG_HOLD 事件，S3 不重复上报。 */
            else if ((i == KEY_S1) && (key->long_sent != 0U) &&
                     (KEY_LONG_HOLD_TICKS != 0U) &&
                     ((key->hold_ticks % KEY_LONG_HOLD_TICKS) == 0U))
            {
                key->events |= KEY_EVENT_LONG_HOLD;
            }
        }
    }

    /* 恢复 LED 低边输出模式，且不改动之前保存的输出电平。 */
    key_config_output();
}

uint32_t key_get_event(KeyId_t key)
{
    uint32_t events;

    /* 非法索引直接返回无事件，避免访问数组边界之外的状态。 */
    if (key >= KEY_COUNT)
    {
        return KEY_EVENT_NONE;
    }

    /* 先保存事件，再清零，实现“一次读取、一次消费”。 */
    events = s_keys[key].events;
    s_keys[key].events = KEY_EVENT_NONE;
    return events;
}

uint32_t key_peek_event(KeyId_t key)
{
    /* 非法索引直接返回无事件，避免访问数组边界之外的状态。 */
    if (key >= KEY_COUNT)
    {
        return KEY_EVENT_NONE;
    }

    /* 只读取事件，不修改缓存，允许其他模块随后继续消费该事件。 */
    return s_keys[key].events;
}

uint8_t key_is_pressed(KeyId_t key)
{
    /* 查询接口不清除事件，只返回当前已经确认的稳定电平。 */
    if (key >= KEY_COUNT)
    {
        return 0U;
    }

    return s_keys[key].pressed;
}
