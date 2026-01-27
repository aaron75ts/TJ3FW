/**
 * @file di.h
 * @brief Digital Input (DI1/DI2) Monitoring Header
 *
 * 根據開發式樣書實現接點輸入 (DI1/DI2) 的監測功能
 * - DI1: P2.06
 * - DI2: P2.07
 *
 * 功能包含：
 * 1. 接點狀態監測與判定時間延時
 * 2. MQTT 雲端通報 (AD 識別碼)
 * 3. Flash 數據記錄
 * 4. BLE 狀態更新
 */

#ifndef DI_H_
#define DI_H_

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief DI 通道編號
 */
typedef enum
{
    DI_CHANNEL_1 = 1, /**< DI1 通道 (P2.06) */
    DI_CHANNEL_2 = 2  /**< DI2 通道 (P2.07) */
} di_channel_t;

/**
 * @brief DI 狀態
 */
typedef enum
{
    DI_STATE_NORMAL = 0, /**< 正常狀態 (Low) */
    DI_STATE_ALERT = 1   /**< 警報狀態 (High) */
} di_state_t;

/**
 * @brief DI 配置參數
 */
typedef struct
{
    uint16_t on_time;  /**< 接點警報判定時間 (1-999秒) */
    uint16_t off_time; /**< 接點復歸判定時間 (1-999秒) */
} di_config_t;

/**
 * @brief 初始化 DI 監測模組
 *
 * 初始化 GPIO、工作隊列、定時器等
 *
 * @return 0 成功，負值為錯誤碼
 */
int di_init(void);

/**
 * @brief 獲取指定 DI 通道的當前狀態
 *
 * @param channel DI 通道編號
 * @return 當前狀態 (DI_STATE_NORMAL 或 DI_STATE_ALERT)
 */
di_state_t di_get_state(di_channel_t channel);

/**
 * @brief 設定指定 DI 通道的判定時間
 *
 * 透過 BLE GATT (OP Code 0xB5) 設定判定時間
 *
 * @param channel DI 通道編號
 * @param on_time 接點警報判定時間 (1-999秒)
 * @param off_time 接點復歸判定時間 (1-999秒)
 * @return 0 成功，負值為錯誤碼
 */
int di_set_config(di_channel_t channel, uint16_t on_time, uint16_t off_time);

/**
 * @brief 獲取指定 DI 通道的配置
 *
 * @param channel DI 通道編號
 * @param config 輸出配置參數
 * @return 0 成功，負值為錯誤碼
 */
int di_get_config(di_channel_t channel, di_config_t *config);

/**
 * @brief 將 DI 狀態保存到 Flash
 *
 * 在停電時被調用，優先保存 DI1 和 DI2 的當前狀態
 *
 * @return 0 成功，負值為錯誤碼
 */
int di_save_state_to_flash(void);

/**
 * @brief 從 Flash 載入 DI 狀態
 *
 * 在復電後被調用，恢復 DI1 和 DI2 的狀態
 *
 * @return 0 成功，負值為錯誤碼
 */
int di_load_state_from_flash(void);

#endif /* DI_H_ */
