/**
 * @file pulse.c
 * @brief Pulse Out 監測模組實現
 */

#include "pulse.h"
#include "poff_detect.h"
#include "fs_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(pulse, LOG_LEVEL_INF);

/* GPIO 定義 */
static const struct gpio_dt_spec pulse_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(pulse_out), gpios);

/* GPIO 回調結構 */
static struct gpio_callback pulse_cb_data;

/* 脈衝數據 */
static pulse_data_t pulse_data = {
    .pulse_count = 0,
    .pulse_coefficient = 0x0000000C, // 預設 0.0012 kWh/pulse (12 * 0.0001)
    .total_kwh = 0.0f,
    .current_power = 0.0f,
    .predicted_power = 0.0f,
    .pulse_frequency = 0,
    .last_pulse_time = 0,
};

/* 30 分鐘電量記錄 (用於需量計算) */
#define DEMAND_HISTORY_SIZE 30 // 30 個 1 分鐘數據
static float kwh_history[DEMAND_HISTORY_SIZE] = {0};
static uint8_t history_index = 0;

/* 脈衝檢測狀態 */
static bool last_pulse_state = false;
static uint32_t pulse_on_time = 0;
static uint32_t pulse_off_time = 0;

/* 定時器 (用於需量計算，每 30 秒更新) */
static struct k_timer demand_timer;

/* 前向聲明 */
static void pulse_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void demand_timer_handler(struct k_timer *timer);
static void update_demand_calculation(void);
static void pulse_poff_callback(void);
static void pulse_poff_callback(void);

/**
 * @brief 計算電量 (kWh)
 *
 * 公式: kWh = pulse_count * (pulse_coefficient / 10000)
 */
static void calculate_kwh(void)
{
    // pulse_coefficient 是 Big Endian，單位是 0.0001 kWh/pulse
    // 例如 0x0000000C = 12 = 0.0012 kWh/pulse
    float coefficient_kwh = (float)pulse_data.pulse_coefficient / 10000.0f;
    pulse_data.total_kwh = (float)pulse_data.pulse_count * coefficient_kwh;
}

/**
 * @brief 計算脈衝頻率 (milli-Hz)
 */
static void calculate_frequency(void)
{
    uint32_t current_time = k_uptime_get_32();

    if (pulse_data.last_pulse_time > 0)
    {
        uint32_t interval_ms = current_time - pulse_data.last_pulse_time;
        if (interval_ms > 0)
        {
            // 頻率 (Hz) = 1000 / interval_ms
            // milli-Hz = Hz * 1000 = 1000000 / interval_ms
            pulse_data.pulse_frequency = 1000000 / interval_ms;
        }
    }

    pulse_data.last_pulse_time = current_time;
}

/**
 * @brief GPIO 中斷回調
 */
static void pulse_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t current_time = k_uptime_get_32();
    int pin_state = gpio_pin_get_dt(&pulse_gpio);

    if (pin_state < 0)
    {
        return;
    }

    bool current_pulse_state = (pin_state == 1);

    /* 檢測 OFF → ON 轉換 */
    if (!last_pulse_state && current_pulse_state)
    {
        pulse_on_time = current_time;
    }
    /* 檢測 ON → OFF 轉換 */
    else if (last_pulse_state && !current_pulse_state)
    {
        pulse_off_time = current_time;

        /* 驗證脈衝寬度 (需大於 10ms) */
        uint32_t pulse_width = pulse_off_time - pulse_on_time;
        if (pulse_width >= 10)
        {
            /* 有效脈衝，累加計數 */
            pulse_data.pulse_count++;

            /* 更新電量 */
            calculate_kwh();

            /* 更新頻率 */
            calculate_frequency();

            LOG_DBG("Pulse detected: count=%u, width=%ums, kWh=%.4f",
                    pulse_data.pulse_count, pulse_width, (double)pulse_data.total_kwh);
        }
        else
        {
            LOG_WRN("Invalid pulse width: %ums (< 10ms)", pulse_width);
        }
    }

    last_pulse_state = current_pulse_state;
}

/**
 * @brief 需量計算定時器回調 (每 30 秒)
 */
static void demand_timer_handler(struct k_timer *timer)
{
    update_demand_calculation();
}

/**
 * @brief 更新需量計算
 */
static void update_demand_calculation(void)
{
    /* 記錄當前電量到歷史 (每分鐘更新兩次，取平均) */
    static uint8_t sub_count = 0;
    static float minute_sum = 0.0f;

    minute_sum += pulse_data.total_kwh;
    sub_count++;

    /* 每 1 分鐘更新一次歷史記錄 */
    if (sub_count >= 2)
    {
        float minute_avg = minute_sum / 2.0f;
        kwh_history[history_index] = minute_avg;
        history_index = (history_index + 1) % DEMAND_HISTORY_SIZE;

        minute_sum = 0.0f;
        sub_count = 0;

        /* 計算現在電力 (30 分鐘前到現在) */
        uint8_t old_index = history_index; // 30 分鐘前的數據
        float kwh_30min_ago = kwh_history[old_index];
        pulse_data.current_power = (pulse_data.total_kwh - kwh_30min_ago) * 2.0f; // kW

        /* 預測電力 (簡化實現：使用最近 1 分鐘的趨勢) */
        uint8_t prev_index = (history_index + DEMAND_HISTORY_SIZE - 1) % DEMAND_HISTORY_SIZE;
        float kwh_1min_ago = kwh_history[prev_index];
        float rate_per_min = pulse_data.total_kwh - kwh_1min_ago;

        /* 計算到 30 分鐘時限終了時的預測值 */
        uint8_t remaining_minutes = 30; // 簡化，實際應計算當前週期剩餘時間
        float predicted_kwh = pulse_data.total_kwh + (rate_per_min * remaining_minutes);
        pulse_data.predicted_power = (predicted_kwh - kwh_30min_ago) * 2.0f; // kW

        LOG_DBG("Demand updated: Current=%.3f kW, Predicted=%.3f kW",
                (double)pulse_data.current_power, (double)pulse_data.predicted_power);
    }
}

int pulse_init(void)
{
    int ret;

    LOG_INF("Initializing pulse monitoring...");

    /* 檢查 GPIO 是否準備好 */
    if (!device_is_ready(pulse_gpio.port))
    {
        LOG_ERR("Pulse GPIO device not ready");
        return -ENODEV;
    }

    /* 配置為輸入，帶上拉電阻 */
    ret = gpio_pin_configure_dt(&pulse_gpio, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure pulse GPIO: %d", ret);
        return ret;
    }

    /* 配置中斷 (雙邊觸發) */
    ret = gpio_pin_interrupt_configure_dt(&pulse_gpio, GPIO_INT_EDGE_BOTH);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure pulse interrupt: %d", ret);
        return ret;
    }

    /* 初始化並註冊 GPIO 回調 */
    gpio_init_callback(&pulse_cb_data, pulse_gpio_callback, BIT(pulse_gpio.pin));
    gpio_add_callback(pulse_gpio.port, &pulse_cb_data);

    /* 從 Flash 載入脈衝計數 */
    pulse_load_from_flash();

    /* 啟動需量計算定時器 (每 30 秒) */
    k_timer_init(&demand_timer, demand_timer_handler, NULL);
    k_timer_start(&demand_timer, K_SECONDS(30), K_SECONDS(30));

    /* 註冊停電回調 */
    poff_detect_register_callback(pulse_poff_callback);

    LOG_INF("Pulse monitoring initialized (P%d.%02d)",
            pulse_gpio.pin / 32 + 1, pulse_gpio.pin % 32);

    return 0;
}

uint32_t pulse_get_count(void)
{
    return pulse_data.pulse_count;
}

float pulse_get_kwh(void)
{
    return pulse_data.total_kwh;
}

float pulse_get_current_power(void)
{
    return pulse_data.current_power;
}

float pulse_get_predicted_power(void)
{
    return pulse_data.predicted_power;
}

uint32_t pulse_get_frequency(void)
{
    return pulse_data.pulse_frequency;
}

void pulse_set_coefficient(uint32_t coefficient)
{
    pulse_data.pulse_coefficient = coefficient;
    calculate_kwh(); // 重新計算電量
    LOG_INF("Pulse coefficient updated: 0x%08X (%.4f kWh/pulse)",
            coefficient, (double)((float)coefficient / 10000.0f));
}

int pulse_get_data(pulse_data_t *data)
{
    if (data == NULL)
    {
        return -EINVAL;
    }

    memcpy(data, &pulse_data, sizeof(pulse_data_t));
    return 0;
}

int pulse_save_to_flash(void)
{
    char line[128];

    LOG_INF("Saving pulse data to Flash: count=%u, kWh=%.4f",
            pulse_data.pulse_count, (double)pulse_data.total_kwh);

    /* 格式: pulse_count,total_kwh,current_power,predicted_power */
    snprintf(line, sizeof(line), "%u,%.4f,%.3f,%.3f",
             pulse_data.pulse_count,
             (double)pulse_data.total_kwh,
             (double)pulse_data.current_power,
             (double)pulse_data.predicted_power);

    int ret = fs_handler_append_log("/lfs/pulse_data.txt", line);
    if (ret < 0)
    {
        LOG_ERR("Failed to save pulse data: %d", ret);
        return ret;
    }

    LOG_DBG("Pulse data saved successfully");
    return 0;
}

int pulse_load_from_flash(void)
{
    LOG_INF("Loading pulse data from Flash...");

    /* TODO: 實現從 Flash 讀取最後一筆脈衝數據
     * 目前簡化實現，使用預設值
     */

    LOG_DBG("Pulse data loaded: count=%u, kWh=%.4f",
            pulse_data.pulse_count, (double)pulse_data.total_kwh);

    return 0;
}

/**
 * @brief 停電回調包裝函數
 *
 * 適配 poff_callback_t 類型 (void 返回值)
 */
static void pulse_poff_callback(void)
{
    int ret = pulse_save_to_flash();
    if (ret < 0)
    {
        LOG_ERR("Failed to save pulse data on power-off: %d", ret);
    }
}
