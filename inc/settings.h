/**
 * @file settings.h
 * @brief TJ3 系統設定值管理模組
 *
 * 提供設定值的讀取、寫入、Flash 持久化等功能
 * 設定值以 key-value 格式存儲在 settings.txt
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>

/* 警報類型定義 */
typedef enum
{
    ALARM_TYPE_IO = 3,        /**< Io 模式 */
    ALARM_TYPE_IOR_AUTO = 1,  /**< Ior 自動模式 */
    ALARM_TYPE_IOR_MANUAL = 2 /**< Ior 手動模式 */
} alarm_type_t;

/* 相位設定 */
typedef enum
{
    PHASE_SINGLE = 1, /**< 單相 */
    PHASE_THREE = 3   /**< 三相 */
} phase_type_t;

/* ZCT 比值 */
typedef enum
{
    ZCT_RATIO_4500 = 0, /**< 4500:1 */
    ZCT_RATIO_2000 = 1  /**< 2000:1 */
} zct_ratio_t;

/* 補正相位角 (0° ~ 150°, 每30度一檔) */
typedef enum
{
    PHASE_ANGLE_0 = 0,
    PHASE_ANGLE_30 = 1,
    PHASE_ANGLE_60 = 2,
    PHASE_ANGLE_90 = 3,
    PHASE_ANGLE_120 = 4,
    PHASE_ANGLE_150 = 5
} phase_correction_t;

/* AI 監控設定 (OP Code 0xB5) */
typedef struct
{
    /* 6 個通道的配置 */
    alarm_type_t alarm_type[6];       /**< 警報類型 */
    phase_type_t phase_type[6];       /**< 相位設定 */
    phase_correction_t phase_corr[6]; /**< 補正相位角 */
    zct_ratio_t zct_ratio[6];         /**< ZCT 比值 */

    /* 漏電閥值 (mA) */
    uint16_t light_leak_th[6]; /**< 輕漏電閥值 */
    uint16_t heavy_leak_th[6]; /**< 重漏電閥值 */

    /* 判定時間 (秒) */
    uint16_t leak_on_time[6];  /**< 漏電判定時間 */
    uint16_t leak_off_time[6]; /**< 漏電復歸時間 */

    /* 地絡 (Ig) 設定 */
    uint16_t ig_threshold; /**< 地絡閥值 (mA) */
    uint16_t ig_on_time;   /**< 地絡判定時間 (秒) */
    uint16_t ig_off_time;  /**< 地絡復歸時間 (秒) */
} ai_config_t;

/* 需量與 Modem 設定 (OP Code 0xE5) */
typedef struct
{
    /* 需量警報 (kW) */
    uint16_t demand_alarm1; /**< 目標電力 */
    uint16_t demand_alarm2; /**< 限界電力 */

    /* 脈衝係數 */
    uint32_t pulse_const; /**< 脈衝係數 (Little Endian) */

    /* Modem 設定 */
    uint8_t modem_ip[4]; /**< Modem IP */
    char apn[64];        /**< APN 名稱 */
} demand_config_t;

/* MQTT 客戶端設定 (OP Code 0xA5) */
typedef struct
{
    /* 連線設定 */
    char server_url[81];  /**< MQTT Server URL (80 bytes + null), 支援 mqtt://ip:port 格式 */
    uint16_t server_port; /**< MQTT 伺服器 Port (Little Endian, 可選) */

    /* 認證資訊 */
    char client_id[17]; /**< MQTT Client ID (16 bytes + null) */
    char username[33];  /**< MQTT Username (32 bytes + null) */
    char password[33];  /**< MQTT Password (32 bytes + null) */
} mqtt_config_t;

/* 設備名稱與位置 (OP Code 0xD5) */
typedef struct
{
    char device_name[21]; /**< 設備名稱 (20 bytes + null) */
    char location[61];    /**< 位置名稱 (60 bytes + null) */
} device_info_t;

/* 計量校正參數 (特徵值 8c02) */
typedef struct
{
    uint16_t ct_ratio[6]; /**< CT 變流器比例 */
    uint16_t ch_igain[6]; /**< 通道增益校正值 */
} meter_config_t;

/**
 * @brief 初始化設定值模組
 *
 * 從 Flash 載入 settings.txt，若不存在則使用預設值
 *
 * @return 0 成功，負值為錯誤碼
 */
int settings_init(void);

/**
 * @brief 獲取 AI 監控設定
 *
 * @param config 輸出參數
 * @return 0 成功
 */
int settings_get_ai_config(ai_config_t *config);

/**
 * @brief 設定 AI 監控參數
 *
 * @param config 新的設定值
 * @return 0 成功，負值為錯誤碼
 */
int settings_set_ai_config(const ai_config_t *config);

/**
 * @brief 獲取需量與 Modem 設定
 *
 * @param config 輸出參數
 * @return 0 成功
 */
int settings_get_demand_config(demand_config_t *config);

/**
 * @brief 設定需量與 Modem 參數
 *
 * @param config 輸入參數
 * @return 0 成功
 */
int settings_set_demand_config(const demand_config_t *config);

/**
 * @brief 獲取 MQTT 設定
 *
 * @param config 輸出參數
 * @return 0 成功
 */
int settings_get_mqtt_config(mqtt_config_t *config);

/**
 * @brief 設定 MQTT 參數
 *
 * @param config 新的設定值
 * @return 0 成功，負值為錯誤碼
 */
int settings_set_mqtt_config(const mqtt_config_t *config);

/**
 * @brief 獲取設備資訊
 *
 * @param info 輸出參數
 * @return 0 成功
 */
int settings_get_device_info(device_info_t *info);

/**
 * @brief 設定設備資訊
 *
 * @param info 新的設備資訊
 * @return 0 成功，負值為錯誤碼
 */
int settings_set_device_info(const device_info_t *info);

/**
 * @brief 獲取計量校正參數
 *
 * @param config 輸出參數
 * @return 0 成功
 */
int settings_get_meter_config(meter_config_t *config);

/**
 * @brief 設定計量校正參數
 *
 * @param config 新的計量參數
 * @return 0 成功，負值為錯誤碼
 */
int settings_set_meter_config(const meter_config_t *config);
/**
 * @brief 獲取 Log Level
 *
 * @param level 輸出參數 (0=OFF 1=ERR 2=WRN 3=INF 4=DBG)
 * @return 0 成功
 */
int settings_get_log_level(uint8_t *level);

/**
 * @brief 設定並持久化 Log Level
 *
 * @param level 新的 Log Level (0=OFF 1=ERR 2=WRN 3=INF 4=DBG)
 * @return 0 成功，負値為錯誤碼
 */
int settings_set_log_level(uint8_t level);
/**
 * @brief 將所有設定值保存到 Flash
 *
 * 以 key-value 格式寫入 settings.txt
 *
 * @return 0 成功，負值為錯誤碼
 */
int settings_save_to_flash(void);

/**
 * @brief 從 Flash 載入設定值
 *
 * 讀取 settings.txt 並解析 key-value
 *
 * @return 0 成功，負值為錯誤碼
 */
int settings_load_from_flash(void);

/**
 * @brief 恢復出廠設定
 *
 * @return 0 成功，負值為錯誤碼
 */
int settings_reset_to_default(void);

#endif /* SETTINGS_H_ */
