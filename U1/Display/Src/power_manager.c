#include "power_manager.h"
#include "main.h"
#include "py32_u1_hw_config.h"
#include "py32f002b_hal_pwr.h"
#include "key_driver.h"
#include "led_driver.h"
#include "uart_command_driver.h"
#include "uart_control_driver.h"

#define U1_WAKE_LOCAL_PIN       GPIO_PIN_0
#define U1_WAKE_LINK_PIN        GPIO_PIN_5
#define U1_WAKE_LOCAL_EVENT     0x01U
#define U1_WAKE_LINK_EVENT      0x02U
#define U1_WAKE_LOCAL_PORT      GPIOA
#define U1_WAKE_LINK_PORT       GPIOB

static volatile uint8_t s_wake_flags;
static uint8_t s_sleep_requested;
static uint8_t s_sleep_command_sent;
static uint8_t s_sleeping;
static uint32_t s_retry_tick;
static uint8_t s_suppress_s1;
static uint8_t s_suppress_s1_seen_pressed;
static uint32_t s_suppress_s1_deadline;

static void power_gpio_config(GPIO_TypeDef *port, uint16_t pin,
                              uint32_t mode, uint32_t pull)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pin;
    gpio.Mode = mode;
    gpio.Pull = pull;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0U;
    HAL_GPIO_Init(port, &gpio);
}

static void power_config_wake_inputs(void)
{
    /* PA0 is S1 only after the LED/key multiplexing has been stopped. */
    power_gpio_config(U1_WAKE_LOCAL_PORT, U1_WAKE_LOCAL_PIN,
                      GPIO_MODE_IT_FALLING, GPIO_PULLUP);
    power_gpio_config(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN,
                      GPIO_MODE_IT_RISING, GPIO_PULLDOWN);
    __HAL_GPIO_EXTI_CLEAR_IT(U1_WAKE_LOCAL_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(U1_WAKE_LINK_PIN);
    HAL_NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
    HAL_NVIC_SetPriority(EXTI0_1_IRQn, 1U, 0U);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void power_config_running_inputs(void)
{
    HAL_NVIC_DisableIRQ(EXTI0_1_IRQn);
    HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
    power_gpio_config(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN,
                      GPIO_MODE_INPUT, GPIO_PULLDOWN);
    __HAL_GPIO_EXTI_CLEAR_IT(U1_WAKE_LOCAL_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(U1_WAKE_LINK_PIN);
    HAL_NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
}

static void power_config_low_leakage(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* All display lines are already at their off level before this switch. */
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0U;
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void power_pulse_u2(void)
{
    if (HAL_GPIO_ReadPin(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN) != GPIO_PIN_RESET)
    {
        return;
    }
    HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
    HAL_GPIO_WritePin(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN, GPIO_PIN_SET);
    power_gpio_config(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN,
                      GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
    HAL_Delay(5U);
    power_gpio_config(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN,
                      GPIO_MODE_INPUT, GPIO_PULLDOWN);
    __HAL_GPIO_EXTI_CLEAR_IT(U1_WAKE_LINK_PIN);
}

static uint8_t power_enter_stop(void)
{
    uint8_t reason;
    uint32_t primask;

    s_wake_flags = 0U;
    led_set_enabled(0U);
    uart_control_suspend();
    power_config_low_leakage();
    power_config_wake_inputs();

    /* Re-check after all GPIO changes. A held S1 or high WK_UP must win. */
    if ((s_wake_flags != 0U) ||
        (HAL_GPIO_ReadPin(U1_WAKE_LOCAL_PORT, U1_WAKE_LOCAL_PIN) ==
         GPIO_PIN_RESET) ||
        (HAL_GPIO_ReadPin(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN) ==
         GPIO_PIN_SET))
    {
        power_config_running_inputs();
        key_init();
        led_init();
        led_set_enabled(0U);
        uart_control_resume();
        return 0U;
    }

    /* Close the last check-to-WFI race: an edge during this window remains
     * pending in EXTI and wakes WFI, while a flag already serviced aborts. */
    primask = __get_PRIMASK();
    __disable_irq();
    if ((s_wake_flags != 0U) ||
        (HAL_GPIO_ReadPin(U1_WAKE_LOCAL_PORT, U1_WAKE_LOCAL_PIN) ==
         GPIO_PIN_RESET) ||
        (HAL_GPIO_ReadPin(U1_WAKE_LINK_PORT, U1_WAKE_LINK_PIN) ==
         GPIO_PIN_SET))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        power_config_running_inputs();
        key_init();
        led_init();
        led_set_enabled(0U);
        uart_control_resume();
        return 0U;
    }

    s_sleeping = 1U;
    HAL_SuspendTick();
    __DSB();
    if (primask == 0U)
    {
        __enable_irq();
    }
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    __ISB();
    HAL_ResumeTick();

    reason = s_wake_flags;
    app_system_clock_config();
    power_config_running_inputs();
    key_init();
    led_init();
    uart_control_resume();
    s_sleeping = 0U;
    s_sleep_requested = 0U;
    s_sleep_command_sent = 0U;

    if ((reason & U1_WAKE_LINK_EVENT) != 0U)
    {
        led_set_enabled(1U);
    }
    else
    {
        led_set_enabled(0U);
    }
    if ((reason & U1_WAKE_LOCAL_EVENT) != 0U)
    {
        /* The S1 press that woke U1 must never become a pump toggle event. */
        s_suppress_s1 = 1U;
        s_suppress_s1_seen_pressed = 0U;
        s_suppress_s1_deadline = HAL_GetTick() + 200U;
        power_pulse_u2();
    }
    s_wake_flags = 0U;
    return 1U;
}

void power_manager_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    power_config_running_inputs();
    s_wake_flags = 0U;
    s_sleep_requested = 0U;
    s_sleep_command_sent = 0U;
    s_sleeping = 0U;
    s_retry_tick = HAL_GetTick();
    s_suppress_s1 = 0U;
    s_suppress_s1_seen_pressed = 0U;
    s_suppress_s1_deadline = 0U;
}

void power_manager_request_sleep(void)
{
    if ((s_sleeping == 0U) && (led_is_enabled() == 0U))
    {
        s_sleep_requested = 1U;
    }
}

uint8_t power_manager_is_sleeping(void)
{
    return s_sleeping;
}

uint32_t power_manager_filter_s1_events(uint32_t events)
{
    const uint32_t release_events = KEY_EVENT_SHORT_RELEASE |
                                    KEY_EVENT_LONG_RELEASE;

    if (s_suppress_s1 == 0U)
    {
        return events;
    }

    if (key_is_pressed(KEY_S1) != 0U)
    {
        s_suppress_s1_seen_pressed = 1U;
        /*
         * The wake gesture remains active after U1 leaves STOP. Keep the
         * long-press event so it can turn the display on, while dropping the
         * press event that must never toggle the pump.
         */
        return events & KEY_EVENT_LONG_PRESS;
    }

    if ((events & release_events) != 0U)
    {
        /* Suppress the release event, then resume normal key handling. */
        s_suppress_s1 = 0U;
        return events & ~release_events;
    }

    if ((s_suppress_s1_seen_pressed == 0U) &&
        ((int32_t)(HAL_GetTick() - s_suppress_s1_deadline) >= 0))
    {
        /* A very short tap may release before debounce confirms a press. */
        s_suppress_s1 = 0U;
    }

    /* No confirmed press/release yet; there is no event to consume. */
    return events & ~release_events;
}

void power_manager_task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t cmd;
    uint8_t length;
    uint8_t payload[UART_PROTOCOL_MAX_PAYLOAD];

    if (s_sleeping != 0U || s_sleep_requested == 0U ||
        key_is_pressed(KEY_S1) != 0U)
    {
        return;
    }
    /* A second long press can turn the display back on before the request is
     * sent. Cancel that pending local request instead of sleeping later. */
    if ((led_is_enabled() != 0U) && (s_sleep_command_sent == 0U))
    {
        s_sleep_requested = 0U;
        return;
    }
    if (s_sleep_command_sent == 0U)
    {
        if ((uint32_t)(now - s_retry_tick) < 1000U)
        {
            return;
        }
        s_retry_tick = now;
        if (uart_command_request_sleep() != 0U)
        {
            s_sleep_command_sent = 1U;
        }
        return;
    }
    if (uart_control_is_busy() != 0U)
    {
        return;
    }
    if ((uart_control_get_response(&cmd, payload, &length) != 0U) &&
        (cmd == UART_COMMAND_SLEEP) &&
        (length == UART_COMMAND_SLEEP_RESPONSE_LENGTH) &&
        (payload[0] == UART_COMMAND_SLEEP_READY))
    {
        if (power_enter_stop() == 0U)
        {
            s_sleep_command_sent = 0U;
        }
        return;
    }
    /* BUSY, timeout, malformed response, or send error: retry safely. */
    s_sleep_command_sent = 0U;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == U1_WAKE_LOCAL_PIN)
    {
        s_wake_flags |= U1_WAKE_LOCAL_EVENT;
    }
    else if (GPIO_Pin == U1_WAKE_LINK_PIN)
    {
        s_wake_flags |= U1_WAKE_LINK_EVENT;
    }
}
