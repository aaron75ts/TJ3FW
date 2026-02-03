/**
 * @file pulse.h
 * @brief Pulse Out (電能度數與需量) 監測模組
 *
 * 實現 P2.08 引腳的脈衝計數功能
 * - 累積脈衝計數 (50,000 pulse/kWh)
 * - 電量累積計算 (kWh)
 * - 需量計算 (現在電力、預測電力)
 */

#ifndef PULSE_H_
#define PULSE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 脈衝計數數據結構
     */
    typedef struct
    {
        uint32_t pulse_count;       /**< 累積脈衝計數 */
        uint32_t pulse_coefficient; /**< 脈衝係數 (Little Endian, 單位: 0.0001 kWh/pulse) */
        float total_kwh;            /**< 累積電量 (kWh) */
        float current_power;        /**< 現在電力 (kW) */
        float predicted_power;      /**< 預測電力 (kW) */
        uint32_t pulse_frequency;   /**< 脈衝頻率 (milli-Hz) */
        uint32_t last_pulse_time;   /**< 上次脈衝時間 (ms) */
    } pulse_data_t;

    /**
     * @brief 初始化脈衝監測模組
     * @return 0 成功, <0 錯誤
     */
    int pulse_init(void);

    /**
     * @brief 獲取當前脈衝計數
     * @return 累積脈衝數
     */
    uint32_t pulse_get_count(void);

    /**
     * @brief 獲取累積電量 (kWh)
     * @return 累積電量
     */
    float pulse_get_kwh(void);

    /**
     * @brief 獲取現在電力 (kW)
     * @return 現在電力
     */
    float pulse_get_current_power(void);

    /**
     * @brief 獲取預測電力 (kW)
     * @return 預測電力
     */
    float pulse_get_predicted_power(void);

    /**
     * @brief 獲取脈衝頻率 (milli-Hz)
     * @return 脈衝頻率
     */
    uint32_t pulse_get_frequency(void);

    /**
     * @brief 設定脈衝係數
     * @param coefficient 脈衝係數 (Little Endian, 例如 0.0012 = 0x0C000000)
     */
    void pulse_set_coefficient(uint32_t coefficient);

    /**
     * @brief 獲取完整的脈衝數據
     * @param data 輸出數據結構指標
     * @return 0 成功, <0 錯誤
     */
    int pulse_get_data(pulse_data_t *data);

    /**
     * @brief 保存脈衝計數到 Flash
     *
     * 在偵測到電壓驟降時調用
     * @return 0 成功, <0 錯誤
     */
    int pulse_save_to_flash(void);

    /**
     * @brief 從 Flash 載入脈衝計數
     *
     * 在系統啟動時調用
     * @return 0 成功, <0 錯誤
     */
    int pulse_load_from_flash(void);

#ifdef __cplusplus
}
#endif

#endif /* PULSE_H_ */
