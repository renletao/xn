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
static volatile uint8_t s_frame[LED_MATRIX_ROWS];
/* 当前扫描时隙：8 行矩阵、模式/DC、单位、上排 DP、下排 DP。 */
static volatile uint8_t s_scan_slot;
/* 显示总开关；关闭时保留缓存，但所有高低边均输出关闭电平。 */
static volatile uint8_t s_display_enabled;
static volatile uint8_t s_scan_timer_configured;
static volatile uint8_t s_scan_suspended;

/* L14 是四个压力小数点和两个单位灯共用的低边。 */
#define LED_DECIMAL_COL 7U
/* 8 个矩阵行、模式、单位、上排 DP、下排 DP 共 12 个扫描时隙。 */
#define LED_MODE_SCAN_SLOT       LED_MATRIX_ROWS
#define LED_UNIT_SCAN_SLOT       (LED_MATRIX_ROWS + 1U)
#define LED_ACTUAL_DP_SCAN_SLOT  (LED_MATRIX_ROWS + 2U)
#define LED_TARGET_DP_SCAN_SLOT  (LED_MATRIX_ROWS + 3U)
#define LED_SCAN_SLOT_COUNT      (LED_MATRIX_ROWS + 4U)
/* 关断比较点提前 20 us，给 SS8550 和公共端留出固定消隐时间。 */
#define LED_SCAN_BLANK_TICKS       480U

/* 系统时钟固定为 24 MHz；4 kHz 时基使 12 个时隙得到约 333 Hz 整屏刷新。 */
#define LED_SCAN_TIMER_RELOAD      ((24000000U / LED_SCAN_TIMER_HZ) - 1U)

/* 按实际 GPIO 端口分组的高边和低边掩码，刷新路径直接写 BSRR/BRR。 */
#define LED_HIGH_GPIOA_MASK        (GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | \
                                    GPIO_PIN_4 | GPIO_PIN_7)
#define LED_HIGH_GPIOB_MASK        (GPIO_PIN_0 | GPIO_PIN_6)
#define LED_HIGH_GPIOC_MASK        GPIO_PIN_0
#define LED_LOW_GPIOA_MASK         (GPIO_PIN_0 | GPIO_PIN_5 | GPIO_PIN_6)
#define LED_LOW_GPIOB_MASK         (GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | \
                                    GPIO_PIN_7)
#define LED_LOW_GPIOC_MASK         GPIO_PIN_1

/* 前置声明：高边放电函数位于低边关闭函数之前。 */
static void led_all_low_off(void);

static void led_low_apply(uint8_t data)
{
    uint32_t gpioa_mask;
    uint32_t gpiob_mask;
    uint32_t gpioc_mask;

    /* 矩阵 bit 到实际 GPIO 的映射：PA0/PA6/PA5、PB1/PB3/PB2/PB7、PC1。 */
    gpioa_mask = ((uint32_t)data & 0x41U) |
                 (((uint32_t)data & 0x80U) >> 2U);
    gpiob_mask = ((uint32_t)data & 0x02U) |
                 (((uint32_t)data & 0x04U) << 1U) |
                 (((uint32_t)data & 0x08U) >> 1U) |
                 (((uint32_t)data & 0x10U) << 3U);
    gpioc_mask = ((uint32_t)data & 0x20U) >> 4U;

    if (gpioa_mask != 0U)
    {
        GPIOA->BRR = gpioa_mask;
    }
    if (gpiob_mask != 0U)
    {
        GPIOB->BRR = gpiob_mask;
    }
    if (gpioc_mask != 0U)
    {
        GPIOC->BRR = gpioc_mask;
    }
}

static void led_high_select(const LED_Pin_t *pin)
{
    /* 高边低有效；所有其他高边已在本时隙开始处批量关闭。 */
    pin->port->BRR = pin->pin;
}

static void led_scan_timer_stop(void)
{
    if (s_scan_timer_configured == 0U)
    {
        return;
    }

    TIM14->DIER &= (uint32_t)~(TIM_DIER_UIE | TIM_DIER_CC1IE);
    TIM14->CR1 &= (uint32_t)~TIM_CR1_CEN;
    TIM14->CCER &= (uint32_t)~TIM_CCER_CC1E;
    TIM14->SR = (uint32_t)~(TIM_SR_UIF | TIM_SR_CC1IF);
    NVIC_DisableIRQ(TIM14_IRQn);
    NVIC_ClearPendingIRQ(TIM14_IRQn);
}

static void led_scan_timer_start(void)
{
    /* STOP 唤醒后外设时钟门控状态可能变化，每次启动都重新打开时钟。 */
    __HAL_RCC_TIM14_CLK_ENABLE();

    /* 每次启动都重写时基，覆盖 STOP 唤醒后定时器寄存器被复位的情况。 */
    TIM14->PSC = 0U;
    TIM14->ARR = LED_SCAN_TIMER_RELOAD;
    TIM14->CCR1 = LED_SCAN_TIMER_RELOAD - LED_SCAN_BLANK_TICKS;
    TIM14->CCMR1 &= (uint32_t)~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M);
    TIM14->CCER |= TIM_CCER_CC1E;
    TIM14->EGR = TIM_EGR_UG;
    s_scan_timer_configured = 1U;

    TIM14->CNT = 0U;
    TIM14->SR = (uint32_t)~(TIM_SR_UIF | TIM_SR_CC1IF);
    TIM14->DIER |= TIM_DIER_UIE | TIM_DIER_CC1IE;
    NVIC_SetPriority(TIM14_IRQn, 1U);
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    NVIC_EnableIRQ(TIM14_IRQn);
    TIM14->CR1 |= TIM_CR1_CEN;
}

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
    /*
     * PNP 高边管低电平导通，因此关闭高边要输出高电平。
     * 切换行时必须先关闭全部 8 路，避免两路公共端同时导通。
     */
    GPIOA->BSRR = LED_HIGH_GPIOA_MASK;
    GPIOB->BSRR = LED_HIGH_GPIOB_MASK;
    GPIOC->BSRR = LED_HIGH_GPIOC_MASK;
}

static void led_all_low_off(void)
{
    /* 低边为低有效，全部输出高电平即可关闭所有 LED。 */
    GPIOA->BSRR = LED_LOW_GPIOA_MASK;
    GPIOB->BSRR = LED_LOW_GPIOB_MASK;
    GPIOC->BSRR = LED_LOW_GPIOC_MASK;
}

static void led_set_low_side(uint8_t row)
{
    uint8_t row_data = (uint8_t)(s_frame[row] & 0x7FU);

    /*
     * 低边为低有效：缓存 bit=1 时输出 RESET 吸电流，
     * 缓存 bit=0 时输出 SET，避免未选中的 LED 串光。
     */
    /* led_scan() 已先批量关闭全部低边；这里只需一次性拉低有效线。 */
    led_low_apply(row_data);
}

static void led_set_pressure_decimal_side(uint8_t first_row)
{
    uint8_t row;

    /* 独立 DP 时隙中只允许 L14 一条低边有效。 */
    led_all_low_off();
    for (row = first_row; row < (uint8_t)(first_row + 2U); ++row)
    {
        if ((s_frame[row] & (uint8_t)(1U << LED_DECIMAL_COL)) != 0U)
        {
            /* 缓存已约束每排最多一个 DP；找到后只打开对应高边。 */
            led_high_select(&s_high_pins[row]);
            led_low_apply((uint8_t)(1U << LED_DECIMAL_COL));
            return;
        }
    }
}

static void led_set_mode_side(void)
{
    uint8_t mode_mask = mode_led_get_mask();
    uint8_t dc_on = dc_led_is_charging();
    uint8_t low_data = (uint8_t)(mode_mask & 0x07U);

    /*
     * Q1/L01 的额外扫描时隙同时承载模式灯和 DC 灯：
     * L00~L03 中最多点亮一个模式灯，L04/L05 为充电 DC 指示灯。
     */
    /* 模式灯 bit0~bit2，充电灯占用 bit3/bit4。 */
    if (dc_on != 0U)
    {
        low_data |= 0x18U;
    }
    led_low_apply(low_data);
}

static void led_set_unit_side(void)
{
    uint8_t unit = unit_led_get_index();

    /*
     * 单位灯的公共端通常使用 Q1/L01；最后一个 L14-L08
     * 使用 Q4/P08，因此需要同时选择低边和对应高边。
     */
    uint8_t cathode_col = (unit == 0U) ? 5U :
                          (unit == 1U) ? 6U : 7U;

    /* 先前已批量关闭所有低边；只拉低当前单位对应的低边。 */
    led_low_apply((uint8_t)(1U << cathode_col));

    /* 前三个单位灯使用 L01，最后一个 L14-L08 使用 Q4/P08。 */
    led_high_select(&s_high_pins[(unit == 3U) ? 3U : 0U]);
}

void led_init(void)
{
    uint32_t i;

    /* 重新初始化 GPIO 前先停止旧的扫描中断，避免唤醒时并发改脚。 */
    led_scan_timer_stop();

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
    /* 从 Q1 开始扫描；实际节拍由 TIM14 固定产生。 */
    s_scan_slot = 0U;
    led_scan_timer_start();
}

void led_scan(void)
{
    /* 显示关闭时保持所有输出关闭，不执行正常矩阵扫描。 */
    if ((s_display_enabled == 0U) || (s_scan_suspended != 0U))
    {
        led_all_high_off();
        led_all_low_off();
        return;
    }

    /* 更新事件已经由比较事件提前关断；这里再做一次无等待的保险关断。 */
    led_all_low_off();
    led_all_high_off();
    if (s_scan_slot < LED_MATRIX_ROWS)
    {
        /* 普通矩阵行不驱动 L14，小数点由后面的独立时隙完成。 */
        led_set_low_side(s_scan_slot);
        led_high_select(&s_high_pins[s_scan_slot]);
    }
    else if (s_scan_slot == LED_MODE_SCAN_SLOT)
    {
        /* 辅助时隙 8：Q1/L01 驱动模式灯和 DC 充电灯。 */
        led_set_mode_side();
        led_high_select(&s_high_pins[0]);
    }
    else if (s_scan_slot == LED_UNIT_SCAN_SLOT)
    {
        /* 辅助时隙 9：驱动当前选中的单位灯。 */
        led_set_unit_side();
    }
    else if (s_scan_slot == LED_ACTUAL_DP_SCAN_SLOT)
    {
        /* 辅助时隙 10：只扫描上排实际压力的小数点。 */
        led_set_pressure_decimal_side(1U);
    }
    else
    {
        /* 辅助时隙 11：只扫描下排目标压力的小数点。 */
        led_set_pressure_decimal_side(4U);
    }

    ++s_scan_slot;
    if (s_scan_slot >= LED_SCAN_SLOT_COUNT)
    {
        s_scan_slot = 0U;
    }
}

void led_scan_blank(void)
{
    /* 比较事件在更新事件前固定到达；低边保持有效以释放公共端残留电荷。 */
    led_all_high_off();
    GPIOA->BRR = LED_LOW_GPIOA_MASK;
    GPIOB->BRR = LED_LOW_GPIOB_MASK;
    GPIOC->BRR = LED_LOW_GPIOC_MASK;
}

void led_scan_suspend(void)
{
    /* 停止定时器和关闭 GPIO 必须是一个不可被扫描中断打断的窗口。 */
    __disable_irq();
    s_scan_suspended = 1U;
    led_scan_timer_stop();
    led_all_high_off();
    led_all_low_off();
    __enable_irq();
}

void led_scan_resume(void)
{
    s_scan_suspended = 0U;
    if (s_display_enabled != 0U)
    {
        led_scan_timer_start();
    }
}

void led_set_enabled(uint8_t enabled)
{
    /* 统一保存为 0/1，避免调用方传入其他数值造成状态不明确。 */
    s_display_enabled = (enabled != 0U) ? 1U : 0U;

    /* 显示总开关与定时器/GPIO 必须成组切换，避免半个时隙被重新点亮。 */
    __disable_irq();
    if (s_display_enabled == 0U)
    {
        /* 先停扫描中断，再立即关闭输出，避免 GPIO 与扫描 ISR 并发。 */
        led_scan_timer_stop();
        led_all_high_off();
        led_all_low_off();
    }
    else
    {
        if (s_scan_suspended == 0U)
        {
            led_scan_timer_start();
        }
    }
    __enable_irq();
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
    uint8_t first_row;
    uint8_t i;

    /* 越界行直接忽略，防止破坏显示缓存。 */
    if (row >= LED_MATRIX_ROWS)
    {
        return;
    }

    /* 上下两排压力值各自最多允许一个数位携带小数点。 */
    if ((data & (uint8_t)(1U << LED_DECIMAL_COL)) != 0U)
    {
        if ((row >= 1U) && (row <= 3U))
        {
            first_row = 1U;
        }
        else if ((row >= 4U) && (row <= 6U))
        {
            first_row = 4U;
        }
        else
        {
            first_row = LED_MATRIX_ROWS;
        }

        for (i = first_row; i < (uint8_t)(first_row + 3U); ++i)
        {
            if (i < LED_MATRIX_ROWS)
            {
                s_frame[i] &= (uint8_t)~(1U << LED_DECIMAL_COL);
            }
        }
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
