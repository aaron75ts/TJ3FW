#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "led.h"

static const struct gpio_dt_spec led_ind = GPIO_DT_SPEC_GET(DT_NODELABEL(led_ind), gpios);

/* LED 模式狀態 */
static led_pattern_t current_pattern = LED_PATTERN_NORMAL;

void led_init(void)
{
    gpio_pin_configure_dt(&led_ind, GPIO_OUTPUT_ACTIVE);
}

void led_on(void)
{
    gpio_pin_set_dt(&led_ind, 1);
}

void led_off(void)
{
    gpio_pin_set_dt(&led_ind, 0);
}

void led_toggle(void)
{
    gpio_pin_toggle_dt(&led_ind);
}

void led_get_state(bool *is_on)
{
    int val = gpio_pin_get_dt(&led_ind);
    *is_on = (val > 0);
}

void led_set_pattern(led_pattern_t pattern)
{
    current_pattern = pattern;

    /* 根據模式設定 LED 狀態 */
    switch (pattern)
    {
    case LED_PATTERN_ON:
        led_on();
        break;
    case LED_PATTERN_OFF:
        led_off();
        break;
    default:
        /* 其他模式由 main loop 控制閃爍 */
        break;
    }
}