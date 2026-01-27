#include <zephyr/drivers/gpio.h>
#include "led.h"

static const struct gpio_dt_spec led_ind = GPIO_DT_SPEC_GET(DT_NODELABEL(led_ind), gpios);

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