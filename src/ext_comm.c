#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_ctrl.h>
#include "protocol.h"
#include "ext_comm.h"

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart30));
static uint8_t log_buf[256];
static uint8_t scratch_buf[1];
static uint8_t protocol_buf[300];

/* Callback for log_output to write into our buffer */
static int buf_char_out(uint8_t *data, size_t length, void *ctx)
{
    size_t *idx = (size_t *)ctx;
    for (size_t i = 0; i < length; i++)
    {
        if (*idx < sizeof(log_buf) - 1)
        {
            log_buf[(*idx)++] = data[i];
        }
    }
    return length;
}

/* Define the log output instance with a dedicated scratch buffer */
LOG_OUTPUT_DEFINE(log_output_custom, buf_char_out, scratch_buf, 1);

static void process(const struct log_backend *const backend,
                    union log_msg_generic *msg)
{
    if (!device_is_ready(uart_dev))
    {
        return;
    }

    size_t len = 0;

    /* Use the generic output processor to format the message into text */
    /* We pass the address of 'len' as the context so buf_char_out can update it */
    log_output_ctx_set(&log_output_custom, &len);

    /* Flags: Level, Timestamp. No Colors (protocol doesn't need ANSI codes) */
    uint32_t flags = LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_FORMAT_SYSLOG;

    log_output_msg_process(&log_output_custom, &msg->log, flags);

    if (len > 0)
    {
        /* Null terminate just in case, though not strictly needed for protocol */
        log_buf[len] = '\0';

        /* Wrap in Protocol Frame */
        int packet_len = protocol_compose(OP_EXT_LOG_OUTPUT, log_buf, len, protocol_buf, sizeof(protocol_buf));

        if (packet_len > 0)
        {
            /* Send via UART Poll (Blocking) to ensure integrity */
            for (int i = 0; i < packet_len; i++)
            {
                uart_poll_out(uart_dev, protocol_buf[i]);
            }
        }
    }
}

static void panic(const struct log_backend *const backend)
{
    /* In panic, we might want to flush or simply do nothing special for now */
}

/* Initialization */
static void init(const struct log_backend *const backend)
{
    /* Device readiness is checked in process() or separate init call */
}

/* Backend API structure */
static const struct log_backend_api log_backend_custom_api = {
    .process = process,
    .panic = panic,
    .init = init,
};

/* Register the backend */
LOG_BACKEND_DEFINE(log_backend_uart_proto, log_backend_custom_api, true);

int ext_comm_init(void)
{
    if (!device_is_ready(uart_dev))
    {
        printk("UART30 not ready for logging\n");
        return -ENODEV;
    }
    return 0;
}
