#include "led_driver.h"
#include "mode_led_driver.h"
#include "unit_led_driver.h"
#include "dc_led_driver.h"

typedef struct
{
    /* 高边或低边 GPIO 的端口和引脚。 */
    GPIO_TypeDef *port;
    uint16_t pin;
} LED_Pin_t;

/*
 * 高边公共端映射：数组下标 0~7 对应 Q1~Q8。
 * 高边使用 PNP 管，输出低电平时选通，输出高电平时关闭。
 */
static const LED_Pin_t s_high_pins[LED_MATRIX_ROWS] =
{
    { LED_HIGH_Q1_PORT, LED_HIGH_Q1_PIN },
    { LED_HIGH_Q2_PORT, LED_HIGH_Q2_PIN },
    { LED_HIGH_Q3_PORT, LED_HIGH_Q3_PIN },
    { LED_HIGH_Q4_PORT, LED_HIGH_Q4_PIN },
    { LED_HIGH_Q5_PORT, LED_HIGH_Q5_PIN },
    { LED_HIGH_Q6_PORT, LED_HIGH_Q6_PIN },
    { LED_HIGH_Q7_PORT, LED_HIGH_Q7_PIN },
    { LED_HIGH_Q8_PORT, LED_HIGH_Q8_PIN }
};

static const LED_Pin_t s_low_pins[LED_MATRIX_COLS] =
{
    { LED_KEY_S1_PORT, LED_KEY_S1_PIN },
    { LED_KEY_S5_PORT, LED_KEY_S5_PIN },
    { LED_KEY_S6_PORT, LED_KEY_S6_PIN },
    { LED_KEY_S4_PORT, LED_KEY_S4_PIN },
    { LED_KEY_S3_PORT, LED_KEY_S3_PIN },
    { LED_KEY_S2_PORT, LED_KEY_S2_PIN },
    { P13_GPIO_PORT, P13_GPIO_PIN },
    { P14_GPIO_PORT, P14_GPIO_PIN }
};

/* 显示缓存。每一位对应一条低边线，bit=1 表示该行的 LED 点亮。 */
static uint8_t s_frame[LED_MATRIX_ROWS];
/* 当前扫描时隙：0~7 为 Q1~Q8，8 为模式/DC，9 为单位指示。 */
static uint8_t s_scan_row;
static uint32_t s_last_scan_tick;
/* 显示总开关；关闭时保留缓存，但所有高低边均输出关闭电平。 */
static uint8_t s_display_enabled;

static void led_config_pin_output(const LED_Pin_t *pin)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 所有矩阵线都配置为普通推挽输出，不使用上下拉。
     * 具体的有效电平由 led_set_low_side() 和扫描函数决定，
     * 这里仅负责建立稳定的 GPIO 工作模式。
     */
    gpio.Pin = pin->pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pin->port, &gpio);
}

static void led_all_high_off(void)
{
    uint32_t i;

    /*
     * PNP 高边管低电平导通，因此关闭高边要输出高电平。
     * 切换行时必须先关闭全部 8 路，避免两路公共端同时导通。
     */
    for (i = 0U; i < LED_MATRIX_ROWS; ++i)
    {
        HAL_GPIO_WritePin(s_high_pins[i].port, s_high_pins[i].pin, GPIO_PIN_SET);
    }
}

static void led_all_low_off(void)
{
    uint32_t i;

    /* 低边为低有效，全部输出高电平即可关闭所有 LED。 */
    for (i = 0U; i < LED_MATRIX_COLS; ++i)
    {
        HAL_GPIO_WritePin(s_low_pins[i].port, s_low_pins[i].pin, GPIO_PIN_SET);
    }
}

static void led_set_low_side(uint8_t row)
{
    uint32_t col;
    uint8_t row_data = s_frame[row];

    /*
     * 低边为低有效：缓存 bit=1 时输出 RESET 吸电流，
     * 缓存 bit=0 时输出 SET，避免未选中的 LED 串光。
     */
    /* row 在调用前已经由 led_scan() 限制为 0~7。 */
    for (col = 0U; col < LED_MATRIX_COLS; ++col)
    {
        GPIO_PinState state = ((row_data & (uint8_t)(1U << col)) != 0U) ?
                              GPIO_PIN_RESET : GPIO_PIN_SET;
        HAL_GPIO_WritePin(s_low_pins[col].port, s_low_pins[col].pin, state);
    }
}

static void led_set_mode_side(void)
{
    uint32_t col;
    uint8_t mode_mask = mode_led_get_mask();
    uint8_t dc_on = dc_led_is_charging();

    /*
     * Q1/L01 的额外扫描时隙同时承载模式灯和 DC 灯：
     * L00~L03 中最多点亮一个模式灯，L04/L05 为充电 DC 指示灯。
     */
    /* 每次先把 8 条低边全部置为关闭，再按状态选择需要拉低的线。 */
    for (col = 0U; col < LED_MATRIX_COLS; ++col)
    {
        GPIO_PinState state = GPIO_PIN_SET;

        if ((col < MODE_LED_COUNT) &&
            ((mode_mask & (uint8_t)(1U << col)) != 0U))
        {
            state = GPIO_PIN_RESET;
        }
        else if ((dc_on != 0U) && ((col == 3U) || (col == 4U)))
        {
            /* L04/L01 和 L05/L01 是两只 DC 充电指示灯。 */
            state = GPIO_PIN_RESET;
        }
        HAL_GPIO_WritePin(s_low_pins[col].port, s_low_pins[col].pin, state);
    }
}

static void led_set_unit_side(void)
{
    uint32_t col;
    uint8_t unit = unit_led_get_index();
    /*
     * 单位灯的公共端通常使用 Q1/L01；最后一个 L14-L08
     * 使用 Q4/P08，因此需要同时选择低边和对应高边。
     */
    uint8_t cathode_col = (unit == 0U) ? 5U :
                          (unit == 1U) ? 6U : 7U;

    /* 先关闭所有低边，只保留当前单位灯对应的低边为有效电平。 */
    for (col = 0U; col < LED_MATRIX_COLS; ++col)
    {
        HAL_GPIO_WritePin(s_low_pins[col].port,
                          s_low_pins[col].pin,
                          (col == cathode_col) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }

    /* 前三个单位灯使用 L01，最后一个 L14-L08 使用 Q4/P08。 */
    HAL_GPIO_WritePin(s_high_pins[(unit == 3U) ? 3U : 0U].port,
                      s_high_pins[(unit == 3U) ? 3U : 0U].pin,
                      GPIO_PIN_RESET);
}

void led_init(void)
{
    uint32_t i;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 打开 GPIO 时钟后，将所有高边、低边初始化为推挽输出。 */
    for (i = 0U; i < LED_MATRIX_ROWS; ++i)
    {
        led_config_pin_output(&s_high_pins[i]);
    }
    for (i = 0U; i < LED_MATRIX_COLS; ++i)
    {
        led_config_pin_output(&s_low_pins[i]);
    }

    /* 上电先清空缓存并关闭所有高边，避免初始化瞬间误亮。 */
    led_clear();
    led_all_high_off();
    led_all_low_off();
    /* 上电默认允许显示。 */
    s_display_enabled = 1U;
    /* 从 Q1 开始扫描，并以当前 HAL tick 作为第一个时间基准。 */
    s_scan_row = 0U;
    s_last_scan_tick = HAL_GetTick();
}

void led_scan(void)
{
    uint32_t now = HAL_GetTick();

    /* 使用无符号差值，兼容 HAL tick 溢出后的时间比较。 */
    if ((uint32_t)(now - s_last_scan_tick) < LED_SCAN_STEP_MS)
    {
        return;
    }
    s_last_scan_tick = now;

    /* 显示关闭时保持所有输出关闭，不执行正常矩阵扫描。 */
    if (s_display_enabled == 0U)
    {
        led_all_high_off();
        led_all_low_off();
        return;
    }

    /*
     * 每次切换前先关闭全部高边，再更新低边数据，最后打开目标高边。
     * 这个顺序可以避免高边切换期间出现串光和鬼影。
     */
    led_all_high_off();
    if (s_scan_row < LED_MATRIX_ROWS)
    {
        /* 普通矩阵行：输出该行缓存并打开对应的 Q1~Q8 高边。 */
        led_set_low_side(s_scan_row);
        HAL_GPIO_WritePin(s_high_pins[s_scan_row].port,
                          s_high_pins[s_scan_row].pin,
                          GPIO_PIN_RESET);
    }
    else if (s_scan_row == LED_MATRIX_ROWS)
    {
        /* 辅助时隙 8：Q1/L01 驱动模式灯和 DC 充电灯。 */
        led_set_mode_side();
        HAL_GPIO_WritePin(s_high_pins[0].port,
                          s_high_pins[0].pin,
                          GPIO_PIN_RESET);
    }
    else
    {
        /* 辅助时隙 9：驱动当前选中的单位灯。 */
        led_set_unit_side();
    }

    /* 8 行矩阵 + 2 个辅助时隙组成完整扫描周期。 */
    ++s_scan_row;
    if (s_scan_row > (LED_MATRIX_ROWS + 1U))
    {
        s_scan_row = 0U;
    }
}

void led_set_enabled(uint8_t enabled)
{
    /* 统一保存为 0/1，避免调用方传入其他数值造成状态不明确。 */
    s_display_enabled = (enabled != 0U) ? 1U : 0U;

    if (s_display_enabled == 0U)
    {
        /* 立即关闭输出，不必等待下一次 1 ms 扫描时隙。 */
        led_all_high_off();
        led_all_low_off();
    }
}

uint8_t led_is_enabled(void)
{
    /* 返回当前显示总开关状态，供按键控制或其他业务查询。 */
    return s_display_enabled;
}

void led_clear(void)
{
    uint32_t i;

    /* 清除的是缓存，不直接改 GPIO；实际熄灭在下一次扫描时生效。 */
    for (i = 0U; i < LED_MATRIX_ROWS; ++i)
    {
        s_frame[i] = 0U;
    }
}

void led_all_on(void)
{
    uint32_t i;

    /* 将每一行的 8 个 bit 全部置 1，用于逐行检查矩阵连线。 */
    for (i = 0U; i < LED_MATRIX_ROWS; ++i)
    {
        s_frame[i] = 0xFFU;
    }
}

void led_all_off(void)
{
    /* 与 led_clear() 等价，保留独立接口便于调用方表达意图。 */
    led_clear();
}

void led_set_frame(const uint8_t frame[LED_MATRIX_ROWS])
{
    uint32_t i;

    /* 允许传入空指针作为“清屏”操作，方便调用方处理异常数据。 */
    if (frame == 0)
    {
        led_clear();
        return;
    }

    /* 将调用方准备好的整帧数据复制到内部缓存，避免保存外部指针。 */
    for (i = 0U; i < LED_MATRIX_ROWS; ++i)
    {
        s_frame[i] = frame[i];
    }
}

void led_set_row(uint8_t row, uint8_t data)
{
    /* 越界行直接忽略，防止破坏显示缓存。 */
    if (row >= LED_MATRIX_ROWS)
    {
        return;
    }

    /* 单行更新用于压力/电池等模块，避免相互覆盖其他行。 */
    s_frame[row] = data;
}

void led_set_pixel(uint8_t row, uint8_t col, uint8_t on)
{
    /* 越界坐标直接忽略，避免移位和数组访问越界。 */
    if ((row >= LED_MATRIX_ROWS) || (col >= LED_MATRIX_COLS))
    {
        return;
    }

    /* 根据 on 参数修改一个 bit，其余 bit 保持原样。 */
    if (on != 0U)
    {
        s_frame[row] |= (uint8_t)(1U << col);
    }
    else
    {
        s_frame[row] &= (uint8_t)~(1U << col);
    }
}
/*
 * 低边吸电流映射：数组下标 0~7 对应 8 条 L 线。
 * 低边输出低电平时吸电流，对应 LED 点亮；输出高电平时熄灭。
 * 前 6 条线同时与 S1~S6 按键复用，key_driver 会在采样按键时临时改为输入。
 */
