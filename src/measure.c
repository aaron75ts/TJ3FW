/**
 * @file measure.c
 * @brief TJ3 測電計算模組實現
 */

#include "measure.h"
#include "settings.h"
#include "atm90e26.h"
#include "ext_comm.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(measure, LOG_LEVEL_INF);

/* ATM90E26 設備指標 (在 main.c 中定義) */
static const struct device *atm1_dev;
static const struct device *atm2_dev;
static const struct device *atm3_dev;

/* 通道數據存儲 */
static channel_data_t channel_data[MAX_CHANNELS];

/* 設定值快取 */
static meter_config_t meter_cfg;
static ai_config_t ai_cfg;

/* 數學常數 */
#define PI 3.14159265358979323846f
#define DEG_TO_RAD(deg) ((deg) * PI / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / PI)

/* CH1 & CH2 增益常數 (經過隔離變壓器) */
#define CH12_GAIN_CONST (20000.0f * 0.4f)

/* CH3 ~ CH6 增益常數 (差動負載) */
#define CH36_GAIN_CONST (200.0f)

/* 三相 30 度的 cos 值 */
#define COS_30 (0.866025403784f)

/**
 * @brief 獲取 ATM90E26 設備指標
 */
static int measure_get_atm_devices(void)
{
    atm1_dev = DEVICE_DT_GET(DT_NODELABEL(atm1));
    atm2_dev = DEVICE_DT_GET(DT_NODELABEL(atm2));
    atm3_dev = DEVICE_DT_GET(DT_NODELABEL(atm3));

    if (!device_is_ready(atm1_dev) || !device_is_ready(atm2_dev) || !device_is_ready(atm3_dev))
    {
        LOG_ERR("ATM90E26 devices not ready");
        return -ENODEV;
    }

    return 0;
}

int measure_init(void)
{
    int ret;

    LOG_INF("Initializing measure module...");

    /* 獲取 ATM90E26 設備 */
    ret = measure_get_atm_devices();
    if (ret < 0)
    {
        return ret;
    }

    /* 載入設定值 */
    settings_get_meter_config(&meter_cfg);
    settings_get_ai_config(&ai_cfg);

    /* 初始化通道數據 */
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        memset(&channel_data[i], 0, sizeof(channel_data_t));
        channel_data[i].is_valid = false;
    }

    LOG_INF("Measure module initialized");
    return 0;
}

/**
 * @brief 從 ATM90E26 讀取單個通道數據
 */
static int read_atm_channel(const struct device *dev, channel_data_t *data)
{
    struct sensor_value val;
    int ret;

    /* 觸發採樣 */
    ret = sensor_sample_fetch(dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to fetch sample: %d", ret);
        return ret;
    }

    /* 讀取電壓 */
    ret = sensor_channel_get(dev, SENSOR_CHAN_VOLTAGE, &val);
    if (ret == 0)
    {
        data->raw_voltage = val.val1 * 100 + val.val2 / 10000;
    }

    /* 讀取電流 */
    ret = sensor_channel_get(dev, SENSOR_CHAN_CURRENT, &val);
    if (ret == 0)
    {
        data->raw_current = val.val1 * 1000 + val.val2 / 1000;
    }

    /* 讀取功率 */
    ret = sensor_channel_get(dev, SENSOR_CHAN_POWER, &val);
    if (ret == 0)
    {
        data->raw_power = val.val1;
    }

    /* 讀取相位角 */
    ret = sensor_channel_get(dev, SENSOR_CHAN_ATM90E26_PHASE_ANGLE, &val);
    if (ret == 0)
    {
        data->raw_phase_angle = val.val1;
    }

    data->is_valid = true;
    data->last_update = k_uptime_get_32();

    return 0;
}

int measure_read_raw_data(void)
{
    int ret;

    /* CH1: ATM1 L-line */
    ret = read_atm_channel(atm1_dev, &channel_data[CH1]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH1");
    }

    /* CH2: ATM1 N-line (需要額外處理，因為是 current2) */
    ret = read_atm_channel(atm1_dev, &channel_data[CH2]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH2");
    }

    /* CH3: ATM2 L-line */
    ret = read_atm_channel(atm2_dev, &channel_data[CH3]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH3");
    }

    /* CH4: ATM2 N-line */
    ret = read_atm_channel(atm2_dev, &channel_data[CH4]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH4");
    }

    /* CH5: ATM3 L-line */
    ret = read_atm_channel(atm3_dev, &channel_data[CH5]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH5");
    }

    /* CH6: ATM3 N-line */
    ret = read_atm_channel(atm3_dev, &channel_data[CH6]);
    if (ret < 0)
    {
        LOG_WRN("Failed to read CH6");
    }

    /* CH7: Ig 地絡電流 (特殊處理) */
    // TODO: 實現 Ig 讀取邏輯
    channel_data[CH7].is_valid = false;

    return 0;
}

/**
 * @brief 計算 CH1 或 CH2 的實際電流 (經過隔離變壓器)
 *
 * Real_Irms = raw_value / (CT_ratio * 20000 * 0.4 * CH_Igain)
 */
static float calculate_ch12_current(uint16_t raw_value, uint16_t ct_ratio, uint16_t ch_igain)
{
    if (ct_ratio == 0 || ch_igain == 0)
    {
        return 0.0f;
    }

    float total_gain = (float)ct_ratio * CH12_GAIN_CONST * (float)ch_igain;
    return (float)raw_value / total_gain;
}

/**
 * @brief 計算 CH3~CH6 的實際電流 (差動負載)
 *
 * Real_Irms = raw_value / (CT_ratio * 200 * CH_Igain)
 */
static float calculate_ch36_current(uint16_t raw_value, uint16_t ct_ratio, uint16_t ch_igain)
{
    if (ct_ratio == 0 || ch_igain == 0)
    {
        return 0.0f;
    }

    float total_gain = (float)ct_ratio * CH36_GAIN_CONST * (float)ch_igain;
    return (float)raw_value / total_gain;
}

int measure_calculate_channel(channel_t ch)
{
    if (ch >= MAX_CHANNELS)
    {
        return -EINVAL;
    }

    if (!channel_data[ch].is_valid)
    {
        return -ENODATA;
    }

    channel_data_t *data = &channel_data[ch];

    /* 計算電壓 (XXX.XX V) */
    data->voltage = (float)data->raw_voltage / 100.0f;

    /* 根據通道類型計算電流 */
    if (ch == CH1 || ch == CH2)
    {
        /* CH1 & CH2: 經過隔離變壓器 */
        data->current = calculate_ch12_current(
            data->raw_current,
            meter_cfg.ct_ratio[ch],
            meter_cfg.ch_igain[ch]);
    }
    else if (ch >= CH3 && ch <= CH6)
    {
        /* CH3 ~ CH6: 差動負載 */
        data->current = calculate_ch36_current(
            data->raw_current,
            meter_cfg.ct_ratio[ch],
            meter_cfg.ch_igain[ch]);
    }

    /* 計算功率 */
    data->power = (float)(int16_t)data->raw_power; // signed 16-bit

    /* 計算相位角 (需要根據實際格式調整) */
    data->phase_angle = (float)data->raw_phase_angle;

    /* 計算 Io (零相電流，單位 mA) */
    data->io = data->current * 1000.0f; // A -> mA

    LOG_DBG("CH%d: V=%.2f, I=%.3f, P=%.1f, θ=%.1f, Io=%.1f",
            ch + 1, (double)data->voltage, (double)data->current, (double)data->power,
            (double)data->phase_angle, (double)data->io);

    return 0;
}

int measure_calculate_all(void)
{
    int ret;

    /* 更新設定值快取 */
    settings_get_meter_config(&meter_cfg);
    settings_get_ai_config(&ai_cfg);

    for (int ch = 0; ch < MAX_CHANNELS; ch++)
    {
        ret = measure_calculate_channel(ch);
        if (ret < 0 && ret != -ENODATA)
        {
            LOG_WRN("Failed to calculate CH%d: %d", ch + 1, ret);
        }
    }

    return 0;
}

int measure_get_channel_data(channel_t ch, channel_data_t *data)
{
    if (ch >= MAX_CHANNELS || data == NULL)
    {
        return -EINVAL;
    }

    memcpy(data, &channel_data[ch], sizeof(channel_data_t));
    return 0;
}

int measure_calculate_three_phase(channel_t ch_r, channel_t ch_t, three_phase_data_t *result)
{
    if (ch_r >= MAX_CHANNELS || ch_t >= MAX_CHANNELS || result == NULL)
    {
        return -EINVAL;
    }

    if (!channel_data[ch_r].is_valid || !channel_data[ch_t].is_valid)
    {
        return -ENODATA;
    }

    /* 獲取 R 相和 T 相數據 */
    float I_R = channel_data[ch_r].current;
    float I_T = channel_data[ch_t].current;
    float theta_R = DEG_TO_RAD(channel_data[ch_r].phase_angle);
    float theta_T = DEG_TO_RAD(channel_data[ch_t].phase_angle);

    /* 極坐標 → 直角坐標 */
    float x_R = I_R * cosf(theta_R);
    float y_R = I_R * sinf(theta_R);
    float x_T = I_T * cosf(theta_T);
    float y_T = I_T * sinf(theta_T);

    /* 合成 S 相 (根據基爾霍夫電流定律：IR + IS + IT = 0) */
    float x_S = -(x_R + x_T);
    float y_S = -(y_R + y_T);
    float I_S = sqrtf(x_S * x_S + y_S * y_S);

    /* 計算三相總電流 */
    float I_total = I_R + I_S + I_T;

    /* 填充結果 */
    result->current_r = I_R;
    result->current_s = I_S;
    result->current_t = I_T;
    result->angle_r = channel_data[ch_r].phase_angle;
    result->angle_t = channel_data[ch_t].phase_angle;
    result->total_current = I_total;

    LOG_INF("Three-phase: IR=%.3f, IS=%.3f(calc), IT=%.3f, Total=%.3f",
            (double)I_R, (double)I_S, (double)I_T, (double)I_total);

    return 0;
}

float measure_calculate_ior(channel_t ch, float io, float phase_angle,
                            phase_correction_t correction, phase_type_t phase_type)
{
    /* 補正角度轉換 (0-5 對應 0°-150°，每檔 30°) */
    float alpha = (float)correction * 30.0f;

    /* 相位角加上補正角 */
    float theta_total = DEG_TO_RAD(phase_angle + alpha);

    float ior;

    if (phase_type == PHASE_SINGLE)
    {
        /* 單相公式：Ior = Io * cos(θ + α) */
        ior = io * cosf(theta_total);
    }
    else
    {
        /* 三相公式：Ior = Io * sin(θ + α) / cos(30°) */
        ior = io * sinf(theta_total) / COS_30;
    }

    /* 計算 Ior 並存儲 */
    if (ch < MAX_CHANNELS)
    {
        channel_data[ch].ior = ior;
    }

    LOG_DBG("CH%d Ior calc: Io=%.1f, θ=%.1f, α=%.1f, Ior=%.1f (%s)",
            ch + 1, (double)io, (double)phase_angle, (double)alpha, (double)ior,
            phase_type == PHASE_SINGLE ? "single" : "three");

    return ior;
}

int measure_perform_cycle(void)
{
    int ret;

    LOG_DBG("Starting measurement cycle...");

    /* 1. 讀取原始數據 */
    ret = measure_read_raw_data();
    if (ret < 0)
    {
        LOG_ERR("Failed to read raw data: %d", ret);
        return ret;
    }

    /* 2. 計算所有通道 */
    ret = measure_calculate_all();
    if (ret < 0)
    {
        LOG_ERR("Failed to calculate channels: %d", ret);
        return ret;
    }

    /* 3. 計算 Ior (針對配置為 Ior 模式的通道) */
    for (int ch = 0; ch < 6; ch++)
    { // CH1-6
        if (ai_cfg.alarm_type[ch] == ALARM_TYPE_IOR_AUTO ||
            ai_cfg.alarm_type[ch] == ALARM_TYPE_IOR_MANUAL)
        {

            if (channel_data[ch].is_valid)
            {
                measure_calculate_ior(
                    ch,
                    channel_data[ch].io,
                    channel_data[ch].phase_angle,
                    ai_cfg.phase_corr[ch],
                    ai_cfg.phase_type[ch]);
            }
        }
    }

    /* 4. 三相合成範例 (假設 CH1=R, CH3=T) */
    // 可根據實際配置決定哪些通道需要三相合成
    three_phase_data_t three_phase;
    if (ai_cfg.phase_type[CH1] == PHASE_THREE)
    {
        ret = measure_calculate_three_phase(CH1, CH3, &three_phase);
        if (ret == 0)
        {
            LOG_DBG("Three-phase synthesis completed");
        }
    }

    LOG_DBG("Measurement cycle completed");
    return 0;
}

int measure_get_ig_data(channel_data_t *data)
{
    if (data == NULL)
    {
        return -EINVAL;
    }

    memcpy(data, &channel_data[CH7], sizeof(channel_data_t));
    return 0;
}

int measure_check_leak_alarm(channel_t ch, uint16_t light_threshold, uint16_t heavy_threshold)
{
    if (ch >= MAX_CHANNELS)
    {
        return -EINVAL;
    }

    if (!channel_data[ch].is_valid)
    {
        return -ENODATA;
    }

    /* 使用 Ior 或 Io 判定 (根據配置) */
    float leak_current;
    if (ai_cfg.alarm_type[ch] == ALARM_TYPE_IO)
    {
        leak_current = channel_data[ch].io;
    }
    else
    {
        leak_current = channel_data[ch].ior;
    }

    /* 判定警報等級 */
    if (leak_current >= heavy_threshold)
    {
        return 2; // 重漏電
    }
    else if (leak_current >= light_threshold)
    {
        return 1; // 輕漏電
    }
    else
    {
        return 0; // 正常
    }
}

int measure_send_log_output(phase_type_t phase_type, uint32_t pulse_count)
{
    char log_buffer[256];
    int offset = 0;

    /* 根據相位類型選擇格式 */
    if (phase_type == PHASE_SINGLE)
    {
        /* 單相格式 */
        offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                           "AC 1 PH\r\n");

        /* CH1: Io, Ior, PH */
        if (channel_data[CH1].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH1: Io=%4.0fmA, Ior=%4.0fmA, PH=%3.0f°\r\n",
                               (double)channel_data[CH1].io,
                               (double)channel_data[CH1].ior,
                               (double)channel_data[CH1].phase_angle);
        }

        /* CH2: Io, Ior, PH */
        if (channel_data[CH2].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH2: Io=%4.0fmA, Ior=%4.0fmA, PH=%3.0f°\r\n",
                               (double)channel_data[CH2].io,
                               (double)channel_data[CH2].ior,
                               (double)channel_data[CH2].phase_angle);
        }

        /* CH3: Irms, PH */
        if (channel_data[CH3].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH3: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)channel_data[CH3].current,
                               (double)channel_data[CH3].phase_angle);
        }

        /* CH4: Irms, PH */
        if (channel_data[CH4].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH4: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)channel_data[CH4].current,
                               (double)channel_data[CH4].phase_angle);
        }

        /* CH5: Irms, PH */
        if (channel_data[CH5].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH5: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)channel_data[CH5].current,
                               (double)channel_data[CH5].phase_angle);
        }

        /* CH6: Irms, PH */
        if (channel_data[CH6].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH6: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)channel_data[CH6].current,
                               (double)channel_data[CH6].phase_angle);
        }
    }
    else
    {
        /* 三相格式 */
        offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                           "AC 3 PH\r\n");

        /* CH1: Io, Ior, PH */
        if (channel_data[CH1].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH1: Io=%4.0fmA, Ior=%4.0fmA, PH=%3.0f°\r\n",
                               (double)channel_data[CH1].io,
                               (double)channel_data[CH1].ior,
                               (double)channel_data[CH1].phase_angle);
        }

        /* CH2: Io, Ior, PH */
        if (channel_data[CH2].is_valid)
        {
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH2: Io=%4.0fmA, Ior=%4.0fmA, PH=%3.0f°\r\n",
                               (double)channel_data[CH2].io,
                               (double)channel_data[CH2].ior,
                               (double)channel_data[CH2].phase_angle);
        }

        /* CH3/4: Irms (合併顯示), PH */
        if (channel_data[CH3].is_valid && channel_data[CH4].is_valid)
        {
            float combined_current = channel_data[CH3].current + channel_data[CH4].current;
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH3/4: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)combined_current,
                               (double)channel_data[CH3].phase_angle);
        }

        /* CH5/6: Irms (合併顯示), PH */
        if (channel_data[CH5].is_valid && channel_data[CH6].is_valid)
        {
            float combined_current = channel_data[CH5].current + channel_data[CH6].current;
            offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                               "CH5/6: Irms=%2.3fA, PH=%3.0f°\r\n",
                               (double)combined_current,
                               (double)channel_data[CH5].phase_angle);
        }
    }

    /* PULSE 累積度數 */
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                       "PULSE: %07u\r\n", pulse_count);

    /* 透過 UART30 發送 */
    int ret = ext_comm_send_log(log_buffer);
    if (ret < 0)
    {
        LOG_ERR("Failed to send log output: %d", ret);
        return ret;
    }

    LOG_DBG("Sent measurement log (%d bytes)", offset);
    return 0;
}
