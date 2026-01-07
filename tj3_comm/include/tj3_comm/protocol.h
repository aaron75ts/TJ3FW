#ifndef TJ3_COMM_PROTOCOL_H_
#define TJ3_COMM_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define TJ3_COMM_STX 0x02u
#define TJ3_COMM_ETX 0x03u

/* nRF54 <-> nRF9151 opcodes from docs/PROTOCOL.md */
#define TJ3_COMM_OP_LOG 0xC0u
#define TJ3_COMM_OP_MQTT_PUB 0xC1u
#define TJ3_COMM_OP_SETTINGS 0xC2u

#define TJ3_COMM_OP_REQ_SETTINGS 0xD0u
#define TJ3_COMM_OP_NET_STATUS 0xD1u
#define TJ3_COMM_OP_RTC_SYNC 0xD2u

    typedef void (*tj3_comm_frame_rx_cb)(uint8_t op, const uint8_t *payload, uint16_t payload_len, void *user_data);

    typedef void (*tj3_comm_frame_tx_cb)(const uint8_t *frame, uint16_t frame_len, void *user_data);

    struct tj3_comm_parser
    {
        uint8_t buf[CONFIG_TJ3_COMM_MAX_PAYLOAD + 7u];
        uint16_t pos;
        uint16_t expected_len;
        bool in_frame;

        tj3_comm_frame_rx_cb on_frame;
        void *user_data;
    };

    void tj3_comm_parser_init(struct tj3_comm_parser *parser, tj3_comm_frame_rx_cb on_frame, void *user_data);

    /* Feed a byte stream into the parser. Returns 0 always; invalid frames are dropped. */
    int tj3_comm_parser_feed(struct tj3_comm_parser *parser, const uint8_t *data, uint16_t len);

    /* Encode an OP+payload into a full frame. out_len is set on success. */
    int tj3_comm_frame_encode(uint8_t op, const uint8_t *payload, uint16_t payload_len,
                              uint8_t *out, uint16_t out_sz, uint16_t *out_len);

    /* Convenience: create MQTT publish payload per docs/PROTOCOL.md:
     * [TopicLen(1B)][TopicBytes][BodyBytes]
     */
    int tj3_comm_mqtt_payload_build(const char *topic, const uint8_t *body, uint16_t body_len,
                                    uint8_t *out, uint16_t out_sz, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* TJ3_COMM_PROTOCOL_H_ */
