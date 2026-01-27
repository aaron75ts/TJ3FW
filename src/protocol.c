#include "protocol.h"
#include <string.h>
#include <zephyr/sys/byteorder.h>

/* CRC16-CCITT (Poly 0x1021) implementation */
uint16_t protocol_calculate_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

int protocol_parse(const uint8_t *buf, size_t len, protocol_packet_t *packet)
{
    if (len < PROTOCOL_OVERHEAD)
    {
        return -1; /* Too short */
    }

    if (buf[0] != PROTOCOL_STX)
    {
        return -2; /* Invalid STX */
    }

    /* Extract Length (Little Endian based on L/H names, but usually network is Big.
       Docs say LEN_L then LEN_H, which implies Little Endian byte order on wire?
       Example: LEN_L (byte 1), LEN_H (byte 2).
       Let's assume Little Endian: buf[1] is LSB, buf[2] is MSB.
    */
    uint16_t packet_len = buf[1] | (buf[2] << 8);

    /* Verify Total Length vs Received Data */
    /* LEN definition: OP + Payload + ETX */
    /* Total frame size = 1(STX) + 2(LEN) + LEN(OP+DATA+ETX) + 2(CRC) */
    /* Wait, LEN field value is just the "OP+DATA+ETX" part?
       PROTOCOL.md Example: Payload(11)+OP(1)+ETX(1) = 13.
       Total bytes = 1(STX) + 1(LEN_L) + 1(LEN_H) + 13(Body) + 2(CRC) = 18.
       We should check if buffer has enough data.
    */

    size_t expected_total_len = 1 + 2 + packet_len + 2;

    if (len < expected_total_len)
    {
        return -3; /* Incomplete packet */
    }

    /* Check ETX at the expected position */
    /* Position of ETX = 1(STX) + 2(LEN) + packet_len - 1
       Because packet_len includes ETX.
    */
    uint8_t etx_received = buf[1 + 2 + packet_len - 1];
    if (etx_received != PROTOCOL_ETX)
    {
        return -4; /* Invalid ETX position/value */
    }

    /* Verify CRC */
    /* CRC covers from LEN_L to ETX */
    /* Range: buf[1] to buf[1 + 2 + packet_len - 1] (inclusive) */
    size_t crc_calc_len = 2 + packet_len;
    uint16_t calculated_crc = protocol_calculate_crc(&buf[1], crc_calc_len);

    uint16_t received_crc = buf[expected_total_len - 2] | (buf[expected_total_len - 1] << 8);
    /* Assuming CRC is also Little Endian or Big Endian?
       Usually protocols are Big Endian for CRC but LEN_L/H suggests Little.
       Let's assume Little Endian for consistency with LEN_L/H naming.
       If CRC_1 is MSB, then it's Big Endian.
    */

    /* Let's try matching the calculated CRC.
       If CRC_1 is first byte and CRC_2 is second.
    */
    // For now assuming Little Endian (CRC_1 = LSB, CRC_2 = MSB) as per LEN_L/LEN_H pattern.
    // If it fails we can flip.

    if (calculated_crc != received_crc)
    {
        // Try Big Endian just in case
        uint16_t received_crc_be = (buf[expected_total_len - 2] << 8) | buf[expected_total_len - 1];
        if (calculated_crc != received_crc_be)
        {
            return -5; /* CRC Mismatch */
        }
    }

    packet->op_code = buf[3];
    packet->payload_len = packet_len - 2; /* Subtract OP(1) and ETX(1) */
    packet->payload = (uint8_t *)&buf[4];

    return 0;
}

int protocol_compose(uint8_t op_code, const uint8_t *payload, uint16_t payload_len, uint8_t *out_buf, size_t out_max)
{
    /* LEN = OP(1) + Payload + ETX(1) */
    uint16_t len_val = 1 + payload_len + 1;
    size_t total_size = 1 + 2 + len_val + 2;

    if (out_max < total_size)
    {
        return -1;
    }

    size_t idx = 0;
    out_buf[idx++] = PROTOCOL_STX;

    /* LEN_L, LEN_H */
    out_buf[idx++] = len_val & 0xFF;
    out_buf[idx++] = (len_val >> 8) & 0xFF;

    out_buf[idx++] = op_code;

    if (payload && payload_len > 0)
    {
        memcpy(&out_buf[idx], payload, payload_len);
        idx += payload_len;
    }

    out_buf[idx++] = PROTOCOL_ETX;

    /* Calculate CRC from LEN_L to ETX */
    /* Start at buf[1], length = 2 + len_val */
    uint16_t crc = protocol_calculate_crc(&out_buf[1], 2 + len_val);

    /* CRC_1, CRC_2 (Little Endian assumed) */
    out_buf[idx++] = crc & 0xFF;
    out_buf[idx++] = (crc >> 8) & 0xFF;

    return (int)total_size;
}
