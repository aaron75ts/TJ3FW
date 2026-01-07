/*
 * Copyright (c) 2023, Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT atmel_m90e26

/* Zephyr moved headers to the <zephyr/...> namespace; keep compatibility. */
#if defined(__has_include)
#if __has_include(<zephyr/drivers/uart.h>)
#include <zephyr/drivers/uart.h>
#elif __has_include(<drivers/uart.h>)
#include <drivers/uart.h>
#else
#error "Cannot find UART header (expected <zephyr/drivers/uart.h> or <drivers/uart.h>)"
#endif
#else
#include <drivers/uart.h>
#endif

#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <drivers/sensor/m90e26.h>

LOG_MODULE_REGISTER(M90E26, CONFIG_SENSOR_LOG_LEVEL);

/* M90E26 Register Addresses */
#define M90E26_REG_SOFTRESET 0x00
#define M90E26_REG_CALSTART 0x20
#define M90E26_REG_ADJSTART 0x30
#define M90E26_REG_IRMS 0x48
#define M90E26_REG_URMS 0x49
#define M90E26_REG_PMEAN 0x4A
#define M90E26_REG_QMEAN 0x4B
#define M90E26_REG_FREQ 0x4C
#define M90E26_REG_POWERF 0x4D
#define M90E26_REG_PANGLE 0x4E
#define M90E26_REG_SMEAN 0x4F

#define M90E26_REG_IRMS2 0x68
#define M90E26_REG_PANGLE2 0x6E

/* UART protocol constants */
#define M90E26_UART_START_BYTE 0xFE
/* Datasheet (UART):
 * - Read request : FE, RW_Address, HostChecksum
 *   HostChecksum = RW_Address
 * - Write request: FE, RW_Address, DATA_MSB, DATA_LSB, HostChecksum
 *   HostChecksum = RW_Address + DATA_MSB + DATA_LSB
 */
#define M90E26_UART_READ_BIT 0x80
#define M90E26_UART_READ_REQ_LEN 3
#define M90E26_UART_WRITE_REQ_LEN 5
#define READ_RESP_LEN 3  /* DATA_MSB, DATA_LSB, CHKSUM */
#define WRITE_RESP_LEN 1 /* CHKSUM */
#define MSGQ_MAX_LEN MAX(MAX(READ_RESP_LEN, WRITE_RESP_LEN), MAX(M90E26_UART_READ_REQ_LEN, M90E26_UART_WRITE_REQ_LEN))

struct m90e26_rx_msg
{
    int16_t status; /* 0: ok, <0: errno */
    uint8_t len;
    uint8_t buf[MSGQ_MAX_LEN];
    uint32_t stop_reason;
};

struct m90e26_data
{
    uint16_t urms;
    uint16_t irms;
    uint16_t irms2;
    int16_t pmean;
    int16_t qmean;
    uint16_t smean;
    uint16_t freq;
    int16_t powerf;

    int16_t pangle;
    int16_t pangle2;

    /* UART specific data */
    struct k_msgq rx_msgq;
    uint8_t msgq_buf[sizeof(struct m90e26_rx_msg)];
    uint8_t rx_buf_a[MSGQ_MAX_LEN];
    uint8_t rx_buf_b[MSGQ_MAX_LEN];
    bool rx_using_a;
    uint8_t resp_buf[MSGQ_MAX_LEN];
    uint8_t expected_len;
    uint8_t resp_len;
    bool awaiting_resp;
    bool rx_failed;
    uint32_t rx_stop_reason;
    struct k_sem rx_disabled_sem;
    uint32_t current_baud;
    bool is_initialized;
    struct k_mutex lock;
};

struct m90e26_config
{
    const struct device *uart;
};

static const struct uart_config m90e26_uart_cfg = {
    .baudrate = 9600,
    /* Datasheet: 8N1 */
    .parity = UART_CFG_PARITY_NONE,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

static int m90e26_uart_set_baud(const struct device *dev, uint32_t baud)
{
    const struct m90e26_config *config = dev->config;
    struct m90e26_data *data = dev->data;
    struct uart_config cfg = m90e26_uart_cfg;
    int ret;

    cfg.baudrate = baud;
    ret = uart_configure(config->uart, &cfg);
    if (ret < 0 && ret != -ENOTSUP)
    {
        LOG_ERR("Could not configure UART baud=%u (%d)", baud, ret);
        return ret;
    }

    LOG_WRN("%s: UART cfg v2: %u %u%u%u flow=%u (rc=%d)",
            dev->name,
            cfg.baudrate,
            (unsigned)cfg.data_bits,
            (unsigned)cfg.parity,
            (unsigned)cfg.stop_bits,
            (unsigned)cfg.flow_ctrl,
            ret);

    if (data != NULL)
    {
        data->current_baud = baud;
    }

    return 0;
}

static int m90e26_init(const struct device *dev);

static uint8_t m90e26_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return sum;
}

static int16_t m90e26_reason_to_errno(uint32_t reason)
{
    /* See uart_rx_stop_reason bitmask (UART_ERROR_*). */
    if (reason == 0)
    {
        return -EIO;
    }

    if (reason & UART_ERROR_PARITY)
    {
        return -EILSEQ;
    }
    if (reason & UART_ERROR_FRAMING)
    {
        return -EILSEQ;
    }
    if (reason & UART_BREAK)
    {
        return -EIO;
    }
    if (reason & UART_ERROR_OVERRUN)
    {
        return -EIO;
    }
    if (reason & UART_ERROR_NOISE)
    {
        return -EILSEQ;
    }
    if (reason & UART_ERROR_COLLISION)
    {
        return -EIO;
    }

    return -EIO;
}

static void m90e26_post_rx_ok(struct m90e26_data *data)
{
    struct m90e26_rx_msg msg = {
        .status = 0,
        .len = data->expected_len,
        .stop_reason = 0,
    };

    memcpy(msg.buf, data->resp_buf, MIN((size_t)data->expected_len, sizeof(msg.buf)));
    if (k_msgq_put(&data->rx_msgq, &msg, K_NO_WAIT) != 0)
    {
        LOG_ERR("UART response msgq full");
    }
}

static void m90e26_post_rx_err(struct m90e26_data *data, uint32_t stop_reason)
{
    struct m90e26_rx_msg msg = {
        .status = m90e26_reason_to_errno(stop_reason),
        .len = 0,
        .stop_reason = stop_reason,
    };

    memset(msg.buf, 0, sizeof(msg.buf));
    (void)k_msgq_put(&data->rx_msgq, &msg, K_NO_WAIT);
}

static void uart_cb_handler(const struct device *dev, struct uart_event *evt, void *user_data)
{
    const struct device *sensor = user_data;
    struct m90e26_data *data = sensor->data;

    if (data == NULL)
    {
        LOG_ERR("UART callback received NULL user_data for %s!", dev->name);
        return;
    }

    switch (evt->type)
    {
    case UART_RX_RDY:
    {
        if (!data->awaiting_resp)
        {
            break;
        }

        if (evt->data.rx.len == 0)
        {
            break;
        }

        size_t copy_len = evt->data.rx.len;
        if ((size_t)data->resp_len + copy_len > sizeof(data->resp_buf))
        {
            copy_len = sizeof(data->resp_buf) - (size_t)data->resp_len;
        }

        memcpy(&data->resp_buf[data->resp_len], &evt->data.rx.buf[evt->data.rx.offset], copy_len);
        data->resp_len += (uint8_t)copy_len;

        if (data->resp_len >= data->expected_len)
        {
            m90e26_post_rx_ok(data);
            data->awaiting_resp = false;
            (void)uart_rx_disable(dev);
        }

        LOG_DBG("%s: UART_RX_RDY got=%u/%u", dev->name, data->resp_len, data->expected_len);
        break;
    }
    case UART_RX_STOPPED:
    {
        uint32_t reason = evt->data.rx_stop.reason;

        /* Only report once per transaction to avoid log spam. */
        if (data->awaiting_resp && !data->rx_failed)
        {
            data->rx_failed = true;
            data->rx_stop_reason = reason;
            data->awaiting_resp = false;

            LOG_WRN("%s: UART_RX_STOPPED reason=0x%x got=%u/%u", dev->name,
                    (unsigned)reason,
                    (unsigned)data->resp_len,
                    (unsigned)data->expected_len);

            m90e26_post_rx_err(data, reason);
            (void)uart_rx_disable(dev);
        }
        break;
    }
    case UART_RX_BUF_REQUEST:
    {
        uint8_t *next = data->rx_using_a ? data->rx_buf_b : data->rx_buf_a;
        int rc = uart_rx_buf_rsp(dev, next, sizeof(data->rx_buf_a));
        if (rc == 0)
        {
            data->rx_using_a = !data->rx_using_a;
        }
        else
        {
            LOG_WRN("%s: uart_rx_buf_rsp failed (%d)", dev->name, rc);
        }
        break;
    }

    case UART_RX_BUF_RELEASED:
        break;
    case UART_RX_DISABLED:
    {
        LOG_DBG("%s: UART_RX_DISABLED", dev->name);
        k_sem_give(&data->rx_disabled_sem);
        break;
    }
    case UART_TX_DONE:
        LOG_DBG("%s: UART_TX_DONE", dev->name);
        break;

    case UART_TX_ABORTED:
        break;

    default:
        break;
    }
}

static int m90e26_start_rx(const struct device *dev, uint8_t expected_len)
{
    const struct m90e26_config *config = dev->config;
    struct m90e26_data *data = dev->data;

    data->expected_len = expected_len;
    data->resp_len = 0;
    data->awaiting_resp = true;
    data->rx_failed = false;
    data->rx_stop_reason = 0;
    data->rx_using_a = true;
    memset(data->resp_buf, 0, sizeof(data->resp_buf));

    k_msgq_purge(&data->rx_msgq);

    /* Ensure RX is fully disabled before re-enabling.
     * Without this, some drivers return -EBUSY during back-to-back transactions.
     */
    (void)uart_rx_disable(config->uart);
    (void)k_sem_take(&data->rx_disabled_sem, K_NO_WAIT);
    (void)k_sem_take(&data->rx_disabled_sem, K_MSEC(20));

    int ret = uart_rx_enable(config->uart, data->rx_buf_a, sizeof(data->rx_buf_a), SYS_FOREVER_US);
    if (ret == -EBUSY)
    {
        (void)uart_rx_disable(config->uart);
        (void)k_sem_take(&data->rx_disabled_sem, K_MSEC(20));
        ret = uart_rx_enable(config->uart, data->rx_buf_a, sizeof(data->rx_buf_a), SYS_FOREVER_US);
    }

    return ret;
}

static int m90e26_reg_read(const struct device *dev, uint8_t reg_addr, uint16_t *val)
{
    const struct m90e26_config *config = dev->config;
    struct m90e26_data *data = dev->data;
    uint8_t tx_buf[M90E26_UART_READ_REQ_LEN];
    struct m90e26_rx_msg msg;
    int ret;

    /* Read request: FE, RW_Address, HostChecksum */
    tx_buf[0] = M90E26_UART_START_BYTE;
    tx_buf[1] = reg_addr | M90E26_UART_READ_BIT;
    tx_buf[2] = tx_buf[1];

    ret = m90e26_start_rx(dev, READ_RESP_LEN);
    if (ret != 0)
    {
        LOG_ERR("uart_rx_enable failed: %d", ret);
        return ret;
    }

    ret = uart_tx(config->uart, tx_buf, sizeof(tx_buf), SYS_FOREVER_US);
    if (ret != 0)
    {
        LOG_ERR("uart_tx failed: %d", ret);
        uart_rx_disable(config->uart);
        return ret;
    }
    ret = k_msgq_get(&data->rx_msgq, &msg, K_MSEC(200));
    (void)uart_rx_disable(config->uart);
    if (ret != 0)
    {
        LOG_ERR("Get msgq timeout for reg: 0x%02X (%d)", reg_addr, ret);
        return -ETIMEDOUT;
    }

    if (msg.status != 0)
    {
        LOG_ERR("RX stopped (reason=0x%x) while reading reg 0x%02X (%d)",
                (unsigned)msg.stop_reason, reg_addr, (int)msg.status);
        return msg.status;
    }

    if (msg.len < READ_RESP_LEN)
    {
        LOG_ERR("Short read response for reg 0x%02X: len=%u", reg_addr, (unsigned)msg.len);
        return -EIO;
    }

    uint8_t expected_checksum = m90e26_checksum(msg.buf, 2);
    if (msg.buf[2] != expected_checksum)
    {
        LOG_ERR("Read checksum error. Got %02x, expected %02x", msg.buf[2], expected_checksum);
        return -EIO;
    }

    *val = sys_get_be16(msg.buf);
    LOG_INF("Successfully read value: %u", *val);

    return 0;
}

static int m90e26_reg_write(const struct device *dev, uint8_t reg_addr, uint16_t val)
{
    const struct m90e26_config *config = dev->config;
    struct m90e26_data *data = dev->data;
    uint8_t tx_buf[M90E26_UART_WRITE_REQ_LEN];
    struct m90e26_rx_msg msg;
    int ret = 0;

    /* Write request: FE, RW_Address, DATA_MSB, DATA_LSB, HostChecksum */
    tx_buf[0] = M90E26_UART_START_BYTE;
    tx_buf[1] = reg_addr & ~M90E26_UART_READ_BIT;
    sys_put_be16(val, &tx_buf[2]);
    tx_buf[4] = m90e26_checksum(&tx_buf[1], 3);

    ret = m90e26_start_rx(dev, WRITE_RESP_LEN);
    if (ret != 0)
    {
        LOG_ERR("uart_rx_enable failed: %d", ret);
        return ret;
    }

    ret = uart_tx(config->uart, tx_buf, sizeof(tx_buf), SYS_FOREVER_US);
    if (ret != 0)
    {
        LOG_ERR("Write fail: %d", ret);
        return ret;
    }

    ret = k_msgq_get(&data->rx_msgq, &msg, K_MSEC(200));
    (void)uart_rx_disable(config->uart);
    if (ret != 0)
    {
        LOG_ERR("Get msgq timeout for reg: 0x%02X (baud=%u) (%d)",
                reg_addr, (unsigned)data->current_baud, ret);
        return -ETIMEDOUT;
    }

    if (msg.status != 0)
    {
        LOG_ERR("RX stopped (reason=0x%x) while writing reg 0x%02X (%d)",
                (unsigned)msg.stop_reason, reg_addr, (int)msg.status);
        return msg.status;
    }

    if (msg.len < WRITE_RESP_LEN)
    {
        LOG_ERR("Short write response for reg 0x%02X: len=%u", reg_addr, (unsigned)msg.len);
        return -EIO;
    }

    if (msg.buf[0] != tx_buf[4])
    {
        LOG_ERR("Write checksum error. Got %02x, expected %02x", msg.buf[0], tx_buf[4]);
        return -EIO;
    }
    else
    {
        LOG_INF("m90e26_reg_write: checksum ok: 0x%02X", msg.buf[0]);
    }

    return 0;
}

static int m90e26_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    struct m90e26_data *data = dev->data;
    int ret = 0;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (data->is_initialized == false)
    {
        ret = m90e26_init(dev);
        if (ret != 0)
        {
            LOG_ERR("%s: m90e26 initialization fail", __func__);
            k_mutex_unlock(&data->lock);
            return ret;
        }
        else
        {
            data->is_initialized = true;
        }
    }

    if (chan != SENSOR_CHAN_ALL)
    {
        LOG_WRN("Unsupported channel %d", chan);
        k_mutex_unlock(&data->lock);
        return -ENOTSUP;
    }
    LOG_INF("Fetch start");
    ret |= m90e26_reg_read(dev, M90E26_REG_URMS, &data->urms);
    ret |= m90e26_reg_read(dev, M90E26_REG_IRMS, &data->irms);
    ret |= m90e26_reg_read(dev, M90E26_REG_IRMS2, &data->irms2);
    ret |= m90e26_reg_read(dev, M90E26_REG_PMEAN, (uint16_t *)&data->pmean);
    ret |= m90e26_reg_read(dev, M90E26_REG_QMEAN, (uint16_t *)&data->qmean);
    ret |= m90e26_reg_read(dev, M90E26_REG_FREQ, &data->freq);
    ret |= m90e26_reg_read(dev, M90E26_REG_POWERF, (uint16_t *)&data->powerf);
    ret |= m90e26_reg_read(dev, M90E26_REG_PANGLE, (uint16_t *)&data->pangle);
    ret |= m90e26_reg_read(dev, M90E26_REG_PANGLE2, (uint16_t *)&data->pangle2);
    k_mutex_unlock(&data->lock);
    return ret;
}

static void m90e26_convert_and_set(struct sensor_value *val, int32_t raw_val, int scale)
{
    val->val1 = raw_val / scale;
    val->val2 = (raw_val % scale) * (1000000 / scale);
}

static void m90e26_convert_and_set_signed(struct sensor_value *val, int32_t raw_val, int scale)
{
    int32_t whole = raw_val / scale;
    int32_t frac = raw_val % scale;

    if (frac < 0)
    {
        frac = -frac;
    }

    val->val1 = whole;
    val->val2 = frac * (1000000 / scale);

    if (whole < 0)
    {
        val->val2 = -val->val2;
    }
}

static int m90e26_channel_get(const struct device *dev, enum sensor_channel chan,
                              struct sensor_value *val)
{
    struct m90e26_data *data = dev->data;

    switch ((int)chan)
    {
    case SENSOR_CHAN_VOLTAGE:
        m90e26_convert_and_set(val, data->urms, 100);
        break;
    case SENSOR_CHAN_CURRENT:
        m90e26_convert_and_set(val, data->irms, 1000);
        break;
    case SENSOR_CHAN_M90E26_CURRENT2:
        m90e26_convert_and_set(val, data->irms2, 1000);
        break;
    case SENSOR_CHAN_M90E26_ACTIVE_POWER:
        m90e26_convert_and_set(val, data->pmean, 1000);
        break;
    case SENSOR_CHAN_M90E26_REACTIVE_POWER:
        m90e26_convert_and_set(val, data->qmean, 1000);
        break;
    case SENSOR_CHAN_M90E26_APPARENT_POWER:
        m90e26_convert_and_set(val, data->smean, 1000);
        break;
    case SENSOR_CHAN_M90E26_POWER_FACTOR:
        m90e26_convert_and_set(val, data->powerf, 1000);
        break;
    case SENSOR_CHAN_M90E26_PHASE_ANGLE:
        /* Represent as degrees with 0.1-degree resolution (common in TJ3 docs).
         * If your hardware/firmware uses a different scaling, adjust this divisor.
         */
        m90e26_convert_and_set_signed(val, data->pangle, 10);
        break;
    case SENSOR_CHAN_M90E26_PHASE_ANGLE2:
        m90e26_convert_and_set_signed(val, data->pangle2, 10);
        break;
    default:
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api m90e26_api = {
    .sample_fetch = m90e26_sample_fetch,
    .channel_get = m90e26_channel_get,
};

static int m90e26_setup(const struct device *dev)
{
    const struct m90e26_config *config = dev->config;
    struct m90e26_data *data = dev->data;
    int ret;

    if (!device_is_ready(config->uart))
    {
        LOG_ERR("UART bus is not ready");
        return -ENODEV;
    }

    /* Default to 9600; chip can auto-detect 2400/9600 based on 0xFE. */
    ret = m90e26_uart_set_baud(dev, m90e26_uart_cfg.baudrate);
    if (ret != 0)
    {
        return ret;
    }

    k_msgq_init(&data->rx_msgq, data->msgq_buf, sizeof(struct m90e26_rx_msg), 1);
    k_sem_init(&data->rx_disabled_sem, 0, 1);
    k_mutex_init(&data->lock);

    data->is_initialized = false;
    uart_callback_set(config->uart, uart_cb_handler, (void *)dev);

    return 0;
}

static int m90e26_init(const struct device *dev)
{
    int ret;
    LOG_WRN("m90e26_init");

    /* Try 9600 first, then 2400 as a fallback. */
    static const uint32_t baud_candidates[] = {9600, 2400};
    int last_err = -ETIMEDOUT;
    for (size_t i = 0; i < ARRAY_SIZE(baud_candidates); i++)
    {
        (void)m90e26_uart_set_baud(dev, baud_candidates[i]);

        /* Software Reset */
        LOG_DBG("%s: SOFTRESET tx frame (baud=%u)", dev->name, (unsigned)baud_candidates[i]);
        ret = m90e26_reg_write(dev, M90E26_REG_SOFTRESET, 0x789A);
        if (ret < 0)
        {
            LOG_ERR("Failed to reset device (baud=%u): %d", baud_candidates[i], ret);
            last_err = ret;
            continue;
        }
        k_sleep(K_MSEC(20));

        /* Start metering and measurement calibration */
        ret = m90e26_reg_write(dev, M90E26_REG_CALSTART, 0x5678);
        if (ret < 0)
        {
            LOG_ERR("Failed to start metering calibration (baud=%u): %d", baud_candidates[i], ret);
            last_err = ret;
            continue;
        }
        k_sleep(K_MSEC(20));

        ret = m90e26_reg_write(dev, M90E26_REG_ADJSTART, 0x5678);
        if (ret < 0)
        {
            LOG_ERR("Failed to start measurement calibration (baud=%u): %d", baud_candidates[i], ret);
            last_err = ret;
            continue;
        }
        k_sleep(K_MSEC(20));

        uint16_t reg_buff = 0x00;
        (void)m90e26_reg_read(dev, 0x01, &reg_buff);
        LOG_INF("M90E26 driver initialized via UART (baud=%u)", baud_candidates[i]);
        k_sleep(K_MSEC(20));

        return 0;
    }

    return last_err;
}

#define M90E26_DEFINE(inst)                                               \
    static struct m90e26_data m90e26_data_##inst = {                      \
        .is_initialized = false,                                          \
    };                                                                    \
                                                                          \
    static const struct m90e26_config m90e26_config_##inst = {            \
        .uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                         \
    };                                                                    \
                                                                          \
    DEVICE_DT_INST_DEFINE(inst, &m90e26_setup, NULL, &m90e26_data_##inst, \
                          &m90e26_config_##inst, POST_KERNEL,             \
                          CONFIG_SENSOR_INIT_PRIORITY, &m90e26_api);

DT_INST_FOREACH_STATUS_OKAY(M90E26_DEFINE)
