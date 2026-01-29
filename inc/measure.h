/**
 * @file measure.h
 * @brief TJ3 測電計算模組
 *
 * 實現 CH1-7 通道的電流、電壓、漏電等參數的計算
 * - 通道映射 (ATM1/2/3 → CH1-7)
 * - 增益補正計算
 * - 三相向量合成
 * - Ior 抵抗分漏電計算
 */

#ifndef MEASURE_H_
#define MEASURE_H_

#include <stdint.h>
#include <stdbool.h>
#include "settings.h"

/* 通道編號定義 */
typedef enum
{
    CH1 = 0, /**< 通道 1 (ATM1 L-line) */
    CH2 = 1, /**< 通道 2 (ATM1 N-line) */
    CH3 = 2, /**< 通道 3 (ATM2 L-line) */
    CH4 = 3, /**< 通道 4 (ATM2 N-line) */
    CH5 = 4, /**< 通道 5 (ATM3 L-line) */
    CH6 = 5, /**< 通道 6 (ATM3 N-line) */
    CH7 = 6  /**< 通道 7 (Ig 地絡電流) */
} channel_t;

#define MAX_CHANNELS 7

/* 通道測量數據 */
typedef struct
{
    /* 原始數據 (從 ATM90E26 讀取) */
    uint16_t raw_voltage;     /**< 原始電壓值 */
    uint16_t raw_current;     /**< 原始電流值 */
    uint16_t raw_power;       /**< 原始功率值 */
    uint16_t raw_phase_angle; /**< 原始相位角 */

    /* 校正後數據 */
    float voltage;     /**< 實際電壓 (V) */
    float current;     /**< 實際電流 (A) */
    float power;       /**< 實際功率 (W) */
    float phase_angle; /**< 相位角 (度) */

    /* 漏電相關 */
    float io;  /**< Io 零相電流 (mA) */
    float ior; /**< Ior 抵抗分漏電 (mA) */

    /* 狀態 */
    bool is_valid;        /**< 數據是否有效 */
    uint32_t last_update; /**< 最後更新時間 (ms) */
} channel_data_t;

/* 三相合成數據 */
typedef struct
{
    float current_r;     /**< R 相電流 (A) */
    float current_s;     /**< S 相電流 (A, 合成) */
    float current_t;     /**< T 相電流 (A) */
    float angle_r;       /**< R 相角度 (度) */
    float angle_t;       /**< T 相角度 (度) */
    float total_current; /**< 三相總電流 (A) */
} three_phase_data_t;

/**
 * @brief 初始化測電模組
 *
 * @return 0 成功，負值為錯誤碼
 */
int measure_init(void);

/**
 * @brief 讀取所有通道的原始數據
 *
 * 從 3 個 ATM90E26 讀取原始數據並映射到 CH1-7
 *
 * @return 0 成功，負值為錯誤碼
 */
int measure_read_raw_data(void);

/**
 * @brief 計算指定通道的實際值
 *
 * 執行增益補正、單位轉換等計算
 *
 * @param ch 通道編號
 * @return 0 成功，負值為錯誤碼
 */
int measure_calculate_channel(channel_t ch);

/**
 * @brief 計算所有通道
 *
 * @return 0 成功，負值為錯誤碼
 */
int measure_calculate_all(void);

/**
 * @brief 獲取指定通道的測量數據
 *
 * @param ch 通道編號
 * @param data 輸出參數
 * @return 0 成功，負值為錯誤碼
 */
int measure_get_channel_data(channel_t ch, channel_data_t *data);

/**
 * @brief 計算三相向量合成
 *
 * 根據 R 相和 T 相的數據合成 S 相
 *
 * @param ch_r R 相通道編號
 * @param ch_t T 相通道編號
 * @param result 輸出三相數據
 * @return 0 成功，負值為錯誤碼
 */
int measure_calculate_three_phase(channel_t ch_r, channel_t ch_t, three_phase_data_t *result);

/**
 * @brief 計算 Ior (抵抗分漏電)
 *
 * 根據 Io、相位角和補正角計算 Ior
 *
 * @param ch 通道編號
 * @param io Io 零相電流 (mA)
 * @param phase_angle 相位角 (度)
 * @param correction 補正角 (phase_correction_t)
 * @param phase_type 相位類型 (單相/三相)
 * @return Ior 值 (mA)
 */
float measure_calculate_ior(channel_t ch, float io, float phase_angle,
                            phase_correction_t correction, phase_type_t phase_type);

/**
 * @brief 執行完整的測量週期
 *
 * 1. 讀取原始數據
 * 2. 計算所有通道
 * 3. 計算三相合成
 * 4. 計算 Ior
 *
 * @return 0 成功，負值為錯誤碼
 */
int measure_perform_cycle(void);

/**
 * @brief 獲取 CH7 (Ig 地絡電流) 數據
 *
 * @param data 輸出參數
 * @return 0 成功，負值為錯誤碼
 */
int measure_get_ig_data(channel_data_t *data);

/**
 * @brief 檢查通道是否超過漏電閥值
 *
 * @param ch 通道編號
 * @param light_threshold 輕漏電閥值 (mA)
 * @param heavy_threshold 重漏電閥值 (mA)
 * @return 0=正常, 1=輕漏電, 2=重漏電
 */
int measure_check_leak_alarm(channel_t ch, uint16_t light_threshold, uint16_t heavy_threshold);

/**
 * @brief 將測電數據格式化並通過 UART30 輸出
 * @param phase_type 單相 (PHASE_SINGLE) 或三相 (PHASE_THREE)
 * @param pulse_count 累積度數 (PULSE 計數)
 * @return 0 成功, <0 錯誤
 */
int measure_send_log_output(phase_type_t phase_type, uint32_t pulse_count);

#endif /* MEASURE_H_ */
