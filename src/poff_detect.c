/**
 * @file poff_detect.c
 * @brief Power-off Detect 模組實現
 */

#include "poff_detect.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(poff_detect, LOG_LEVEL_INF);

/* GPIO 定義 */
static const struct gpio_dt_spec poff_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(poff_detect), gpios);

/* GPIO 回調結構 */
static struct gpio_callback poff_cb_data;

/* 停電狀態 */
static bool is_power_off = false;

/* 停電處理回調函數列表 (最多 8 個) */
#define MAX_CALLBACKS 8
static poff_callback_t callbacks[MAX_CALLBACKS];
static uint8_t callback_count = 0;

/* 停電檢測定時器 (用於消抖) */
static struct k_timer debounce_timer;
static bool pending_power_off = false;

/* 前向聲明 */
static void poff_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void debounce_timer_handler(struct k_timer *timer);
static void handle_power_off(void);

/**
 * @brief GPIO 中斷回調
 */
static void poff_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    int pin_state = gpio_pin_get_dt(&poff_gpio);

    if (pin_state < 0)
    {
        return;
    }

    /* POFF_DETECT 為 HIGH 表示停電 */
    if (pin_state == 1 && !is_power_off)
    {
        LOG_WRN("Power-off detected! Starting debounce timer...");
        pending_power_off = true;
        k_timer_start(&debounce_timer, K_MSEC(100), K_NO_WAIT);
    }
    /* POFF_DETECT 為 LOW 表示復電 */
    else if (pin_state == 0 && is_power_off)
    {
        LOG_INF("Power restored!");
        is_power_off = false;
        pending_power_off = false;
        k_timer_stop(&debounce_timer);

        /* TODO: 發送 AC 電文 (復電通報) */
    }
}

/**
 * @brief 消抖定時器回調
 *
 * 在 100ms 後確認停電訊號仍然有效，才進入停電處理程序
 */
static void debounce_timer_handler(struct k_timer *timer)
{
    if (pending_power_off)
    {
        int pin_state = gpio_pin_get_dt(&poff_gpio);
        if (pin_state == 1)
        {
            LOG_ERR("!!! POWER OFF CONFIRMED !!!");
            is_power_off = true;
            handle_power_off();
        }
        else
        {
            LOG_INF("False alarm - power is stable");
            pending_power_off = false;
        }
    }
}

/**
 * @brief 執行停電處理程序
 *
 * 1. 調用所有註冊的回調函數保存數據
 * 2. 發送停電通報 (AB 電文)
 * 3. 關閉所有輸出
 */
static void handle_power_off(void)
{
    LOG_ERR("=== POWER OFF HANDLER ===");

    /* 1. 調用所有註冊的回調函數 */
    for (uint8_t i = 0; i < callback_count; i++)
    {
        if (callbacks[i] != NULL)
        {
            LOG_INF("Calling power-off callback %d", i);
            callbacks[i]();
        }
    }

    /* 2. TODO: 發送 AB 電文 (停電通報) */
    LOG_INF("TODO: Send AB message (Power-off alert) via MQTT");

    /* 3. TODO: 關閉所有輸出 */
    LOG_INF("TODO: Turn off all outputs and LEDs");

    LOG_ERR("=== POWER OFF HANDLER COMPLETED ===");
}

int poff_detect_init(void)
{
    int ret;

    LOG_INF("Initializing power-off detection...");

    /* 檢查 GPIO 是否準備好 */
    if (!device_is_ready(poff_gpio.port))
    {
        LOG_ERR("POFF_DETECT GPIO device not ready");
        return -ENODEV;
    }

    /* 配置為輸入，帶下拉電阻 (正常狀態為 LOW) */
    ret = gpio_pin_configure_dt(&poff_gpio, GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure POFF_DETECT GPIO: %d", ret);
        return ret;
    }

    /* 配置中斷 (雙邊觸發，用於檢測停電和復電) */
    ret = gpio_pin_interrupt_configure_dt(&poff_gpio, GPIO_INT_EDGE_BOTH);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure POFF_DETECT interrupt: %d", ret);
        return ret;
    }

    /* 初始化並註冊 GPIO 回調 */
    gpio_init_callback(&poff_cb_data, poff_gpio_callback, BIT(poff_gpio.pin));
    gpio_add_callback(poff_gpio.port, &poff_cb_data);

    /* 初始化消抖定時器 */
    k_timer_init(&debounce_timer, debounce_timer_handler, NULL);

    /* 檢查初始狀態 */
    int pin_state = gpio_pin_get_dt(&poff_gpio);
    is_power_off = (pin_state == 1);

    LOG_INF("Power-off detection initialized (P%d.%02d), initial state: %s",
            poff_gpio.pin / 32 + 1, poff_gpio.pin % 32,
            is_power_off ? "POWER OFF" : "NORMAL");

    return 0;
}

int poff_detect_register_callback(poff_callback_t callback)
{
    if (callback == NULL)
    {
        return -EINVAL;
    }

    if (callback_count >= MAX_CALLBACKS)
    {
        LOG_ERR("Too many callbacks registered (max %d)", MAX_CALLBACKS);
        return -ENOMEM;
    }

    callbacks[callback_count++] = callback;
    LOG_DBG("Registered power-off callback #%d", callback_count);

    return 0;
}

bool poff_detect_is_power_off(void)
{
    return is_power_off;
}
