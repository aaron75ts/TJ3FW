#ifndef TJ3_COMM_BLE_H_
#define TJ3_COMM_BLE_H_

#include <stdint.h>

#include <tj3_comm/protocol.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int tj3_comm_ble_notify_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len);
    int tj3_comm_ble_init(tj3_comm_frame_rx_cb on_frame, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TJ3_COMM_BLE_H_ */
