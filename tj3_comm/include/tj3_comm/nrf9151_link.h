#ifndef TJ3_COMM_NRF9151_LINK_H_
#define TJ3_COMM_NRF9151_LINK_H_

#include <stdint.h>

#include <tj3_comm/protocol.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int tj3_comm_nrf9151_init(tj3_comm_frame_rx_cb on_frame, void *user_data);

    int tj3_comm_nrf9151_send_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len);

    int tj3_comm_nrf9151_send_log(const char *ascii);

    int tj3_comm_nrf9151_send_settings_json(const char *json);

    int tj3_comm_nrf9151_send_mqtt_publish(const char *topic, const uint8_t *body, uint16_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* TJ3_COMM_NRF9151_LINK_H_ */
