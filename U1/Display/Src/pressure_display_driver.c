#include "pressure_display_driver.h"
#include "led_driver.h"
#include "unit_led_driver.h"

/* 段线映射：L00=A、L02=B、L03=C、L04=D、L05=E、L12=F、L13=G、L14=小数点。 */
#define PRESSURE_SEG_A   0x01U
#define PRESSURE_SEG_B   0x02U
#define PRESSURE_SEG_C   0x04U
#define PRESSURE_SEG_D   0x08U
#define PRESSURE_SEG_E   0x10U
#define PRESSURE_SEG_F   0x20U
#define PRESSURE_SEG_G   0x40U
#define PRESSURE_SEG_DP  0x80U

/* Q2~Q4 对应上排数字，Q5~Q7 对应下排数字。 */
#define PRESSURE_ACTUAL_ROW      1U
#define PRESSURE_TARGET_ROW      4U

static const uint8_t s_digit_code[10] =
{
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_C |
    PRESSURE_SEG_D | PRESSURE_SEG_E | PRESSURE_SEG_F,
    PRESSURE_SEG_B | PRESSURE_SEG_C,
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_D |
    PRESSURE_SEG_E | PRESSURE_SEG_G,
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_C |
    PRESSURE_SEG_D | PRESSURE_SEG_G,
    PRESSURE_SEG_B | PRESSURE_SEG_C | PRESSURE_SEG_F |
    PRESSURE_SEG_G,
    PRESSURE_SEG_A | PRESSURE_SEG_C | PRESSURE_SEG_D |
    PRESSURE_SEG_F | PRESSURE_SEG_G,
    PRESSURE_SEG_A | PRESSURE_SEG_C | PRESSURE_SEG_D |
    PRESSURE_SEG_E | PRESSURE_SEG_F | PRESSURE_SEG_G,
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_C,
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_C |
    PRESSURE_SEG_D | PRESSURE_SEG_E | PRESSURE_SEG_F |
    PRESSURE_SEG_G,
    PRESSURE_SEG_A | PRESSURE_SEG_B | PRESSURE_SEG_C |
    PRESSURE_SEG_D | PRESSURE_SEG_F | PRESSURE_SEG_G
};

static uint32_t s_actual_base_cbar;
static uint32_t s_target_base_cbar;

static uint32_t pressure_input_to_base(uint32_t value, UnitLedUnit_t unit)
{
    /* 输入值是当前单位数值放大 100 倍后的整数，转换后统一保存为 0.01 BAR。 */
    /* 输入参数按 100 倍放大，999.00 对应 99900。 */
    if (value > PRESSURE_INPUT_MAX)
    {
        value = PRESSURE_INPUT_MAX;
    }

    if (unit == UNIT_LED_BAR)
    {
        /* 所有单位输入统一放大 100 倍：259 表示 2.59 BAR。 */
        return (uint32_t)value;
    }
    if (unit == UNIT_LED_PSI)
    {
        /* value 为 PSI 的百分之一，换算为内部 0.01 BAR。 */
        return (uint32_t)(((uint64_t)value * 689476ULL + 5000000ULL) /
                          10000000ULL);
    }
    if (unit == UNIT_LED_KPA)
    {
        /* value 为 kPa 的百分之一；1 kPa = 1 个 0.01 BAR。 */
        return ((uint32_t)value + 50U) / 100U;
    }

    /* kgf/cm2 显示两位小数：255 表示 2.55 kgf/cm2。 */
    return (uint32_t)(((uint64_t)value * 980665ULL + 500000ULL) /
                      1000000ULL);
}

static uint32_t pressure_unit_hundredths(uint32_t base_cbar,
                                         UnitLedUnit_t unit)
{
    /* 返回当前单位的百分之一，作为统一的自适应显示输入。 */
    if (unit == UNIT_LED_BAR)
    {
        /* 0.01 BAR 本身就是 BAR 的百分之一。 */
        return base_cbar;
    }
    if (unit == UNIT_LED_PSI)
    {
        /* cBAR -> PSI 的百分之一，四舍五入。 */
        return (uint32_t)(((uint64_t)base_cbar * 145038ULL + 5000ULL) /
                          10000ULL);
    }
    if (unit == UNIT_LED_KPA)
    {
        /* 0.01 BAR = 1 kPa，乘 100 得到 kPa 的百分之一。 */
        return base_cbar * 100U;
    }

    /* cBAR -> kgf/cm2 的百分之一，四舍五入。 */
    return (uint32_t)(((uint64_t)base_cbar * 1019716ULL + 500000ULL) /
                      1000000ULL);
}

static uint32_t pressure_unit_hundredths_to_base(uint32_t value,
                                                 UnitLedUnit_t unit)
{
    /* 将当前单位的百分之一转换回内部 0.01 BAR。 */
    if (unit == UNIT_LED_BAR)
    {
        return value;
    }
    if (unit == UNIT_LED_PSI)
    {
        return (uint32_t)(((uint64_t)value * 689476ULL + 5000000ULL) /
                          10000000ULL);
    }
    if (unit == UNIT_LED_KPA)
    {
        return (value + 50U) / 100U;
    }

    return (uint32_t)(((uint64_t)value * 980665ULL + 500000ULL) /
                      1000000ULL);
}

static uint16_t pressure_hundredths_to_display(uint32_t hundredths)
{
    uint32_t display_value;

    /* 0.00~9.99 使用两位小数，10.0~99.9 使用一位小数，100~999 使用整数。 */
    if (hundredths <= 999U)
    {
        display_value = hundredths;
    }
    else if (((hundredths + 5U) / 10U) <= 999U)
    {
        display_value = (hundredths + 5U) / 10U;
    }
    else
    {
        display_value = (hundredths + 50U) / 100U;
    }

    return (display_value > PRESSURE_DISPLAY_MAX) ?
           PRESSURE_DISPLAY_MAX : (uint16_t)display_value;
}

static int8_t pressure_decimal_position(uint32_t hundredths)
{
    /* 数码管段码中的 DP 属于小数点左侧的那一位。 */
    if (hundredths <= 999U)
    {
        return 0; /* X.XX */
    }
    if (((hundredths + 5U) / 10U) <= 999U)
    {
        return 1; /* XX.X */
    }
    return -1; /* XXX */
}

static uint8_t pressure_digit_code(uint16_t value, uint8_t position,
                                   int8_t decimal_position)
{
    uint8_t digit;

    /* 根据位置拆分三位数字：0=百位，1=十位，2=个位。 */
    if (position == 0U)
    {
        digit = (uint8_t)(value / 100U);
    }
    else if (position == 1U)
    {
        digit = (uint8_t)((value / 10U) % 10U);
    }
    else
    {
        digit = (uint8_t)(value % 10U);
    }

    return (decimal_position == (int8_t)position) ?
           (uint8_t)(s_digit_code[digit] | PRESSURE_SEG_DP) :
           s_digit_code[digit];
}

static void pressure_refresh_frame(void)
{
    uint8_t position;
    uint32_t actual_hundredths;
    uint32_t target_hundredths;
    uint16_t actual_value;
    uint16_t target_value;
    int8_t actual_decimal_position;
    int8_t target_decimal_position;
    UnitLedUnit_t unit = unit_led_get_current();

    actual_hundredths = pressure_unit_hundredths(s_actual_base_cbar, unit);
    target_hundredths = pressure_unit_hundredths(s_target_base_cbar, unit);
    actual_value = pressure_hundredths_to_display(actual_hundredths);
    target_value = pressure_hundredths_to_display(target_hundredths);
    actual_decimal_position = pressure_decimal_position(actual_hundredths);
    target_decimal_position = pressure_decimal_position(target_hundredths);

    /*
     * 上排使用 Q2~Q4，下排使用 Q5~Q7。
     * 这里只更新对应 6 行，不调用整帧写入，避免覆盖电池图标 Q8 行。
     */
    for (position = 0U; position < 3U; ++position)
    {
        led_set_row((uint8_t)(PRESSURE_ACTUAL_ROW + position),
                    pressure_digit_code(actual_value, position,
                                       actual_decimal_position));
        led_set_row((uint8_t)(PRESSURE_TARGET_ROW + position),
                    pressure_digit_code(target_value, position,
                                       target_decimal_position));
    }
}

void pressure_display_init(void)
{
    /* 上电默认实际值和目标值均为 000。 */
    s_actual_base_cbar = 0U;
    s_target_base_cbar = 0U;
    pressure_refresh_frame();
}

void pressure_display_set_actual(uint32_t value)
{
    s_actual_base_cbar = pressure_input_to_base(value, unit_led_get_current());
    pressure_refresh_frame();
}

void pressure_display_set_actual_pa(uint32_t pressure_pa)
{
    uint32_t base_cbar = pressure_pa / 1000U;

    /* 0.01 BAR 等于 1000 Pa，采用四舍五入后保存为内部基准单位。 */
    if (((pressure_pa % 1000U) >= 500U) && (base_cbar < 0xFFFFFFFFUL))
    {
        ++base_cbar;
    }

    s_actual_base_cbar = base_cbar;
    pressure_refresh_frame();
}

uint16_t pressure_display_get_actual(void)
{
    return pressure_hundredths_to_display(
        pressure_unit_hundredths(s_actual_base_cbar, unit_led_get_current()));
}

void pressure_display_set_target(uint32_t value)
{
    s_target_base_cbar = pressure_input_to_base(value, unit_led_get_current());
    pressure_refresh_frame();
}

uint16_t pressure_display_get_target(void)
{
    return pressure_hundredths_to_display(
        pressure_unit_hundredths(s_target_base_cbar, unit_led_get_current()));
}

uint32_t pressure_display_get_target_pa(void)
{
    /* 内部基准为 0.01 BAR，1 个基准单位等于 1000 Pa。 */
    return s_target_base_cbar * 1000U;
}

void pressure_display_target_increase(void)
{
    UnitLedUnit_t unit = unit_led_get_current();
    uint32_t hundredths = pressure_unit_hundredths(s_target_base_cbar, unit);

    /* 显示已到 999 时保持不变，不允许继续增加。 */
    if (pressure_hundredths_to_display(hundredths) < PRESSURE_DISPLAY_MAX)
    {
        /* 按当前显示精度增加一个最小步进，再换算回内部单位。 */
        if (hundredths <= 999U)
        {
            ++hundredths;
        }
        else if (((hundredths + 5U) / 10U) <= 999U)
        {
            hundredths += 10U;
        }
        else
        {
            hundredths += 100U;
        }
        s_target_base_cbar = pressure_unit_hundredths_to_base(hundredths, unit);
        pressure_refresh_frame();
    }
}

void pressure_display_target_decrease(void)
{
    UnitLedUnit_t unit = unit_led_get_current();
    uint32_t hundredths = pressure_unit_hundredths(s_target_base_cbar, unit);

    /* 显示已到 000 时保持不变，不允许继续减小。 */
    if (pressure_hundredths_to_display(hundredths) > 0U)
    {
        /* 按当前显示精度减少一个最小步进，再换算回内部单位。 */
        if (hundredths <= 999U)
        {
            --hundredths;
        }
        else if (((hundredths + 5U) / 10U) <= 999U)
        {
            hundredths -= 10U;
        }
        else
        {
            hundredths -= 100U;
        }
        s_target_base_cbar = pressure_unit_hundredths_to_base(hundredths, unit);
        pressure_refresh_frame();
    }
}

void pressure_display_on_unit_changed(void)
{
    /* 单位灯状态已经更新，重新按新单位换算两排显示。 */
    pressure_refresh_frame();
}
