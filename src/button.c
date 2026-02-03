/**
 * @file button.c
 * @brief 設定模式按鈕 (S Button) 控制模組實現
 */

#include "button.h"
#include "led.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

/* GPIO 定義 (從 Device Tree 讀取) */
/* TODO: 需要在 DTS 中定義 set_button 節點 */
// static const struct gpio_dt_spec set_button = GPIO_DT_SPEC_GET(DT_NODELABEL(set_button), gpios);

/* 暫時使用手動定義 (使用 P1.02) */
#define SET_BUTTON_NODE DT_NODELABEL(gpio1)
static const struct gpio_dt_spec set_button = {
    .port = DEVICE_DT_GET(SET_BUTTON_NODE),
    .pin = 2,
    .dt_flags = GPIO_ACTIVE_LOW};

/* 設定模式狀態 */
static bool set_mode_enabled = false;
static int32_t set_mode_timeout = 0; /* 秒數倒數 */

/* 按鈕狀態 */
static bool button_pressed = false;
static int32_t press_duration = 0; /* 按下持續時間 (秒) */

/* 定時器 */
static struct k_timer button_poll_timer;
static struct k_timer set_mode_timer;

/* 配置參數 */
#define LONG_PRESS_DURATION 3 /* 長按 3 秒進入設定模式 */
#define SET_MODE_TIMEOUT 30   /* 設定模式自動超時 30 秒 */

/* 前向聲明 */
static void button_poll_timer_handler(struct k_timer *timer);
static void set_mode_timer_handler(struct k_timer *timer);

/**
 * @brief 按鈕輪詢定時器回調 (每 100ms)
 */
static void button_poll_timer_handler(struct k_timer *timer)
{
    /* 讀取按鈕狀態 */
    int pin_state = gpio_pin_get_dt(&set_button);
    if (pin_state < 0)
    {
        LOG_ERR("Failed to read button GPIO");
        return;
    }

    bool currently_pressed = (pin_state == 0); /* Active Low */

    /* 按鈕按下檢測 */
    if (currently_pressed && !button_pressed)
    {
        /* 按鈕剛被按下 */
        button_pressed = true;
        press_duration = 0;
        LOG_DBG("Button pressed");
    }
    else if (currently_pressed && button_pressed)
    {
        /* 按鈕持續按下 */
        press_duration++;

        /* 每 100ms 累計一次，10 次 = 1 秒 */
        if (press_duration >= (LONG_PRESS_DURATION * 10))
        {
            /* 長按 3 秒，進入設定模式 */
            if (!set_mode_enabled)
            {
                set_mode_enabled = true;
                set_mode_timeout = SET_MODE_TIMEOUT;

                LOG_INF("Set Mode ENABLED (30 sec timeout)");

                /* LED 快速閃爍表示進入設定模式 */
                led_set_pattern(LED_PATTERN_FAST_BLINK);

                /* 啟動超時定時器 */
                k_timer_start(&set_mode_timer, K_SECONDS(1), K_SECONDS(1));
            }

            /* 重置計數，避免重複觸發 */
            press_duration = 0;
        }
    }
    else if (!currently_pressed && button_pressed)
    {
        /* 按鈕被釋放 */
        button_pressed = false;
        press_duration = 0;
        LOG_DBG("Button released");
    }
}

/**
 * @brief 設定模式超時定時器回調 (每秒)
 */
static void set_mode_timer_handler(struct k_timer *timer)
{
    if (!set_mode_enabled)
    {
        k_timer_stop(timer);
        return;
    }

    set_mode_timeout--;

    if (set_mode_timeout <= 0)
    {
        /* 超時，退出設定模式 */
        set_mode_enabled = false;
        k_timer_stop(timer);

        LOG_INF("Set Mode DISABLED (timeout)");

        /* 恢復正常 LED 模式 */
        led_set_pattern(LED_PATTERN_NORMAL);
    }
    else if (set_mode_timeout <= 10)
    {
        /* 剩餘 10 秒，加快閃爍提醒 */
        LOG_DBG("Set Mode timeout in %d sec", set_mode_timeout);
    }
}

int button_init(void)
{
    int ret;

    LOG_INF("Initializing Set Mode button...");

    /* 檢查 GPIO 是否準備好 */
    if (!device_is_ready(set_button.port))
    {
        LOG_ERR("Button GPIO device not ready");
        return -ENODEV;
    }

    /* 配置為輸入，帶上拉電阻 (Active Low) */
    ret = gpio_pin_configure_dt(&set_button, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure button GPIO: %d", ret);
        return ret;
    }

    /* 初始化定時器 */
    k_timer_init(&button_poll_timer, button_poll_timer_handler, NULL);
    k_timer_init(&set_mode_timer, set_mode_timer_handler, NULL);

    /* 啟動按鈕輪詢定時器 (每 100ms) */
    k_timer_start(&button_poll_timer, K_MSEC(100), K_MSEC(100));

    LOG_INF("Set Mode button initialized (P1.02)");

    return 0;
}

bool button_is_set_mode_enabled(void)
{
    return set_mode_enabled;
}

void button_enter_set_mode(uint16_t duration_sec)
{
    if (duration_sec == 0)
    {
        button_exit_set_mode();
        return;
    }

    set_mode_enabled = true;
    set_mode_timeout = duration_sec;

    LOG_INF("Set Mode ENABLED manually (%d sec)", duration_sec);

    /* LED 快速閃爍 */
    led_set_pattern(LED_PATTERN_FAST_BLINK);

    /* 啟動超時定時器 */
    k_timer_start(&set_mode_timer, K_SECONDS(1), K_SECONDS(1));
}

void button_exit_set_mode(void)
{
    set_mode_enabled = false;
    set_mode_timeout = 0;
    k_timer_stop(&set_mode_timer);

    LOG_INF("Set Mode DISABLED manually");

    /* 恢復正常 LED 模式 */
    led_set_pattern(LED_PATTERN_NORMAL);
}
