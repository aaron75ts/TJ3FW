#include <tj3_comm/protocol.h>

#include <stdbool.h>
#include <string.h>
#include <errno.h>

static uint16_t tj3_comm_crc16_ccitt_false(const uint8_t *data, uint16_t len)
{
    /* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, refin/out=false, xorout=0 */
    uint16_t crc = 0xFFFFu;

    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8u; bit++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

void tj3_comm_parser_init(struct tj3_comm_parser *parser, tj3_comm_frame_rx_cb on_frame, void *user_data)
{
    memset(parser, 0, sizeof(*parser));
    parser->on_frame = on_frame;
    parser->user_data = user_data;
}

static void parser_reset(struct tj3_comm_parser *parser)
{
    parser->pos = 0;
    parser->expected_len = 0;
    parser->in_frame = false;
}

static void parser_try_dispatch(struct tj3_comm_parser *parser)
{
    /* buf contains full frame: STX .. CRC2 */
    if (parser->pos < 7u)
    {
        parser_reset(parser);
        return;
    }

    if (parser->buf[0] != TJ3_COMM_STX)
    {
        parser_reset(parser);
        return;
    }

    uint16_t len_field = (uint16_t)parser->buf[1] | ((uint16_t)parser->buf[2] << 8);
    /* LEN = OP + PAYLOAD + ETX (per docs/PROTOCOL.md example) */
    uint16_t payload_len = 0;
    if (len_field < 2u)
    {
        parser_reset(parser);
        return;
    }
    payload_len = len_field - 2u;

    uint16_t crc_input_len = 2u /* LEN_L,H */ + 1u /* OP */ + payload_len + 1u /* ETX */;
    uint16_t crc_expected = tj3_comm_crc16_ccitt_false(&parser->buf[1], crc_input_len);
    uint16_t crc_got = (uint16_t)parser->buf[4u + payload_len] | ((uint16_t)parser->buf[5u + payload_len] << 8);

    uint8_t etx = parser->buf[3u + payload_len];
    uint8_t op = parser->buf[3];
    const uint8_t *payload = &parser->buf[4];

    if (etx != TJ3_COMM_ETX || crc_got != crc_expected)
    {
        parser_reset(parser);
        return;
    }

    if (parser->on_frame != NULL)
    {
        parser->on_frame(op, payload, payload_len, parser->user_data);
    }

    parser_reset(parser);
}

int tj3_comm_parser_feed(struct tj3_comm_parser *parser, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        uint8_t b = data[i];

        if (!parser->in_frame)
        {
            if (b == TJ3_COMM_STX)
            {
                parser->in_frame = true;
                parser->pos = 0;
                parser->buf[parser->pos++] = b;
            }
            continue;
        }

        if (parser->pos >= sizeof(parser->buf))
        {
            parser_reset(parser);
            continue;
        }

        parser->buf[parser->pos++] = b;

        if (parser->pos == 3u)
        {
            /* got LEN bytes */
            uint16_t len_field = (uint16_t)parser->buf[1] | ((uint16_t)parser->buf[2] << 8);
            /* Total frame bytes = STX(1) + LEN(2) + LEN_FIELD(OP+PAYLOAD+ETX) + CRC(2) */
            parser->expected_len = (uint16_t)(1u + 2u + len_field + 2u);
            if (parser->expected_len > sizeof(parser->buf) || parser->expected_len < 7u)
            {
                parser_reset(parser);
            }
        }

        if (parser->expected_len != 0u && parser->pos == parser->expected_len)
        {
            parser_try_dispatch(parser);
        }
    }

    return 0;
}

int tj3_comm_frame_encode(uint8_t op, const uint8_t *payload, uint16_t payload_len,
                          uint8_t *out, uint16_t out_sz, uint16_t *out_len)
{
    if (out == NULL || out_len == NULL)
    {
        return -EINVAL;
    }

    uint16_t len_field = (uint16_t)(1u + payload_len + 1u); /* OP + PAYLOAD + ETX */
    uint16_t total = (uint16_t)(1u + 2u + len_field + 2u);
    if (total > out_sz)
    {
        return -ENOMEM;
    }

    out[0] = TJ3_COMM_STX;
    out[1] = (uint8_t)(len_field & 0xFFu);
    out[2] = (uint8_t)((len_field >> 8) & 0xFFu);
    out[3] = op;
    if (payload_len != 0u && payload != NULL)
    {
        memcpy(&out[4], payload, payload_len);
    }
    out[4u + payload_len] = TJ3_COMM_ETX;

    uint16_t crc_input_len = (uint16_t)(2u + 1u + payload_len + 1u);
    uint16_t crc = tj3_comm_crc16_ccitt_false(&out[1], crc_input_len);
    out[5u + payload_len] = (uint8_t)(crc & 0xFFu);
    out[6u + payload_len] = (uint8_t)((crc >> 8) & 0xFFu);

    *out_len = total;
    return 0;
}

int tj3_comm_mqtt_payload_build(const char *topic, const uint8_t *body, uint16_t body_len,
                                uint8_t *out, uint16_t out_sz, uint16_t *out_len)
{
    if (topic == NULL || out == NULL || out_len == NULL)
    {
        return -EINVAL;
    }

    size_t topic_len = strlen(topic);
    if (topic_len > 255u)
    {
        return -EINVAL;
    }

    uint16_t total = (uint16_t)(1u + (uint16_t)topic_len + body_len);
    if (total > out_sz)
    {
        return -ENOMEM;
    }

    out[0] = (uint8_t)topic_len;
    memcpy(&out[1], topic, topic_len);
    if (body_len != 0u && body != NULL)
    {
        memcpy(&out[1u + topic_len], body, body_len);
    }
    *out_len = total;
    return 0;
}
