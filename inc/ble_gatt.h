#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdbool.h>
#include <stdint.h>

void ble_gatt_init(void);

/**
 * @brief Push a log string over BLE Log Stream characteristic (8c15, NOTIFY).
 * Safe to call from any context; no-op when not connected or notify disabled.
 */
void ble_gatt_log_stream_send(const uint8_t *data, uint16_t len);

/**
 * @brief 更新 Alarm Status 特徵值中的指定位元
 * @param bit_position 位元位置 (0-31)
 * @param value 位元值 (true=1, false=0)
 */
void ble_gatt_update_alarm_status(uint8_t bit_position, bool value);

#endif
