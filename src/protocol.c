#include "protocol.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>
#include <stdio.h>

#define STX 0x02
#define ETX 0x03

// Define to send logs as raw text (no framing) for terminal debugging
#define PROTOCOL_RAW_LOGS 1

#define PROTOCOL_RX_BUF_SIZE 1024
#define RING_BUF_SIZE 2048

RING_BUF_DECLARE(rx_ring_buf, RING_BUF_SIZE);
RING_BUF_DECLARE(tx_ring_buf, RING_BUF_SIZE);

// Thread Stack and Control Blocks
#define PROTOCOL_THREAD_STACK_SIZE 1024
#define PROTOCOL_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(rx_thread_stack, PROTOCOL_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(tx_thread_stack, PROTOCOL_THREAD_STACK_SIZE);
struct k_thread rx_thread_data;
struct k_thread tx_thread_data;

// UART Device

// UART Device
// Fallback to DT_NODELABEL if Alias is not found, but prefer Alias for abstraction
// Aliases use hyphens but code refers with underscore usually if defined as such,
// here we use the DT node alias name "nrf9151-uart" or "nrf9151_uart".
// Zephyr DT_ALIAS transforms non-alphanumeric chars to underscore for the Macro itself,
// but the alias name string in DT is what we pass. BUT DT_ALIAS(x) actually expects 'x' to match the alias name
// converted to token. "nrf9151-uart" becomes nrf9151_uart in generated macros.
// So DT_ALIAS(nrf9151_uart) works if alias is "nrf9151-uart".

#if DT_NODE_HAS_STATUS(DT_ALIAS(nrf9151_uart), okay)
const struct device *nrf9151_uart = DEVICE_DT_GET(DT_ALIAS(nrf9151_uart));
#else
const struct device *nrf9151_uart = DEVICE_DT_GET(DT_NODELABEL(uart30));
#endif

typedef enum
{
    STATE_WAIT_STX,
    STATE_WAIT_LEN_L,
    STATE_WAIT_LEN_H,
    STATE_WAIT_DATA, // Reads OP + Payload + ETX
    STATE_WAIT_CRC1,
    STATE_WAIT_CRC2
} protocol_state_t;

static protocol_state_t state = STATE_WAIT_STX;
static uint16_t data_len = 0;
static uint16_t bytes_read = 0;
static uint8_t rx_buf[PROTOCOL_RX_BUF_SIZE];
static uint16_t calc_crc = 0xFFFF;
static uint8_t rx_crc1;
static bool module_initialized = false;

// Forward declarations
static void protocol_process_byte(uint8_t byte);

static void rx_thread_entry(void *p1, void *p2, void *p3)
{
    uint8_t c;
    while (1)
    {
        // Get byte from ring buffer.
        // If empty, sleep a bit to yield.
        // Better: use a semaphore or msgq, but for ring buf polling with sleep is simple.
        // Optimal: k_sem_take(&rx_sem, K_FOREVER) signaled by ISR.
        if (ring_buf_get(&rx_ring_buf, &c, 1) > 0)
        {
            protocol_process_byte(c);
        }
        else
        {
            k_msleep(1); // Yield if empty
        }
    }
}

static void tx_thread_entry(void *p1, void *p2, void *p3)
{
    uint8_t c;
    while (1)
    {
        if (ring_buf_get(&tx_ring_buf, &c, 1) > 0)
        {
            uart_poll_out(nrf9151_uart, c);
        }
        else
        {
            k_msleep(1); // Yield if empty
        }
    }
}

// Weak implementations for handlers
__attribute__((weak)) void protocol_handle_settings_read(void)
{
    protocol_send_log("[PROTO] Settings Read Requested");
}

__attribute__((weak)) void protocol_handle_net_status(uint8_t status)
{
    protocol_send_log("[PROTO] Net Status: %d", status);
}

__attribute__((weak)) void protocol_handle_rtc_sync(uint64_t timestamp)
{
    protocol_send_log("[PROTO] RTC Sync: %llu", timestamp);
}

// Internal CRC16-CCITT update function for streaming (Poly 0x1021)
static uint16_t crc_update(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++)
    {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

static void process_frame(void)
{
    // rx_buf contains: OP + Payload ... + ETX
    // Length is data_len.
    // Ensure data_len >= 2 (OP + ETX)

    if (data_len < 2)
    {
        protocol_send_log("[ERR] Frame too short");
        return;
    }

    uint8_t op_code = rx_buf[0];
    uint8_t etx = rx_buf[data_len - 1];

    if (etx != ETX)
    {
        protocol_send_log("[ERR] ETX mismatch in frame data (found 0x%02X)", etx);
        return;
    }

    // Payload is from rx_buf[1] to rx_buf[data_len-2]
    uint8_t *payload = &rx_buf[1];
    uint16_t payload_len = data_len - 2;

    switch (op_code)
    {
    case PROTOCOL_OP_SETTINGS_READ:
        protocol_handle_settings_read();
        break;
    case PROTOCOL_OP_NET_STATUS:
        if (payload_len >= 1)
        {
            protocol_handle_net_status(payload[0]);
        }
        break;
    case PROTOCOL_OP_RTC_SYNC:
        if (payload_len == sizeof(uint64_t))
        {
            uint64_t ts;
            memcpy(&ts, payload, sizeof(ts));
            protocol_handle_rtc_sync(ts);
        }
        else
        {
            protocol_send_log("[WRN] RTC payload len %d unexpected", payload_len);
        }
        break;
    default:
        protocol_send_log("[WRN] Unknown OP Code: 0x%02X", op_code);
        break;
    }
}

void protocol_process_byte(uint8_t byte)
{
    switch (state)
    {
    case STATE_WAIT_STX:
        if (byte == STX)
        {
            state = STATE_WAIT_LEN_L;
            calc_crc = 0xFFFF; // Reset CRC
            // Note: STX is NOT included in CRC according to doc "from LEN_L to ETX"
        }
        break;

    case STATE_WAIT_LEN_L:
        data_len = byte;
        calc_crc = crc_update(calc_crc, byte);
        state = STATE_WAIT_LEN_H;
        break;

    case STATE_WAIT_LEN_H:
        data_len |= ((uint16_t)byte << 8);
        calc_crc = crc_update(calc_crc, byte);
        if (data_len > PROTOCOL_RX_BUF_SIZE)
        {
            protocol_send_log("[ERR] Frame too large: %d", data_len);
            state = STATE_WAIT_STX;
        }
        else if (data_len == 0)
        {
            // Should at least be OP + ETX = 2
            state = STATE_WAIT_STX;
        }
        else
        {
            bytes_read = 0;
            state = STATE_WAIT_DATA;
        }
        break;

    case STATE_WAIT_DATA:
        rx_buf[bytes_read++] = byte;
        calc_crc = crc_update(calc_crc, byte);
        if (bytes_read >= data_len)
        {
            state = STATE_WAIT_CRC1;
        }
        break;

    case STATE_WAIT_CRC1:
        rx_crc1 = byte;
        state = STATE_WAIT_CRC2;
        break;

    case STATE_WAIT_CRC2:
    {
        uint16_t received_crc = ((uint16_t)rx_crc1 << 8) | byte;
        if (received_crc == calc_crc)
        {
            process_frame();
        }
        else
        {
            protocol_send_log("[ERR] CRC Mismatch: Calc 0x%04X, Rx 0x%04X", calc_crc, received_crc);
        }
        state = STATE_WAIT_STX;
    }
    break;
    }
}

static void uart_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    if (!uart_irq_update(dev))
    {
        return;
    }

    if (uart_irq_rx_ready(dev))
    {
        while (uart_fifo_read(dev, &c, 1) == 1)
        {
            // Push to Ring Buffer
            ring_buf_put(&rx_ring_buf, &c, 1);
        }
    }
}

static int uart_init(void)
{
    if (!device_is_ready(nrf9151_uart))
    {
        // Cannot use LOG_ERR here because logging uses this UART!
        // printk may work if routed RTT, but console is disabled.
        return -1;
    }

    int ret = uart_irq_callback_user_data_set(nrf9151_uart, uart_cb, NULL);
    if (ret < 0)
    {
        return ret;
    }

    uart_irq_rx_enable(nrf9151_uart);

    // Start Threads
    k_thread_create(&rx_thread_data, rx_thread_stack,
                    K_THREAD_STACK_SIZEOF(rx_thread_stack),
                    rx_thread_entry, NULL, NULL, NULL,
                    PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_create(&tx_thread_data, tx_thread_stack,
                    K_THREAD_STACK_SIZEOF(tx_thread_stack),
                    tx_thread_entry, NULL, NULL, NULL,
                    PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);

    module_initialized = true;
    protocol_send_log("[INF] UART device %s is ready (Queued)", nrf9151_uart->name);
    return 0;
}

static void uart_send_bytes(const uint8_t *data, size_t len)
{
    if (!module_initialized)
    {
        return;
    }

    // Push to TX Ring Buffer
    // If buffer is full, we drop data or spin?
    // Allow partial write logic:
    size_t written = 0;
    while (written < len)
    {
        uint32_t ret = ring_buf_put(&tx_ring_buf, &data[written], len - written);
        written += ret;
        if (ret == 0)
        {
            k_usleep(100); // Wait for consumer
        }
    }
}

int protocol_init(void)
{
    state = STATE_WAIT_STX;
    return uart_init();
}

static void send_frame(uint8_t op, const uint8_t *payload, uint16_t length)
{
    uint8_t header[4];
    uint8_t footer[3];                         // ETX + CRC
    uint16_t frame_len_field = 1 + length + 1; // OP + Payload + ETX

    header[0] = STX;
    header[1] = frame_len_field & 0xFF;        // LEN_L
    header[2] = (frame_len_field >> 8) & 0xFF; // LEN_H
    header[3] = op;

    // Calculate CRC
    uint16_t crc = 0xFFFF;
    crc = crc_update(crc, header[1]);
    crc = crc_update(crc, header[2]);
    crc = crc_update(crc, header[3]);

    for (uint16_t i = 0; i < length; i++)
    {
        crc = crc_update(crc, payload[i]);
    }

    uint8_t etx = ETX;
    crc = crc_update(crc, etx);

    footer[0] = etx;
    footer[1] = (crc >> 8) & 0xFF; // CRC1 (MSB)
    footer[2] = crc & 0xFF;        // CRC2 (LSB)

    // Send Header
    uart_send_bytes(header, 4);
    // Send Payload
    if (length > 0 && payload)
    {
        uart_send_bytes(payload, length);
    }
    // Send Footer
    uart_send_bytes(footer, 3);
}

void protocol_send_log(const char *fmt, ...)
{
    char log_buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(log_buf, sizeof(log_buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
#if PROTOCOL_RAW_LOGS
        // Send raw string with newline for debugging
        uart_send_bytes((uint8_t *)log_buf, len);
        uart_send_bytes((uint8_t *)"\n", 1);
#else
        send_frame(PROTOCOL_OP_LOG, (uint8_t *)log_buf, len);
#endif
    }
}

void protocol_send_mqtt(const char *topic, const char *json_content)
{
    // Format: [TopicLen(1B)][Topic][JSON]
    // We need to construct this payload buffer
    size_t t_len = strlen(topic);
    size_t j_len = strlen(json_content);

    if (t_len > 255)
    {
        protocol_send_log("[ERR] Topic too long");
        return;
    }

    static uint8_t buf[1024];
    if (1 + t_len + j_len > sizeof(buf))
    {
        protocol_send_log("[ERR] MQTT payload too big");
        return;
    }

    buf[0] = (uint8_t)t_len;
    memcpy(&buf[1], topic, t_len);
    memcpy(&buf[1 + t_len], json_content, j_len);

    send_frame(PROTOCOL_OP_MQTT, buf, 1 + t_len + j_len);
}

void protocol_send_settings(const char *json_settings)
{
    send_frame(PROTOCOL_OP_SETTINGS_PUSH, (uint8_t *)json_settings, strlen(json_settings));
}
