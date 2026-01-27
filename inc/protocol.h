#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Frame Constants */
#define PROTOCOL_STX 0x02
#define PROTOCOL_ETX 0x03

/* OP Codes - BLE Commands (from APP) */
#define OP_BLE_GET_IOR_INFO 0x66
#define OP_BLE_GET_REALTIME_CURR 0xF6
#define OP_BLE_GET_MAX_CURRENT 0xD8
#define OP_BLE_GET_DEMAND_DATA 0xF9
#define OP_BLE_SET_DEVICE_NAME 0xD5
#define OP_BLE_GET_DEVICE_NAME 0xD6
#define OP_BLE_SET_COMM_DEMAND 0xE5
#define OP_BLE_GET_COMM_DEMAND 0xE6
#define OP_BLE_SET_AI_CONFIG 0xB5
#define OP_BLE_GET_AI_CONFIG 0xB6
#define OP_BLE_SET_MQTT_CONFIG 0xA5
#define OP_BLE_GET_MQTT_CONFIG 0xA6
/* 自訂的 op_code (未在規格書上定義) */
#define OP_BLE_TEST_ECHO 0xF0
#define OP_BLE_TEST_FLASH 0xF1
#define OP_BLE_FS_FAST_FORMAT 0xFA
#define OP_BLE_FS_LOW_LEVEL_FORMAT 0xFB

/* OP Codes - External Communication (to/from nRF9151) */
#define OP_EXT_LOG_OUTPUT 0xC0
#define OP_EXT_MQTT_PUBLISH 0xC1
#define OP_EXT_PUSH_SETTINGS 0xC2
#define OP_EXT_REQUEST_SETTINGS 0xD0
#define OP_EXT_NETWORK_STATUS 0xD1
#define OP_EXT_RTC_SYNC 0xD2

/* Protocol format: [STX][LEN_L][LEN_H][OP][Payload...][ETX][CRC_1][CRC_2] */
/* Header (3) + OP(1) + ETX(1) + CRC(2) = 7 bytes overhead */
#define PROTOCOL_OVERHEAD 7
#define MAX_PAYLOAD_SIZE 240 /* BLE MTU limit buffer */

typedef struct
{
    uint8_t op_code;
    uint8_t *payload;
    uint16_t payload_len;
} protocol_packet_t;

/**
 * @brief Calculate CRC16 for the data
 *
 * @param data Pointer to data
 * @param len Length of data
 * @return uint16_t Calculated CRC
 */
uint16_t protocol_calculate_crc(const uint8_t *data, size_t len);

/**
 * @brief Parse a raw buffer into a protocol packet
 *
 * @param buf Raw buffer received
 * @param len Length of raw buffer
 * @param packet Pointer to packet structure to fill
 * @return int 0 on success, negative on error
 */
int protocol_parse(const uint8_t *buf, size_t len, protocol_packet_t *packet);

/**
 * @brief Compose a frame into a buffer
 *
 * @param op_code Command OP Code
 * @param payload Data payload
 * @param payload_len Length of payload
 * @param out_buf Buffer to store the frame
 * @param out_max Max size of out_buf
 * @return int Total length of frame, or negative on error
 */
int protocol_compose(uint8_t op_code, const uint8_t *payload, uint16_t payload_len, uint8_t *out_buf, size_t out_max);

#endif /* PROTOCOL_H */
