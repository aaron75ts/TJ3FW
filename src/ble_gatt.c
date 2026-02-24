#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "led.h"
#include "ble_gatt.h"
#include "protocol.h"
#include "spi_flash.h"
#include "fs_handler.h"
#include "di.h"
#include "pulse.h"
#include "button.h" /* 加入按鈕模組 */
#include "settings.h"

LOG_MODULE_REGISTER(ble_gatt, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* ============================================================================
 * Energy Service UUIDs (based on TJ3-nRF spec)
 * Service: 4d6f8c00-4b9a-4c1b-9a61-112233445500
 * ============================================================================ */
#define BT_UUID_ENERGY_SVC_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c00, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Meter Snapshot (READ + NOTIFY): 4d6f8c01... - 20 bytes raw data */
#define BT_UUID_ENERGY_SNAPSHOT_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c01, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Meter Config (READ + WRITE): 4d6f8c02... - 24 bytes raw data */
#define BT_UUID_ENERGY_CONFIG_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c02, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Command (WRITE + INDICATE): 4d6f8c03... - Command with Echo back */
#define BT_UUID_ENERGY_CMD_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c03, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Power Meter Pulse (READ + NOTIFY): 4d6f8c04... - uint32_le (milli-Hz) */
#define BT_UUID_ENERGY_PULSE_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c04, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Alarm Status (READ + NOTIFY): 4d6f8c05... - uint32_le bitmask */
#define BT_UUID_ENERGY_ALARM_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c05, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* ============================================================================
 * Diagnostics Service UUIDs
 * Service: 4d6f8c10-4b9a-4c1b-9a61-112233445500
 * ============================================================================ */
#define BT_UUID_DIAG_SVC_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c10, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Log Count (READ): 4d6f8c11... - uint16_le */
#define BT_UUID_DIAG_LOG_COUNT_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c11, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Log Fetch (WRITE + INDICATE): 4d6f8c12... - Write index, indicate log */
#define BT_UUID_DIAG_LOG_FETCH_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c12, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Log Level (READ + WRITE): 4d6f8c13... - 1 byte */
#define BT_UUID_DIAG_LOG_LEVEL_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c13, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Clear Logs (WRITE + INDICATE): 4d6f8c14... - Clear and return status */
#define BT_UUID_DIAG_CLEAR_LOGS_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c14, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* Log Stream (NOTIFY): 4d6f8c15... - Real-time log stream */
#define BT_UUID_DIAG_LOG_STREAM_VAL \
    BT_UUID_128_ENCODE(0x4d6f8c15, 0x4b9a, 0x4c1b, 0x9a61, 0x112233445500)

/* UUID instances */
static struct bt_uuid_128 energy_svc_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_SVC_VAL);
static struct bt_uuid_128 energy_snapshot_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_SNAPSHOT_VAL);
static struct bt_uuid_128 energy_config_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_CONFIG_VAL);
static struct bt_uuid_128 energy_cmd_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_CMD_VAL);
static struct bt_uuid_128 energy_pulse_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_PULSE_VAL);
static struct bt_uuid_128 energy_alarm_uuid = BT_UUID_INIT_128(BT_UUID_ENERGY_ALARM_VAL);

static struct bt_uuid_128 diag_svc_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_SVC_VAL);
static struct bt_uuid_128 diag_log_count_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_LOG_COUNT_VAL);
static struct bt_uuid_128 diag_log_fetch_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_LOG_FETCH_VAL);
static struct bt_uuid_128 diag_log_level_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_LOG_LEVEL_VAL);
static struct bt_uuid_128 diag_clear_logs_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_CLEAR_LOGS_VAL);
static struct bt_uuid_128 diag_log_stream_uuid = BT_UUID_INIT_128(BT_UUID_DIAG_LOG_STREAM_VAL);

/* Static data for characteristics */
static uint8_t meter_snapshot[20] = {0};
static uint8_t meter_config[sizeof(meter_config_t)] = {0};
static uint32_t power_pulse = 0;  // milli-Hz
static uint32_t alarm_status = 0; // bitmask
static uint16_t log_count = 0;
static uint8_t log_level = 3; // Default: INFO level

/* Forward declarations */
extern const struct bt_gatt_service_static energy_service;
extern const struct bt_gatt_service_static diag_service;

static void protocol_handler(struct bt_conn *conn, const protocol_packet_t *packet)
{
    uint8_t tx_buf[MAX_PAYLOAD_SIZE];
    int tx_len = 0;
    uint8_t dummy_payload[4] = {0x00, 0x01, 0x02, 0x03};
    uint8_t op_code = packet->op_code;

    LOG_INF("Processing OP Code: 0x%02X", op_code);

    switch (op_code)
    {
    /*
     * 測試範例 (Echo):
     * 指令結構: [STX] [LEN_L] [LEN_H] [OP] [DATA...] [ETX] [CRC_L] [CRC_H]
     * Raw Data: 020300F0AB036BED
     * Response: 020300F0AB036BED
     */
    case OP_BLE_TEST_ECHO:
        LOG_INF("CMD: Test Echo. Payload: ");
        for (int i = 0; i < packet->payload_len; i++)
        {
            LOG_INF("%02X", packet->payload[i]);
        }
        LOG_INF("");

        // Echo back the received payload
        tx_len = protocol_compose(op_code, packet->payload, packet->payload_len, tx_buf, sizeof(tx_buf));
        break;

    /*
     * 測試範例:
     * 指令結構: [STX] [LEN_L] [LEN_H] [OP] [DATA...] [ETX] [CRC_L] [CRC_H]
     * Raw Data: 020200F1033B79
     * Response: 020300F10003DF1B (0x00 Success)
     *           020300F1FF032018 (0xFF Fail)
     */
    case OP_BLE_TEST_FLASH:
        LOG_INF("CMD: Test File System");
        int ret = fs_handler_test();
        uint8_t res_payload[1];
        res_payload[0] = (ret == 0) ? 0x00 : 0xFF; // 0x00 Success, 0xFF Fail
        tx_len = protocol_compose(op_code, res_payload, 1, tx_buf, sizeof(tx_buf));
        break;

    case OP_BLE_GET_IOR_INFO:
        LOG_INF("CMD: Get Io/Ior Info");
        // Mock response
        tx_len = protocol_compose(op_code, dummy_payload, 4, tx_buf, sizeof(tx_buf));
        break;

    case OP_BLE_GET_REALTIME_CURR:
        LOG_INF("CMD: Get Realtime Current");
        tx_len = protocol_compose(op_code, dummy_payload, 4, tx_buf, sizeof(tx_buf));
        break;

    case OP_BLE_FS_FAST_FORMAT:
        LOG_INF("CMD: Fast Format File System");
        {
            int rc = fs_handler_fast_format();
            uint8_t res_byte = (rc == 0) ? 0x00 : 0xFF;
            tx_len = protocol_compose(op_code, &res_byte, 1, tx_buf, sizeof(tx_buf));
        }
        break;

    case OP_BLE_FS_LOW_LEVEL_FORMAT:
        LOG_INF("CMD: Low-Level Format File System");
        {
            int rc = fs_handler_low_level_format();
            uint8_t res_byte = (rc == 0) ? 0x00 : 0xFF;
            tx_len = protocol_compose(op_code, &res_byte, 1, tx_buf, sizeof(tx_buf));
        }
        break;

    case OP_BLE_SET_AI_CONFIG:
        LOG_INF("CMD: Set AI Config (DI Timing)");
        {
            /* ⚠️ 安全檢查：必須處於設定模式才能修改 */
            // if (!button_is_set_mode_enabled())
            // {
            //     LOG_WRN("Rejected Set AI Config: Set Mode not enabled");
            //     LOG_WRN("Please long-press 'S' button for 3 seconds");
            //     uint8_t error = 0xFE; /* 授權錯誤 */
            //     tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
            //     break;
            // }

            // Payload: [DI1_ON_L][DI1_ON_H][DI1_OFF_L][DI1_OFF_H][DI2_ON_L][DI2_ON_H][DI2_OFF_L][DI2_OFF_H]
            if (packet->payload_len != 8)
            {
                LOG_ERR("Invalid AI config payload length: %d", packet->payload_len);
                uint8_t error = 0xFF;
                tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
                break;
            }

            // 解析 DI1 設定 (Little Endian / LSB first)
            uint16_t di1_on_time = packet->payload[0] | (packet->payload[1] << 8);
            uint16_t di1_off_time = packet->payload[2] | (packet->payload[3] << 8);

            // 解析 DI2 設定 (Little Endian / LSB first)
            uint16_t di2_on_time = packet->payload[4] | (packet->payload[5] << 8);
            uint16_t di2_off_time = packet->payload[6] | (packet->payload[7] << 8);

            LOG_INF("DI1: ON=%d, OFF=%d", di1_on_time, di1_off_time);
            LOG_INF("DI2: ON=%d, OFF=%d", di2_on_time, di2_off_time);

            // 設定 DI1 和 DI2
            int rc1 = di_set_config(DI_CHANNEL_1, di1_on_time, di1_off_time);
            int rc2 = di_set_config(DI_CHANNEL_2, di2_on_time, di2_off_time);

            if (rc1 == 0 && rc2 == 0)
            {
                // 成功：Echo 回原始 payload
                tx_len = protocol_compose(op_code, packet->payload,
                                          packet->payload_len, tx_buf, sizeof(tx_buf));
                LOG_INF("DI config updated successfully (Set Mode active)");
            }
            else
            {
                // 失敗：回傳錯誤碼
                uint8_t error = 0xFF;
                tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
                LOG_ERR("Failed to update DI config: rc1=%d, rc2=%d", rc1, rc2);
            }
        }
        break;

    case OP_BLE_GET_AI_CONFIG:
        LOG_INF("CMD: Get AI Config (DI Timing)");
        {
            di_config_t di1_cfg, di2_cfg;

            di_get_config(DI_CHANNEL_1, &di1_cfg);
            di_get_config(DI_CHANNEL_2, &di2_cfg);

            uint8_t response[8];
            // Little Endian / LSB first
            response[0] = di1_cfg.on_time & 0xFF;
            response[1] = (di1_cfg.on_time >> 8) & 0xFF;
            response[2] = di1_cfg.off_time & 0xFF;
            response[3] = (di1_cfg.off_time >> 8) & 0xFF;
            response[4] = di2_cfg.on_time & 0xFF;
            response[5] = (di2_cfg.on_time >> 8) & 0xFF;
            response[6] = di2_cfg.off_time & 0xFF;
            response[7] = (di2_cfg.off_time >> 8) & 0xFF;

            tx_len = protocol_compose(op_code, response, 8, tx_buf, sizeof(tx_buf));
            LOG_INF("DI1: ON=%d, OFF=%d", di1_cfg.on_time, di1_cfg.off_time);
            LOG_INF("DI2: ON=%d, OFF=%d", di2_cfg.on_time, di2_cfg.off_time);
        }
        break;

    /*
     * Set MQTT Config (0xA5)
     * Payload: [server_url(81)][server_port_L][server_port_H][client_id(17)][username(33)][password(33)]
     * Total: 167 bytes
     * Response: 0x00 Success, 0xFF Fail
     */
    case OP_BLE_SET_MQTT_CONFIG:
        LOG_INF("CMD: Set MQTT Config");
        {
            const size_t expected = sizeof(mqtt_config_t);
            if (packet->payload_len != expected)
            {
                LOG_ERR("Invalid MQTT config payload: got %d, expected %zu",
                        packet->payload_len, expected);
                uint8_t error = 0xFF;
                tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
                break;
            }
            mqtt_config_t cfg;
            memcpy(&cfg, packet->payload, sizeof(mqtt_config_t));
            /* Ensure null-termination */
            cfg.server_url[sizeof(cfg.server_url) - 1] = '\0';
            cfg.client_id[sizeof(cfg.client_id) - 1] = '\0';
            cfg.username[sizeof(cfg.username) - 1] = '\0';
            cfg.password[sizeof(cfg.password) - 1] = '\0';

            int rc = settings_set_mqtt_config(&cfg);
            uint8_t result = (rc == 0) ? 0x00 : 0xFF;
            tx_len = protocol_compose(op_code, &result, 1, tx_buf, sizeof(tx_buf));
            if (rc == 0)
            {
                LOG_INF("MQTT Config saved: url=%s port=%u", cfg.server_url, cfg.server_port);
            }
            else
            {
                LOG_ERR("Failed to save MQTT Config: rc=%d", rc);
            }
        }
        break;

    /*
     * Get MQTT Config (0xA6)
     * No payload in request.
     * Response payload: [server_url(81)][server_port_L][server_port_H][client_id(17)][username(33)][password(33)]
     */
    case OP_BLE_GET_MQTT_CONFIG:
        LOG_INF("CMD: Get MQTT Config");
        {
            mqtt_config_t cfg;
            if (settings_get_mqtt_config(&cfg) != 0)
            {
                uint8_t error = 0xFF;
                tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
                LOG_ERR("Failed to read MQTT Config from settings");
                break;
            }
            tx_len = protocol_compose(op_code, (const uint8_t *)&cfg,
                                      sizeof(mqtt_config_t), tx_buf, sizeof(tx_buf));
            LOG_INF("MQTT Config: url=%s port=%u cid=%s",
                    cfg.server_url, cfg.server_port, cfg.client_id);
        }
        break;

        /* Handle other OPs... */

    default:
        LOG_WRN("Unknown/Basic OP Code");
        // Echo back with empty payload as acknowledgement or error
        tx_len = protocol_compose(op_code, NULL, 0, tx_buf, sizeof(tx_buf));
        break;
    }

    if (tx_len > 0)
    {
        /* Send Indication via Command Characteristic (Echo back) */
        /* Use UUID based indication for reliable ACK */
        int err = bt_gatt_indicate(conn, &(struct bt_gatt_indicate_params){
                                             .uuid = &energy_cmd_uuid.uuid,
                                             .attr = NULL, /* Will be resolved by UUID */
                                             .data = tx_buf,
                                             .len = tx_len,
                                             .func = NULL, /* No callback needed for simple echo */
                                         });

        if (err)
        {
            LOG_ERR("Indicate failed: %d (len %d)", err, tx_len);
        }
        else
        {
            LOG_DBG("Indicate sent, len: %d", tx_len);
        }
    }
}

static ssize_t write_packet(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset,
                            uint8_t flags)
{
    protocol_packet_t packet;
    int err;

    LOG_DBG("Received data len: %d", len);

    err = protocol_parse((const uint8_t *)buf, len, &packet);
    if (err == 0)
    {
        LOG_INF("Valid Packet! OP: 0x%02X", packet.op_code);

        // Handle Logic
        switch (packet.op_code)
        {
        case 0x01: // Keep the old LED Toggle for testing if needed
            if (packet.payload_len > 0 && packet.payload[0] == 0x01)
            {
                led_on();
            }
            else
            {
                led_off();
            }
            break;
        default:
            protocol_handler(conn, &packet);
            break;
        }
    }
    else
    {
        LOG_ERR("Invalid Packet: err %d", err);
    }

    return len;
}

/* ============================================================================
 * Characteristic Read/Write Handlers
 * ============================================================================ */

/* Meter Snapshot - READ handler */
static ssize_t read_meter_snapshot(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("Read Meter Snapshot");
    return bt_gatt_attr_read(conn, attr, buf, len, offset, meter_snapshot, sizeof(meter_snapshot));
}

/* Meter Config - READ handler */
static ssize_t read_meter_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset)
{
    /* 每次讀取前從 settings 刷新緩衝區 */
    meter_config_t cfg;
    if (settings_get_meter_config(&cfg) == 0)
    {
        memcpy(meter_config, &cfg, sizeof(meter_config_t));
    }
    LOG_DBG("Read Meter Config");
    return bt_gatt_attr_read(conn, attr, buf, len, offset, meter_config, sizeof(meter_config));
}

/* Meter Config - WRITE handler */
static ssize_t write_meter_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    /* ⚠️ 安全檢查：必須處於設定模式才能修改 */
    // if (!button_is_set_mode_enabled())
    // {
    //     LOG_WRN("Rejected Meter Config write: Set Mode not enabled");
    //     LOG_WRN("Please long-press 'S' button for 3 seconds");
    //     return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    // }

    if (offset + len > sizeof(meter_config))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Prepare Write 階段：僅驗證，不實際寫入 */
    if (flags & BT_GATT_WRITE_FLAG_PREPARE)
    {
        return len;
    }

    memcpy(meter_config + offset, buf, len);

    /* 若已接收完整資料，儲存至 settings */
    if (offset + len == sizeof(meter_config_t))
    {
        meter_config_t cfg;
        memcpy(&cfg, meter_config, sizeof(meter_config_t));
        if (settings_set_meter_config(&cfg) == 0)
        {
            LOG_INF("Meter Config saved to settings (Set Mode active)");
        }
        else
        {
            LOG_ERR("Failed to save Meter Config to settings");
        }
    }
    else
    {
        LOG_INF("Meter Config partially updated (offset=%u, len=%u)", offset, len);
    }

    return len;
}

/* Power Pulse - READ handler */
static ssize_t read_power_pulse(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    /* 從脈衝模組讀取實際頻率 (milli-Hz) */
    power_pulse = pulse_get_frequency();
    uint32_t pulse_le = sys_cpu_to_le32(power_pulse);
    LOG_DBG("Read Power Pulse: %u milli-Hz", power_pulse);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &pulse_le, sizeof(pulse_le));
}

/* Alarm Status - READ handler */
static ssize_t read_alarm_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset)
{
    uint32_t alarm_le = sys_cpu_to_le32(alarm_status);
    LOG_DBG("Read Alarm Status: 0x%08X", alarm_status);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &alarm_le, sizeof(alarm_le));
}

/* Log Count - READ handler */
static ssize_t read_log_count(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    uint16_t count_le = sys_cpu_to_le16(log_count);
    LOG_DBG("Read Log Count: %u", log_count);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &count_le, sizeof(count_le));
}

/* Log Fetch - WRITE handler (write index, will indicate log back) */
static ssize_t write_log_fetch(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len < 2)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint16_t log_index = sys_le16_to_cpu(*(uint16_t *)buf);
    LOG_INF("Log Fetch request: index %u", log_index);

    /* 準備日誌回應封包 */
    uint8_t log_response[20];
    memset(log_response, 0, sizeof(log_response));

    /* 格式: [index(2)][timestamp(4)][level(1)][message(13)] */
    sys_put_le16(log_index, &log_response[0]);

    /* 簡化實現：返回當前系統運行時間和日誌計數 */
    uint32_t uptime = k_uptime_get_32();
    sys_put_le32(uptime, &log_response[2]);
    log_response[6] = 2; /* INFO level */

    snprintf((char *)&log_response[7], 13, "Log #%u", log_index);

    /* 發送 Indication */
    bt_gatt_indicate(conn, &(struct bt_gatt_indicate_params){
                               .uuid = &diag_log_fetch_uuid.uuid,
                               .attr = NULL,
                               .data = log_response,
                               .len = sizeof(log_response),
                               .func = NULL,
                           });

    return len;
}

/* Log Level - READ handler */
static ssize_t read_log_level(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("Read Log Level: %u", log_level);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &log_level, sizeof(log_level));
}

/* Log Level - WRITE handler */
static void apply_log_level_runtime(uint8_t level); /* forward declaration */
static ssize_t write_log_level(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len != 1)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t new_level = *(uint8_t *)buf;
    if (new_level > 4)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    log_level = new_level;
    LOG_INF("Log Level set to: %u", log_level);

    /* 套用到 Zephyr runtime log filter（立即生效） */
    apply_log_level_runtime(log_level);

    /* 持久化到 Flash ，重開後仍然生效 */
    if (settings_set_log_level(log_level) != 0)
    {
        LOG_WRN("Failed to persist Log Level");
    }

    return len;
}

/* Clear Logs - WRITE handler */
static ssize_t write_clear_logs(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    LOG_INF("Clear Logs request");

    /* 清除日誌檔案 (可選：刪除並重新創建) */
    uint8_t status = 0x00; // Success

    /* 簡化實現：重置計數器 */
    log_count = 0;

    /* 如果需要實際刪除檔案，可使用：
     * fs_unlink("/lfs/di_events.csv");
     * fs_unlink("/lfs/di_state.txt");
     */

    LOG_INF("Logs cleared successfully");

    // Indicate status back
    bt_gatt_indicate(conn, &(struct bt_gatt_indicate_params){
                               .uuid = &diag_clear_logs_uuid.uuid,
                               .attr = NULL,
                               .data = &status,
                               .len = 1,
                               .func = NULL,
                           });

    return len;
}

/* CCC configuration changed callbacks */
static void snapshot_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Meter Snapshot notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static void pulse_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Power Pulse notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static void alarm_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Alarm Status notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static bool log_stream_notify_enabled = false;
static const struct bt_gatt_attr *log_stream_val_attr = NULL;
static struct bt_conn *current_conn;

static void log_stream_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    log_stream_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    /* CCC descriptor immediately follows the value attribute */
    if (log_stream_val_attr == NULL)
    {
        log_stream_val_attr = attr - 1;
    }
    LOG_INF("Log Stream notifications %s", log_stream_notify_enabled ? "enabled" : "disabled");
}

static void my_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    if (value == BT_GATT_CCC_INDICATE)
    {
        LOG_INF("Indications enabled");
    }
    else
    {
        LOG_INF("Indications disabled");
    }
}

/* ============================================================================
 * GATT Service Definitions
 * ============================================================================ */

/* Define Energy Service */
BT_GATT_SERVICE_DEFINE(energy_service,
                       BT_GATT_PRIMARY_SERVICE(&energy_svc_uuid),

                       /* Meter Snapshot (READ + NOTIFY) */
                       BT_GATT_CHARACTERISTIC(&energy_snapshot_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_meter_snapshot, NULL, NULL),
                       BT_GATT_CCC(snapshot_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Meter Config (READ + WRITE) */
                       BT_GATT_CHARACTERISTIC(&energy_config_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE,
                                              read_meter_config, write_meter_config, NULL),

                       /* Command (WRITE + INDICATE) */
                       BT_GATT_CHARACTERISTIC(&energy_cmd_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_packet, NULL),
                       BT_GATT_CCC(my_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Power Meter Pulse (READ + NOTIFY) */
                       BT_GATT_CHARACTERISTIC(&energy_pulse_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_power_pulse, NULL, NULL),
                       BT_GATT_CCC(pulse_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Alarm Status (READ + NOTIFY) */
                       BT_GATT_CHARACTERISTIC(&energy_alarm_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_alarm_status, NULL, NULL),
                       BT_GATT_CCC(alarm_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* Define Diagnostics Service */
BT_GATT_SERVICE_DEFINE(diag_service,
                       BT_GATT_PRIMARY_SERVICE(&diag_svc_uuid),

                       /* Log Count (READ) */
                       BT_GATT_CHARACTERISTIC(&diag_log_count_uuid.uuid,
                                              BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ,
                                              read_log_count, NULL, NULL),

                       /* Log Fetch (WRITE + INDICATE) */
                       BT_GATT_CHARACTERISTIC(&diag_log_fetch_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_log_fetch, NULL),
                       BT_GATT_CCC(my_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Log Level (READ + WRITE) */
                       BT_GATT_CHARACTERISTIC(&diag_log_level_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_log_level, write_log_level, NULL),

                       /* Clear Logs (WRITE + INDICATE) */
                       BT_GATT_CHARACTERISTIC(&diag_clear_logs_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_clear_logs, NULL),
                       BT_GATT_CCC(my_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Log Stream (NOTIFY) */
                       BT_GATT_CHARACTERISTIC(&diag_log_stream_uuid.uuid,
                                              BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE,
                                              NULL, NULL, NULL),
                       BT_GATT_CCC(log_stream_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );
/* ============================================================================
 * Advertising Data
 * ============================================================================ */

/**
 * @brief Send a raw log message over BLE Log Stream (8c15, NOTIFY).
 *
 * Called from the ext_comm log backend so every Zephyr log line is also
 * pushed to the connected TJ3-GW app.  MTU cap: 244 bytes (L2CAP_TX_MTU=247).
 *
 * @param data  Formatted log text (NOT null-terminated required)
 * @param len   Length in bytes
 */
void ble_gatt_log_stream_send(const uint8_t *data, uint16_t len)
{
    if (!log_stream_notify_enabled || log_stream_val_attr == NULL || current_conn == NULL)
    {
        return;
    }
    /* Clamp to safe notify payload size */
    if (len > 244)
    {
        len = 244;
    }
    int err = bt_gatt_notify(current_conn, log_stream_val_attr, data, len);
    ARG_UNUSED(err); /* avoid log recursion - suppress any error logging here */
}

/* 自定義廣播參數 - 縮短間隔提升桌面版掃描發現率 */
static const struct bt_le_adv_param adv_param = {
    .id = BT_ID_DEFAULT,
    .options = BT_LE_ADV_OPT_CONN,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_1, /* 30ms */
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_1, /* 60ms */
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    /* Only advertise main Energy Service UUID (16 bytes)
     * Diagnostics Service will be discoverable after connection via GATT */
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_ENERGY_SVC_VAL),
};

/* Work item for deferred advertising restart (cannot call bt_le_adv_start
 * or k_sleep directly inside a BT connection callback) */
static struct k_work_delayable adv_restart_work;

static void adv_restart_handler(struct k_work *work)
{
    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err == -EALREADY)
    {
        LOG_WRN("Advertising already running");
        return;
    }
    if (err)
    {
        LOG_ERR("Failed to restart advertising (err %d), retrying in 1 s", err);
        k_work_schedule(&adv_restart_work, K_MSEC(1000));
    }
    else
    {
        LOG_INF("Advertising restarted successfully");
    }
}

/**
 * @brief Apply TJ3 log level to the Zephyr runtime log filter for all modules.
 *
 * TJ3 / Zephyr levels share the same numeric mapping:
 *   0 = NONE, 1 = ERR, 2 = WRN, 3 = INF, 4 = DBG
 * Requires CONFIG_LOG_RUNTIME_FILTERING=y.
 */
static void apply_log_level_runtime(uint8_t level)
{
    if (level > 4)
    {
        level = 4;
    }
    uint16_t src_cnt = log_src_cnt_get(0);
    for (uint16_t i = 0; i < src_cnt; i++)
    {
        log_filter_set(NULL, 0, i, level);
    }
    LOG_INF("Runtime log level set to %u (%s)", level,
            level == 0 ? "NONE" : level == 1 ? "ERR"
                              : level == 2   ? "WRN"
                              : level == 3   ? "INF"
                                             : "DBG");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
    }
    else
    {
        LOG_INF("Connected");
        current_conn = bt_conn_ref(conn);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    /* 不可在 BT callback 中呼叫 k_sleep 或直接重啟廣播（會凍結 system workqueue）。
     * 改用 k_work_delayable 延出到獨立 context 執行。 */
    k_work_schedule(&adv_restart_work, K_MSEC(200));
    LOG_INF("Advertising restart scheduled (200 ms)");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

void ble_gatt_init(void)
{
    int err;

    /* 載入已持久化的 Log Level 並立即套用 runtime filter */
    settings_get_log_level(&log_level);
    LOG_INF("Log Level loaded from flash: %u", log_level);
    apply_log_level_runtime(log_level);

    /* 初始化廣播重啟 work item */
    k_work_init_delayable(&adv_restart_work, adv_restart_handler);

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    /* 使用自定義廣播參數 (30-60ms 間隔) 提升掃描發現率 */
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started (30-60ms interval)");
}

/**
 * @brief 更新 Alarm Status 特徵值中的指定位元
 *
 * 用於 DI 模組更新 BLE Alarm Status (UUID ...8c05)
 * BIT_6: DI1 狀態
 * BIT_7: DI2 狀態
 *
 * @param bit_position 位元位置 (0-31)
 * @param value 位元值 (true=1, false=0)
 */
void ble_gatt_update_alarm_status(uint8_t bit_position, bool value)
{
    if (bit_position >= 32)
    {
        LOG_ERR("Invalid bit position: %u", bit_position);
        return;
    }

    uint32_t mask = 1U << bit_position;

    if (value)
    {
        alarm_status |= mask; /* 設置位元 */
    }
    else
    {
        alarm_status &= ~mask; /* 清除位元 */
    }

    LOG_DBG("Alarm Status updated: BIT_%u = %d, Status = 0x%08X",
            bit_position, value, alarm_status);

    /* TODO: 發送 BLE Notification (需要連接時才能發送) */
}
