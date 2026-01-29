/**
 * @file di.c
 * @brief Digital Input (DI1/DI2) Monitoring Implementation
 *
 * 實現接點輸入的監測、判定、通報與記錄功能
 */

#include "di.h"
#include "poff_detect.h"
#include "fs_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(di, LOG_LEVEL_INF);

/* DI GPIO 定義 (從 Device Tree 讀取) */
static const struct gpio_dt_spec di1_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(di1), gpios);
static const struct gpio_dt_spec di2_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(di2), gpios);

/* DI 通道資訊結構 */
typedef struct
{
    const struct gpio_dt_spec *gpio;    /**< GPIO 規格 */
    di_channel_t channel;               /**< 通道編號 */
    di_state_t current_state;           /**< 當前狀態 */
    di_state_t last_stable_state;       /**< 上次穩定狀態 */
    di_config_t config;                 /**< 配置參數 */
    int32_t countdown;                  /**< 判定倒數計時 (秒) */
    bool pending_alert;                 /**< 待判定警報 */
    bool pending_restore;               /**< 待判定復歸 */
    struct k_work_delayable check_work; /**< 延時檢查工作 */
} di_channel_info_t;

/* DI 通道資訊 */
static di_channel_info_t di_channels[2];

/* 工作隊列與定時器 */
static struct k_timer di_poll_timer;

/* 前向聲明 */
static void di_check_work_handler(struct k_work *work);
static void di_poll_timer_handler(struct k_timer *timer);
static void di_send_mqtt_alert(di_channel_t channel, di_state_t state);
static void di_update_ble_status(di_channel_t channel, di_state_t state);
static void di_log_to_flash(di_channel_t channel, di_state_t state);
static void di_poff_callback(void);

/**
 * @brief 初始化指定 DI 通道
 */
static int di_channel_init(di_channel_info_t *info, const struct gpio_dt_spec *gpio,
                           di_channel_t channel)
{
    int ret;

    info->gpio = gpio;
    info->channel = channel;
    info->current_state = DI_STATE_NORMAL;
    info->last_stable_state = DI_STATE_NORMAL;
    info->config.on_time = 3;  // 預設 3 秒
    info->config.off_time = 3; // 預設 3 秒
    info->countdown = 0;
    info->pending_alert = false;
    info->pending_restore = false;

    /* 檢查 GPIO 是否準備好 */
    if (!device_is_ready(gpio->port))
    {
        LOG_ERR("DI%d GPIO device not ready", channel);
        return -ENODEV;
    }

    /* 配置為輸入，帶上拉電阻 */
    ret = gpio_pin_configure_dt(gpio, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure DI%d GPIO: %d", channel, ret);
        return ret;
    }

    /* 初始化延時工作 */
    k_work_init_delayable(&info->check_work, di_check_work_handler);

    LOG_INF("DI%d initialized (P%d.%02d)", channel, gpio->pin / 32 + 1, gpio->pin % 32);
    return 0;
}

/**
 * @brief DI 輪詢定時器回調
 *
 * 每秒被調用，檢查 DI 狀態並更新倒數計時
 */
static void di_poll_timer_handler(struct k_timer *timer)
{
    for (int i = 0; i < 2; i++)
    {
        di_channel_info_t *info = &di_channels[i];

        /* 讀取當前 GPIO 狀態 */
        int pin_state = gpio_pin_get_dt(info->gpio);
        if (pin_state < 0)
        {
            LOG_ERR("Failed to read DI%d GPIO", info->channel);
            continue;
        }

        di_state_t current = (pin_state == 1) ? DI_STATE_ALERT : DI_STATE_NORMAL;

        /* 狀態改變檢測 */
        if (current != info->current_state)
        {
            info->current_state = current;

            if (current == DI_STATE_ALERT && !info->pending_alert)
            {
                /* 偵測到警報訊號，啟動判定計時 */
                info->pending_alert = true;
                info->pending_restore = false;
                info->countdown = info->config.on_time;
                LOG_DBG("DI%d: Alert signal detected, countdown: %d sec",
                        info->channel, info->countdown);
            }
            else if (current == DI_STATE_NORMAL && !info->pending_restore)
            {
                /* 偵測到復歸訊號，啟動復歸計時 */
                info->pending_restore = true;
                info->pending_alert = false;
                info->countdown = info->config.off_time;
                LOG_DBG("DI%d: Normal signal detected, countdown: %d sec",
                        info->channel, info->countdown);
            }
        }

        /* 倒數計時處理 */
        if (info->pending_alert || info->pending_restore)
        {
            info->countdown--;

            if (info->countdown <= 0)
            {
                /* 判定時間已到，確認狀態變化 */
                di_state_t new_state = info->pending_alert ? DI_STATE_ALERT : DI_STATE_NORMAL;

                if (new_state != info->last_stable_state)
                {
                    info->last_stable_state = new_state;

                    /* 執行通報與記錄動作 */
                    if (new_state == DI_STATE_ALERT)
                    {
                        LOG_WRN("DI%d Alert!", info->channel);
                    }
                    else
                    {
                        LOG_INF("DI%d Restored", info->channel);
                    }

                    /* 發送 MQTT 通報 */
                    di_send_mqtt_alert(info->channel, new_state);

                    /* 更新 BLE 狀態 */
                    di_update_ble_status(info->channel, new_state);

                    /* 記錄到 Flash */
                    di_log_to_flash(info->channel, new_state);
                }

                /* 清除待判定標誌 */
                info->pending_alert = false;
                info->pending_restore = false;
            }
        }
    }
}

/**
 * @brief 延時檢查工作處理函數
 */
static void di_check_work_handler(struct k_work *work)
{
    /* 預留給未來擴展使用 */
}

/**
 * @brief 發送 MQTT 警報通報
 *
 * 使用識別碼 AD 發送至 AWS 雲端
 * Topic: kyokuto/<MainID>/<PeriID>/AD
 *
 * @param channel DI 通道編號
 * @param state 狀態 (0: 復歸, 1: 警報)
 */
static void di_send_mqtt_alert(di_channel_t channel, di_state_t state)
{
    /* TODO: 實現 MQTT 發送邏輯
     * 1. 組裝 AD 識別碼封包
     * 2. 包含子機 ID、電文序號、通道編號、狀態碼
     * 3. 發送至 Topic: kyokuto/<MainID>/<PeriID>/AD
     * 4. 啟動 ACK 計時器
     * 5. 實現重傳機制
     */
    LOG_INF("MQTT Alert: DI%d = %d (TODO: Implement MQTT)", channel, state);
}

/**
 * @brief 更新 BLE Alarm Status
 *
 * 更新 UUID ...8c05 的 Alarm Status 特徵值
 * BIT_6: DI1 狀態
 * BIT_7: DI2 狀態
 *
 * @param channel DI 通道編號
 * @param state 狀態
 */
static void di_update_ble_status(di_channel_t channel, di_state_t state)
{
    /* 外部函數聲明 (定義在 ble_gatt.c) */
    extern void ble_gatt_update_alarm_status(uint8_t bit_position, bool value);

    /* DI1 → BIT_6, DI2 → BIT_7 */
    uint8_t bit_pos = (channel == DI_CHANNEL_1) ? 6 : 7;
    bool alarm_active = (state == DI_STATE_ALERT);

    /* 更新 BLE Alarm Status 特徵值 */
    ble_gatt_update_alarm_status(bit_pos, alarm_active);

    LOG_DBG("BLE Alarm Status updated: DI%d (BIT_%d) = %d", channel, bit_pos, alarm_active);
}

/**
 * @brief 記錄 DI 狀態到 Flash
 *
 * 寫入「1 分鐘值即時量測紀錄」和「30 分鐘值紀錄」
 * CSV 欄位: DI1_status, DI2_status
 *
 * @param channel DI 通道編號
 * @param state 狀態
 */
static void di_log_to_flash(di_channel_t channel, di_state_t state)
{
    /* 準備 CSV 記錄：時間戳,通道,狀態 */
    char log_line[128];
    uint32_t uptime = k_uptime_get_32();

    snprintf(log_line, sizeof(log_line), "%u,DI%d,%s",
             uptime / 1000, /* 轉換為秒 */
             channel,
             state == DI_STATE_ALERT ? "ALERT" : "NORMAL");

    /* 寫入到日誌檔案 */
    int ret = fs_handler_append_log("/lfs/di_events.csv", log_line);
    if (ret < 0)
    {
        LOG_ERR("Failed to log DI%d event to Flash: %d", channel, ret);
        return;
    }

    LOG_DBG("DI%d event logged: %s", channel, log_line);

    /* TODO: 實現 1 分鐘值記錄 (90天) 和 30 分鐘值記錄 (1年) 的管理 */
}

/**
 * @brief 初始化 DI 監測模組
 */
int di_init(void)
{
    int ret;

    LOG_INF("Initializing DI monitoring...");

    /* 初始化 DI1 */
    ret = di_channel_init(&di_channels[0], &di1_gpio, DI_CHANNEL_1);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize DI1: %d", ret);
        return ret;
    }

    /* 初始化 DI2 */
    ret = di_channel_init(&di_channels[1], &di2_gpio, DI_CHANNEL_2);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize DI2: %d", ret);
        return ret;
    }

    /* 啟動輪詢定時器 (每秒觸發) */
    k_timer_init(&di_poll_timer, di_poll_timer_handler, NULL);
    k_timer_start(&di_poll_timer, K_SECONDS(1), K_SECONDS(1));

    /* 註冊停電回調 */
    poff_detect_register_callback(di_poff_callback);

    LOG_INF("DI monitoring initialized successfully");
    return 0;
}

/**
 * @brief 獲取指定 DI 通道的當前狀態
 */
di_state_t di_get_state(di_channel_t channel)
{
    if (channel < DI_CHANNEL_1 || channel > DI_CHANNEL_2)
    {
        LOG_ERR("Invalid DI channel: %d", channel);
        return DI_STATE_NORMAL;
    }

    return di_channels[channel - 1].last_stable_state;
}

/**
 * @brief 設定指定 DI 通道的判定時間
 */
int di_set_config(di_channel_t channel, uint16_t on_time, uint16_t off_time)
{
    if (channel < DI_CHANNEL_1 || channel > DI_CHANNEL_2)
    {
        LOG_ERR("Invalid DI channel: %d", channel);
        return -EINVAL;
    }

    /* 驗證判定時間範圍 (1-999 秒) */
    if (on_time < 1 || on_time > 999)
    {
        LOG_ERR("Invalid on_time: %d (must be 1-999)", on_time);
        return -EINVAL;
    }
    if (off_time < 1 || off_time > 999)
    {
        LOG_ERR("Invalid off_time: %d (must be 1-999)", off_time);
        return -EINVAL;
    }

    di_channel_info_t *info = &di_channels[channel - 1];
    info->config.on_time = on_time;
    info->config.off_time = off_time;

    LOG_INF("DI%d config updated: on_time=%d, off_time=%d", channel, on_time, off_time);
    return 0;
}

/**
 * @brief 獲取指定 DI 通道的配置
 */
int di_get_config(di_channel_t channel, di_config_t *config)
{
    if (channel < DI_CHANNEL_1 || channel > DI_CHANNEL_2 || config == NULL)
    {
        return -EINVAL;
    }

    di_channel_info_t *info = &di_channels[channel - 1];
    config->on_time = info->config.on_time;
    config->off_time = info->config.off_time;

    return 0;
}

/**
 * @brief 將 DI 狀態保存到 Flash
 *
 * 在停電檢測 (P1.03) 時被調用
 */
int di_save_state_to_flash(void)
{
    LOG_INF("Saving DI states to Flash:");
    LOG_INF("  DI1 = %d", di_channels[0].last_stable_state);
    LOG_INF("  DI2 = %d", di_channels[1].last_stable_state);

    /* 準備 DI 狀態字串 */
    char di_state_line[64];
    snprintf(di_state_line, sizeof(di_state_line), "DI1=%d,DI2=%d",
             di_channels[0].last_stable_state,
             di_channels[1].last_stable_state);

    /* 寫入到 Flash */
    int ret = fs_handler_append_log("/lfs/di_state.txt", di_state_line);
    if (ret < 0)
    {
        LOG_ERR("Failed to save DI states to Flash: %d", ret);
        return ret;
    }

    LOG_DBG("DI states saved successfully");
    return 0;
}

/**
 * @brief 從 Flash 載入 DI 狀態
 *
 * 在系統啟動或復電後被調用
 */
int di_load_state_from_flash(void)
{
    LOG_INF("Loading DI states from Flash...");

    /*
     * 注意：LittleFS 不支持 fseek 到文件末尾或反向讀取
     * 這裡僅示範概念，實際應用中可能需要：
     * 1. 讀取整個文件並解析最後一行
     * 2. 或使用固定位置的小文件來存儲當前狀態
     * 目前簡化為：如果文件不存在，使用默認值
     */

    /* 簡化實現：假設在 settings 中管理 */
    LOG_DBG("DI state loading skipped (managed by settings module)");

    /* 如果需要，可以從 settings 讀取 DI 配置參數 */

    return 0;
}

/**
 * @brief 停電回調包裝函數
 *
 * 適配 poff_callback_t 類型 (void 返回值)
 */
static void di_poff_callback(void)
{
    int ret = di_save_state_to_flash();
    if (ret < 0)
    {
        LOG_ERR("Failed to save DI state on power-off: %d", ret);
    }
}
