#include "battery_icon_driver.h"
#include "led_driver.h"

/* L00/L02/L03/L04 为电池图标四条长亮的外框段。 */
#define BATTERY_ICON_OUTLINE_MASK    0x0FU

/* 电量填充段按屏幕从第一格到最后一格排列：
   第一格 L13-L15，第二格 L12-L15，第三格 L05-L15。 */
#define BATTERY_ICON_FILL_MASK       0x70U

static uint8_t s_level;
static uint8_t s_charging;
static uint8_t s_animation_level;
static uint32_t s_last_animation_tick;

static uint8_t battery_icon_animation_start_level(void)
{
    /* 0 格充电时从第 1 格开始；已有电量则从已有格数开始动画。 */
    return (s_level == 0U) ? 1U : s_level;
}

static uint8_t battery_icon_level_mask(uint8_t level)
{
    uint8_t mask = BATTERY_ICON_OUTLINE_MASK;

    /* 先限制输入，保证后面的位操作只产生合法的 0~3 格显示。 */
    if (level > BATTERY_ICON_MAX_LEVEL)
    {
        level = BATTERY_ICON_MAX_LEVEL;
    }

    /* 外框已经在初始 mask 中，以下只追加电量填充格。 */
    if (level >= 1U)
    {
        mask |= 0x40U; /* L13-L15：第一格电量 */
    }
    if (level >= 2U)
    {
        mask |= 0x20U; /* L12-L15：第二格电量 */
    }
    if (level >= 3U)
    {
        mask |= 0x10U; /* L05-L15：第三格电量 */
    }

    return (uint8_t)(mask & (BATTERY_ICON_OUTLINE_MASK | BATTERY_ICON_FILL_MASK));
}

static uint8_t battery_icon_display_level(void)
{
    /*
     * 非充电或满电时直接显示实际电量。
     * 充电未满时，动画值不能小于实际电量，确保已经充满的格保持长亮。
     */
    if ((s_charging != 0U) && (s_level < BATTERY_ICON_MAX_LEVEL))
    {
        /* 已有电量格保持点亮，只对剩余电量格执行动画。 */
        return (s_animation_level < s_level) ? s_level : s_animation_level;
    }

    return s_level;
}

static void battery_icon_refresh(void)
{
    /*
     * 电池图标占用矩阵第 7 行，即 Q8/P15 公共端。
     * bit0~bit3 为外框，bit4~bit6 为三格电量，bit7(L14) 不参与图标。
     */
    led_set_row(7U, battery_icon_level_mask(battery_icon_display_level()));
}

void battery_icon_init(void)
{
    /* 初始化软件状态；此时 LED 驱动已经建立显示缓存。 */
    s_level = 0U;
    s_charging = 0U;
    s_animation_level = 1U;
    s_last_animation_tick = HAL_GetTick();
    battery_icon_refresh();
}

void battery_icon_scan(void)
{
    uint32_t now;

    /* 未充电或已满电时不需要动画，保留当前静态显示。 */
    if ((s_charging == 0U) || (s_level >= BATTERY_ICON_MAX_LEVEL))
    {
        return;
    }

    now = HAL_GetTick();
    /* 采用 HAL tick 差值判断时间，兼容 tick 计数溢出。 */
    if ((uint32_t)(now - s_last_animation_tick) < BATTERY_ICON_ANIMATION_MS)
    {
        return;
    }

    s_last_animation_tick = now;
    /* 到达第 3 格后回到当前实际电量，形成循环动画。 */
    if (s_animation_level >= BATTERY_ICON_MAX_LEVEL)
    {
        s_animation_level = battery_icon_animation_start_level();
    }
    else
    {
        ++s_animation_level;
        if (s_animation_level < s_level)
        {
            s_animation_level = s_level;
        }
    }
    battery_icon_refresh();
}

void battery_icon_set_level(uint8_t level)
{
    /* 电量由业务层提供，但驱动负责将其限制在硬件支持的 0~3 格。 */
    if (level > BATTERY_ICON_MAX_LEVEL)
    {
        level = BATTERY_ICON_MAX_LEVEL;
    }

    /* 保存实际电量，并修正动画起点，防止动画短暂低于实际电量。 */
    s_level = level;
    if (s_level >= BATTERY_ICON_MAX_LEVEL)
    {
        s_animation_level = BATTERY_ICON_MAX_LEVEL;
    }
    else if (s_animation_level < s_level)
    {
        s_animation_level = s_level;
    }
    else if (s_animation_level == 0U)
    {
        s_animation_level = battery_icon_animation_start_level();
    }
    battery_icon_refresh();
}

uint8_t battery_icon_get_level(void)
{
    /* 返回业务设置的实际电量，不返回动画临时值。 */
    return s_level;
}

void battery_icon_set_charging(uint8_t charging)
{
    /* 将任意非零输入统一转换为 1，便于调用方直接传入 GPIO/状态量。 */
    s_charging = (charging != 0U) ? 1U : 0U;
    if (s_charging != 0U)
    {
        /* 每次开始充电都从当前实际电量重新开始动画。 */
        s_animation_level = battery_icon_animation_start_level();
        s_last_animation_tick = HAL_GetTick();
    }
    battery_icon_refresh();
}

uint8_t battery_icon_is_charging(void)
{
    /* 返回规范化后的 0/1 充电状态。 */
    return s_charging;
}

uint8_t battery_icon_get_displayed_level(void)
{
    /* 该接口用于调试或业务查询当前屏幕正在显示的动画帧。 */
    return battery_icon_display_level();
}
