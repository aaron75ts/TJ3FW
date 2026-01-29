/**
 * @file settings.c
 * @brief TJ3 系統設定值管理實現
 *
 * 設定值以 key=value 格式存儲在 /lfs/settings.txt
 */

#include "settings.h"
#include "fs_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(settings, LOG_LEVEL_INF);

/* 設定檔路徑 */
#define SETTINGS_FILE_PATH "/lfs/settings.txt"
#define SETTINGS_MAX_LINE 256

/* 全域設定值 */
static ai_config_t g_ai_config;
static comm_demand_config_t g_comm_demand_config;
static mqtt_config_t g_mqtt_config;
static device_info_t g_device_info;
static meter_config_t g_meter_config;

/* 預設值 */
static void settings_init_defaults(void)
{
    /* AI 設定預設值 */
    for (int i = 0; i < 6; i++)
    {
        g_ai_config.alarm_type[i] = ALARM_TYPE_IO;
        g_ai_config.phase_type[i] = PHASE_SINGLE;
        g_ai_config.phase_corr[i] = PHASE_ANGLE_0;
        g_ai_config.zct_ratio[i] = ZCT_RATIO_4500;
        g_ai_config.light_leak_th[i] = 100; // 100mA
        g_ai_config.heavy_leak_th[i] = 200; // 200mA
        g_ai_config.leak_on_time[i] = 5;    // 5秒
        g_ai_config.leak_off_time[i] = 3;   // 3秒
    }
    g_ai_config.ig_threshold = 500; // 500mA
    g_ai_config.ig_on_time = 5;
    g_ai_config.ig_off_time = 3;

    /* 通訊設定預設值 */
    g_comm_demand_config.server_ip[0] = 172;
    g_comm_demand_config.server_ip[1] = 31;
    g_comm_demand_config.server_ip[2] = 33;
    g_comm_demand_config.server_ip[3] = 250;
    g_comm_demand_config.server_port = 1883;
    g_comm_demand_config.demand_alarm1 = 50;       // 50kW
    g_comm_demand_config.demand_alarm2 = 60;       // 60kW
    g_comm_demand_config.pulse_const = 0x0000000C; // 0.0012
    strncpy(g_comm_demand_config.apn, "internet", sizeof(g_comm_demand_config.apn));

    /* MQTT 設定預設值 */
    strncpy(g_mqtt_config.client_id, "TJ3_NODE_000001", sizeof(g_mqtt_config.client_id));
    strncpy(g_mqtt_config.server_url, "mqtt.example.com", sizeof(g_mqtt_config.server_url));

    /* 設備資訊預設值 */
    strncpy(g_device_info.device_name, "TJ3_Device", sizeof(g_device_info.device_name));
    strncpy(g_device_info.location, "Factory_A", sizeof(g_device_info.location));

    /* 計量參數預設值 */
    for (int i = 0; i < 6; i++)
    {
        g_meter_config.ct_ratio[i] = 1000; // 1000:1
        g_meter_config.ch_igain[i] = 1;
    }
}

int settings_init(void)
{
    LOG_INF("Initializing settings...");

    /* 載入預設值 */
    settings_init_defaults();

    /* 嘗試從 Flash 載入 */
    int ret = settings_load_from_flash();
    if (ret < 0)
    {
        LOG_WRN("Failed to load settings from flash, using defaults");
        /* 使用預設值並保存 */
        settings_save_to_flash();
    }

    LOG_INF("Settings initialized");
    return 0;
}

int settings_get_ai_config(ai_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(config, &g_ai_config, sizeof(ai_config_t));
    return 0;
}

int settings_set_ai_config(const ai_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_ai_config, config, sizeof(ai_config_t));
    return settings_save_to_flash();
}

int settings_get_comm_demand_config(comm_demand_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(config, &g_comm_demand_config, sizeof(comm_demand_config_t));
    return 0;
}

int settings_set_comm_demand_config(const comm_demand_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_comm_demand_config, config, sizeof(comm_demand_config_t));
    return settings_save_to_flash();
}

int settings_get_mqtt_config(mqtt_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(config, &g_mqtt_config, sizeof(mqtt_config_t));
    return 0;
}

int settings_set_mqtt_config(const mqtt_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_mqtt_config, config, sizeof(mqtt_config_t));
    return settings_save_to_flash();
}

int settings_get_device_info(device_info_t *info)
{
    if (info == NULL)
    {
        return -EINVAL;
    }
    memcpy(info, &g_device_info, sizeof(device_info_t));
    return 0;
}

int settings_set_device_info(const device_info_t *info)
{
    if (info == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_device_info, info, sizeof(device_info_t));
    return settings_save_to_flash();
}

int settings_get_meter_config(meter_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(config, &g_meter_config, sizeof(meter_config_t));
    return 0;
}

int settings_set_meter_config(const meter_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_meter_config, config, sizeof(meter_config_t));
    return settings_save_to_flash();
}

int settings_save_to_flash(void)
{
    struct fs_file_t file;
    char line[SETTINGS_MAX_LINE];
    int ret;

    fs_file_t_init(&file);

    ret = fs_open(&file, SETTINGS_FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (ret < 0)
    {
        LOG_ERR("Failed to open settings file for writing: %d", ret);
        return ret;
    }

    /* 寫入 AI 設定 */
    for (int i = 0; i < 6; i++)
    {
        snprintf(line, sizeof(line), "ai_alarm_type_%d=%d\n", i, g_ai_config.alarm_type[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_phase_type_%d=%d\n", i, g_ai_config.phase_type[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_phase_corr_%d=%d\n", i, g_ai_config.phase_corr[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_zct_ratio_%d=%d\n", i, g_ai_config.zct_ratio[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_light_leak_th_%d=%u\n", i, g_ai_config.light_leak_th[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_heavy_leak_th_%d=%u\n", i, g_ai_config.heavy_leak_th[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_leak_on_time_%d=%u\n", i, g_ai_config.leak_on_time[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "ai_leak_off_time_%d=%u\n", i, g_ai_config.leak_off_time[i]);
        fs_write(&file, line, strlen(line));
    }

    snprintf(line, sizeof(line), "ai_ig_threshold=%u\n", g_ai_config.ig_threshold);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "ai_ig_on_time=%u\n", g_ai_config.ig_on_time);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "ai_ig_off_time=%u\n", g_ai_config.ig_off_time);
    fs_write(&file, line, strlen(line));

    /* 寫入通訊設定 */
    snprintf(line, sizeof(line), "comm_server_ip=%u.%u.%u.%u\n",
             g_comm_demand_config.server_ip[0], g_comm_demand_config.server_ip[1],
             g_comm_demand_config.server_ip[2], g_comm_demand_config.server_ip[3]);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "comm_server_port=%u\n", g_comm_demand_config.server_port);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "comm_demand_alarm1=%u\n", g_comm_demand_config.demand_alarm1);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "comm_demand_alarm2=%u\n", g_comm_demand_config.demand_alarm2);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "comm_pulse_const=%u\n", g_comm_demand_config.pulse_const);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "comm_apn=%s\n", g_comm_demand_config.apn);
    fs_write(&file, line, strlen(line));

    /* 寫入 MQTT 設定 */
    snprintf(line, sizeof(line), "mqtt_client_id=%s\n", g_mqtt_config.client_id);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "mqtt_server_url=%s\n", g_mqtt_config.server_url);
    fs_write(&file, line, strlen(line));

    /* 寫入設備資訊 */
    snprintf(line, sizeof(line), "device_name=%s\n", g_device_info.device_name);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "device_location=%s\n", g_device_info.location);
    fs_write(&file, line, strlen(line));

    /* 寫入計量參數 */
    for (int i = 0; i < 6; i++)
    {
        snprintf(line, sizeof(line), "meter_ct_ratio_%d=%u\n", i, g_meter_config.ct_ratio[i]);
        fs_write(&file, line, strlen(line));

        snprintf(line, sizeof(line), "meter_ch_igain_%d=%u\n", i, g_meter_config.ch_igain[i]);
        fs_write(&file, line, strlen(line));
    }

    fs_close(&file);

    LOG_INF("Settings saved to flash");
    return 0;
}

#if 0  /* 暫時不使用，等待實現完整的文件讀取功能 */
/* 簡化的 key-value 解析器 */
static int parse_key_value(const char *line, char *key, char *value)
{
    const char *eq = strchr(line, '=');
    if (eq == NULL)
    {
        return -EINVAL;
    }

    int key_len = eq - line;
    if (key_len >= SETTINGS_MAX_LINE)
    {
        return -EINVAL;
    }

    strncpy(key, line, key_len);
    key[key_len] = '\0';

    strncpy(value, eq + 1, SETTINGS_MAX_LINE);

    /* 移除換行符 */
    char *newline = strchr(value, '\n');
    if (newline)
    {
        *newline = '\0';
    }

    return 0;
}
#endif /* 結束 parse_key_value */

int settings_load_from_flash(void)
{
    struct fs_file_t file;
    int ret;

#if 0 /* 這些變數將在完整實現時使用 */
    char line[SETTINGS_MAX_LINE];
    char key[SETTINGS_MAX_LINE];
    char value[SETTINGS_MAX_LINE];
#endif

    fs_file_t_init(&file);

    ret = fs_open(&file, SETTINGS_FILE_PATH, FS_O_READ);
    if (ret < 0)
    {
        LOG_WRN("Settings file not found");
        return ret;
    }

    /* 逐行讀取並解析 */
    /* 注意：這裡簡化實現，實際應該使用緩衝區逐行讀取 */
    /* 暫時先記錄警告，實際應該實現完整的行讀取邏輯 */
    LOG_WRN("Settings load from Flash not fully implemented");

    /* TODO: 實現完整的逐行讀取邏輯
     * 可以考慮：
     * 1. 使用 fs_read 讀取整個文件到緩衝區
     * 2. 解析緩衝區中的每一行
     * 3. 或實現自己的 read_line 函數
     */

    fs_close(&file);
    return 0;

/* 以下代碼暫時不執行，等待實現 read_line 功能 */
#if 0
    while (0) /* fs_handler_read_line(&file, line, sizeof(line)) > 0 */
    {
        if (parse_key_value(line, key, value) < 0)
        {
            continue;
        }

        /* 解析 AI 設定 */
        for (int i = 0; i < 6; i++)
        {
            char key_name[64];

            snprintf(key_name, sizeof(key_name), "ai_alarm_type_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.alarm_type[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_light_leak_th_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.light_leak_th[i] = atoi(value);
                continue;
            }

            /* ...其他欄位類似處理... */
        }

        /* 解析 MQTT 設定 */
        if (strcmp(key, "mqtt_client_id") == 0)
        {
            strncpy(g_mqtt_config.client_id, value, sizeof(g_mqtt_config.client_id) - 1);
        }
        else if (strcmp(key, "mqtt_server_url") == 0)
        {
            strncpy(g_mqtt_config.server_url, value, sizeof(g_mqtt_config.server_url) - 1);
        }

        /* ...其他設定類似處理... */
    }
#endif

    fs_close(&file);

    LOG_INF("Settings loaded from flash");
    return 0;
}

int settings_reset_to_default(void)
{
    settings_init_defaults();
    return settings_save_to_flash();
}
