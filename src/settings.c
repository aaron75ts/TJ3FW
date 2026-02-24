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
static demand_config_t g_demand_config;
static mqtt_config_t g_mqtt_config;
static device_info_t g_device_info;
static meter_config_t g_meter_config;
static uint8_t g_log_level;
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

    /* 需量與 Modem 設定預設值 */
    g_demand_config.demand_alarm1 = 50;       // 50kW
    g_demand_config.demand_alarm2 = 60;       // 60kW
    g_demand_config.pulse_const = 0x0C000000; // 0.0012 (Little Endian)
    g_demand_config.modem_ip[0] = 192;
    g_demand_config.modem_ip[1] = 168;
    g_demand_config.modem_ip[2] = 1;
    g_demand_config.modem_ip[3] = 1;
    strncpy(g_demand_config.apn, "internet", sizeof(g_demand_config.apn));

    /* MQTT 預設值 */

    g_mqtt_config.server_port = 1883;
    strncpy(g_mqtt_config.server_url, "mqtt://172.31.33.250:1883", sizeof(g_mqtt_config.server_url));
    strncpy(g_mqtt_config.client_id, "TJ3_NODE_000001", sizeof(g_mqtt_config.client_id));
    strncpy(g_mqtt_config.username, "", sizeof(g_mqtt_config.username));
    strncpy(g_mqtt_config.password, "", sizeof(g_mqtt_config.password));

    /* 設備資訊預設值 */
    strncpy(g_device_info.device_name, "TJ3_Device", sizeof(g_device_info.device_name));
    strncpy(g_device_info.location, "Factory_A", sizeof(g_device_info.location));

    /* 計量參數預設值 */
    for (int i = 0; i < 6; i++)
    {
        g_meter_config.ct_ratio[i] = 1000; // 1000:1
        g_meter_config.ch_igain[i] = 1;
    }
    /* Log Level 預設値: INFO */
    g_log_level = 3;
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

int settings_get_demand_config(demand_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(config, &g_demand_config, sizeof(demand_config_t));
    return 0;
}

int settings_set_demand_config(const demand_config_t *config)
{
    if (config == NULL)
    {
        return -EINVAL;
    }
    memcpy(&g_demand_config, config, sizeof(demand_config_t));
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

int settings_get_log_level(uint8_t *level)
{
    if (level == NULL)
    {
        return -EINVAL;
    }
    *level = g_log_level;
    return 0;
}

int settings_set_log_level(uint8_t level)
{
    g_log_level = level;
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

    /* 寫入需量設定 */
    snprintf(line, sizeof(line), "demand_alarm1=%u\n", g_demand_config.demand_alarm1);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "demand_alarm2=%u\n", g_demand_config.demand_alarm2);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "pulse_const=%u\n", g_demand_config.pulse_const);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "modem_ip=%u.%u.%u.%u\n",
             g_demand_config.modem_ip[0], g_demand_config.modem_ip[1],
             g_demand_config.modem_ip[2], g_demand_config.modem_ip[3]);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "modem_apn=%s\n", g_demand_config.apn);
    fs_write(&file, line, strlen(line));

    /* 寫入 MQTT 設定 */
    snprintf(line, sizeof(line), "mqtt_server_port=%u\n", g_mqtt_config.server_port);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "mqtt_server_url=%s\n", g_mqtt_config.server_url);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "mqtt_client_id=%s\n", g_mqtt_config.client_id);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "mqtt_username=%s\n", g_mqtt_config.username);
    fs_write(&file, line, strlen(line));

    snprintf(line, sizeof(line), "mqtt_password=%s\n", g_mqtt_config.password);
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

    /* 寫入 Log Level */
    snprintf(line, sizeof(line), "log_level=%u\n", g_log_level);
    fs_write(&file, line, strlen(line));

    fs_close(&file);

    LOG_INF("Settings saved to flash");
    return 0;
}

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

int settings_load_from_flash(void)
{
    struct fs_file_t file;
    int ret;
    char buffer[4096]; /* 讀取整個檔案的緩衝區 */
    char line[SETTINGS_MAX_LINE];
    char key[SETTINGS_MAX_LINE];
    char value[SETTINGS_MAX_LINE];
    ssize_t bytes_read;
    int buf_pos = 0;
    int line_pos = 0;

    fs_file_t_init(&file);

    ret = fs_open(&file, SETTINGS_FILE_PATH, FS_O_READ);
    if (ret < 0)
    {
        LOG_WRN("Settings file not found, will use defaults");
        return ret;
    }

    /* 讀取整個檔案到緩衝區 */
    bytes_read = fs_read(&file, buffer, sizeof(buffer) - 1);
    fs_close(&file);

    if (bytes_read < 0)
    {
        LOG_ERR("Failed to read settings file: %d", (int)bytes_read);
        return bytes_read;
    }

    buffer[bytes_read] = '\0'; /* null terminate */
    LOG_INF("Loading settings from flash (%d bytes)...", (int)bytes_read);

    /* 逐行解析 */
    while (buf_pos < bytes_read)
    {
        /* 讀取一行 */
        line_pos = 0;
        while (buf_pos < bytes_read && buffer[buf_pos] != '\n' && line_pos < SETTINGS_MAX_LINE - 1)
        {
            line[line_pos++] = buffer[buf_pos++];
        }
        line[line_pos] = '\0';
        buf_pos++; /* skip \n */

        /* 跳過空行 */
        if (line_pos == 0 || line[0] == '#')
        {
            continue;
        }

        /* 解析 key=value */
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

            snprintf(key_name, sizeof(key_name), "ai_phase_type_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.phase_type[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_phase_corr_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.phase_corr[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_zct_ratio_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.zct_ratio[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_light_leak_th_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.light_leak_th[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_heavy_leak_th_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.heavy_leak_th[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_leak_on_time_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.leak_on_time[i] = atoi(value);
                continue;
            }

            snprintf(key_name, sizeof(key_name), "ai_leak_off_time_%d", i);
            if (strcmp(key, key_name) == 0)
            {
                g_ai_config.leak_off_time[i] = atoi(value);
                continue;
            }
        }

        /* 解析 Ig 設定 */
        if (strcmp(key, "ai_ig_threshold") == 0)
        {
            g_ai_config.ig_threshold = atoi(value);
        }
        else if (strcmp(key, "ai_ig_on_time") == 0)
        {
            g_ai_config.ig_on_time = atoi(value);
        }
        else if (strcmp(key, "ai_ig_off_time") == 0)
        {
            g_ai_config.ig_off_time = atoi(value);
        }
        /* 解析需量設定 */
        else if (strcmp(key, "demand_alarm1") == 0)
        {
            g_demand_config.demand_alarm1 = atoi(value);
        }
        else if (strcmp(key, "demand_alarm2") == 0)
        {
            g_demand_config.demand_alarm2 = atoi(value);
        }
        else if (strcmp(key, "pulse_const") == 0)
        {
            g_demand_config.pulse_const = atoi(value);
        }
        else if (strcmp(key, "modem_ip") == 0)
        {
            sscanf(value, "%hhu.%hhu.%hhu.%hhu",
                   &g_demand_config.modem_ip[0],
                   &g_demand_config.modem_ip[1],
                   &g_demand_config.modem_ip[2],
                   &g_demand_config.modem_ip[3]);
        }
        else if (strcmp(key, "modem_apn") == 0)
        {
            strncpy(g_demand_config.apn, value, sizeof(g_demand_config.apn) - 1);
        }
        /* 解析 MQTT 設定 */
        else if (strcmp(key, "mqtt_server_port") == 0)
        {
            g_mqtt_config.server_port = atoi(value);
        }
        else if (strcmp(key, "mqtt_server_url") == 0)
        {
            strncpy(g_mqtt_config.server_url, value, sizeof(g_mqtt_config.server_url) - 1);
        }
        else if (strcmp(key, "mqtt_client_id") == 0)
        {
            strncpy(g_mqtt_config.client_id, value, sizeof(g_mqtt_config.client_id) - 1);
        }
        else if (strcmp(key, "mqtt_username") == 0)
        {
            strncpy(g_mqtt_config.username, value, sizeof(g_mqtt_config.username) - 1);
        }
        else if (strcmp(key, "mqtt_password") == 0)
        {
            strncpy(g_mqtt_config.password, value, sizeof(g_mqtt_config.password) - 1);
        }
        /* 解析設備資訊 */
        else if (strcmp(key, "device_name") == 0)
        {
            strncpy(g_device_info.device_name, value, sizeof(g_device_info.device_name) - 1);
        }
        else if (strcmp(key, "device_location") == 0)
        {
            strncpy(g_device_info.location, value, sizeof(g_device_info.location) - 1);
        }
        /* 解析計量參數 */
        else
        {
            for (int i = 0; i < 6; i++)
            {
                char key_name[64];

                snprintf(key_name, sizeof(key_name), "meter_ct_ratio_%d", i);
                if (strcmp(key, key_name) == 0)
                {
                    g_meter_config.ct_ratio[i] = atoi(value);
                    break;
                }

                snprintf(key_name, sizeof(key_name), "meter_ch_igain_%d", i);
                if (strcmp(key, key_name) == 0)
                {
                    g_meter_config.ch_igain[i] = atoi(value);
                    break;
                }
            }
        }
        /* 解析 Log Level */
        if (strcmp(key, "log_level") == 0)
        {
            uint8_t lvl = (uint8_t)atoi(value);
            if (lvl <= 4)
            {
                g_log_level = lvl;
            }
        }
    } /* end while */

    LOG_INF("Settings loaded from flash");
    return 0;
}

int settings_reset_to_default(void)
{
    settings_init_defaults();
    return settings_save_to_flash();
}
