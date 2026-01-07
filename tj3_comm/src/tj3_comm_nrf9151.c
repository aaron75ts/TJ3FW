#include <tj3_comm/nrf9151_link.h>

#include <string.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tj3_comm_nrf9151, LOG_LEVEL_INF);

#define UART_NRF9151_NODE DT_ALIAS(uart_nrp)

BUILD_ASSERT(DT_NODE_HAS_STATUS(UART_NRF9151_NODE, okay), "uart-nrp alias not okay");

static const struct device *uart_dev;
static struct tj3_comm_parser rx_parser;
static struct tj3_comm_parser *g_parser;

static uint8_t rx_buf[256];
static uint8_t rx_buf2[256];

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    switch (evt->type)
    {
    case UART_RX_RDY:
        if (g_parser != NULL && evt->data.rx.len != 0u)
        {
            const uint8_t *data = &evt->data.rx.buf[evt->data.rx.offset];
            (void)tj3_comm_parser_feed(g_parser, data, evt->data.rx.len);
        }
        break;
    case UART_RX_BUF_REQUEST:
        (void)uart_rx_buf_rsp(uart_dev, rx_buf2, sizeof(rx_buf2));
        break;
    case UART_RX_DISABLED:
        (void)uart_rx_enable(uart_dev, rx_buf, sizeof(rx_buf), 50);
        break;
    default:
        break;
    }
}

int tj3_comm_nrf9151_init(tj3_comm_frame_rx_cb on_frame, void *user_data)
{
    uart_dev = DEVICE_DT_GET(UART_NRF9151_NODE);
    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("nRF9151 UART not ready");
        return -ENODEV;
    }

    tj3_comm_parser_init(&rx_parser, on_frame, user_data);
    g_parser = &rx_parser;

    int err = uart_callback_set(uart_dev, uart_cb, NULL);
    if (err)
    {
        LOG_ERR("uart_callback_set failed (%d)", err);
        return err;
    }

    err = uart_rx_enable(uart_dev, rx_buf, sizeof(rx_buf), 50);
    if (err)
    {
        LOG_ERR("uart_rx_enable failed (%d)", err);
        return err;
    }

    LOG_INF("nRF9151 UART link ready");
    return 0;
}

int tj3_comm_nrf9151_send_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len)
{
    if (uart_dev == NULL)
    {
        return -ENODEV;
    }

    uint8_t frame[CONFIG_TJ3_COMM_MAX_PAYLOAD + 7u];
    uint16_t frame_len = 0;
    int err = tj3_comm_frame_encode(op, payload, payload_len, frame, sizeof(frame), &frame_len);
    if (err)
    {
        return err;
    }

    return uart_tx(uart_dev, frame, frame_len, SYS_FOREVER_MS);
}

int tj3_comm_nrf9151_send_log(const char *ascii)
{
    if (ascii == NULL)
    {
        return -EINVAL;
    }
    return tj3_comm_nrf9151_send_frame(TJ3_COMM_OP_LOG, (const uint8_t *)ascii, (uint16_t)strlen(ascii));
}

int tj3_comm_nrf9151_send_settings_json(const char *json)
{
    if (json == NULL)
    {
        return -EINVAL;
    }
    return tj3_comm_nrf9151_send_frame(TJ3_COMM_OP_SETTINGS, (const uint8_t *)json, (uint16_t)strlen(json));
}

int tj3_comm_nrf9151_send_mqtt_publish(const char *topic, const uint8_t *body, uint16_t body_len)
{
    uint8_t payload[CONFIG_TJ3_COMM_MAX_PAYLOAD];
    uint16_t payload_len = 0;
    int err = tj3_comm_mqtt_payload_build(topic, body, body_len, payload, sizeof(payload), &payload_len);
    if (err)
    {
        return err;
    }

    return tj3_comm_nrf9151_send_frame(TJ3_COMM_OP_MQTT_PUB, payload, payload_len);
}
