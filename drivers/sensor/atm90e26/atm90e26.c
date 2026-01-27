#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include "atm90e26.h"

#define DT_DRV_COMPAT atmel_atm90e26

LOG_MODULE_REGISTER(atm90e26, CONFIG_SENSOR_LOG_LEVEL);

#define UART_RX_TIMEOUT_MS 100
#define UART_WAIT_DELAY_MS 25

static int atm90e26_uart_read(const struct device *dev, uint8_t addr, uint16_t *val)
{
    const struct atm90e26_config *cfg = dev->config;
    uint8_t tx_buf[3];
    uint8_t rx_buf[3];
    int received = 0;
    int64_t timeout_time;

    /* Flush RX */
    while (uart_poll_in(cfg->uart_dev, &rx_buf[0]) == 0)
        ;

    /* Prepare Read Frame: 0xFE, Addr|0x80, Checksum(Addr|0x80) */
    tx_buf[0] = 0xFE;
    tx_buf[1] = addr | 0x80;
    tx_buf[2] = (addr | 0x80) & 0xFF;

    for (int i = 0; i < 3; i++)
    {
        uart_poll_out(cfg->uart_dev, tx_buf[i]);
    }

    /* Wait for response: LSB, MSB, Checksum */
    timeout_time = k_uptime_get() + UART_RX_TIMEOUT_MS;

    while (received < 3 && k_uptime_get() < timeout_time)
    {
        if (uart_poll_in(cfg->uart_dev, &rx_buf[received]) == 0)
        {
            received++;
        }
        else
        {
            k_busy_wait(100); /* Small wait */
        }
    }

    if (received < 3)
    {
        LOG_ERR("Read timeout for reg 0x%02x", addr);
        return -EIO;
    }

    /* Verify Checksum: (LSB + MSB + Addr|0x80) & 0xFF ? No, doc says CS is last byte */
    /* Sample code says: RX: {res[0]} {res[1]} {res[2]} .. checksum=res[2] */
    /* But how is it calculated? Main.py doesn't verify RX checksum logic strictly, it just prints it. */
    /* Let's trust the value for now or assume simple Sum */

    *val = (rx_buf[1] << 8) | rx_buf[0];

    /* Mandatory delay between transactions */
    k_msleep(UART_WAIT_DELAY_MS);

    return 0;
}

static int atm90e26_uart_write(const struct device *dev, uint8_t addr, uint16_t val)
{
    const struct atm90e26_config *cfg = dev->config;
    uint8_t tx_buf[5];
    uint8_t rx_byte;
    int received = 0;
    int64_t timeout_time;

    /* Flush RX */
    while (uart_poll_in(cfg->uart_dev, &rx_byte) == 0)
        ;

    uint8_t lsb = val & 0xFF;
    uint8_t msb = (val >> 8) & 0xFF;
    uint8_t chk = (addr + lsb + msb) & 0xFF;

    tx_buf[0] = 0xFE;
    tx_buf[1] = addr;
    tx_buf[2] = lsb;
    tx_buf[3] = msb;
    tx_buf[4] = chk;

    for (int i = 0; i < 5; i++)
    {
        uart_poll_out(cfg->uart_dev, tx_buf[i]);
    }

    /* Read 1 byte response (Checksum/Status) */
    timeout_time = k_uptime_get() + UART_RX_TIMEOUT_MS;

    while (received < 1 && k_uptime_get() < timeout_time)
    {
        if (uart_poll_in(cfg->uart_dev, &rx_byte) == 0)
        {
            received++;
        }
        else
        {
            k_busy_wait(100);
        }
    }

    if (received < 1)
    {
        LOG_ERR("Write timeout for reg 0x%02x", addr);
        return -EIO;
    }

    k_msleep(UART_WAIT_DELAY_MS);

    return 0;
}

static int atm90e26_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    struct atm90e26_data *data = dev->data;
    int ret;

    /* Read Voltage */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_URMS, &data->voltage);
    if (ret < 0)
        return ret;

    /* Read Current L-line */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_IRMS, &data->current);
    if (ret < 0)
        return ret;

    /* Read Current N-line */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_IRMS2, &data->current2);
    if (ret < 0)
        return ret;

    /* Read Active Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_PMEAN, &data->power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_PMEAN2, &data->power2);
    if (ret < 0)
        return ret;

    /* Read Reactive Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_QMEAN, &data->reactive_power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_QMEAN2, &data->reactive_power2);
    if (ret < 0)
        return ret;

    /* Read Apparent Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_SMEAN, &data->apparent_power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_SMEAN2, &data->apparent_power2);
    if (ret < 0)
        return ret;

    /* Read Frequency */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_FREQ, &data->freq);
    if (ret < 0)
        return ret;

    /* Read Power Factor */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_POWERF, &data->power_factor);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_POWERF2, &data->power_factor2);
    if (ret < 0)
        return ret;

    /* Read Phase Angle */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_PANGLE, &data->phase_angle);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_PANGLE2, &data->phase_angle2);
    if (ret < 0)
        return ret;

    /* Read Energy (Note: APenergy auto-clears after read) */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_APENERGY, &data->energy_active_p);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_ANENERGY, &data->energy_active_n);
    if (ret < 0)
        return ret;

    return 0;
}

static int atm90e26_channel_get(const struct device *dev,
                                enum sensor_channel chan,
                                struct sensor_value *val)
{
    struct atm90e26_data *data = dev->data;

    switch ((int)chan)
    {
    case SENSOR_CHAN_VOLTAGE:
        /* Voltage format: XXX.XX V */
        val->val1 = data->voltage / 100;
        val->val2 = (data->voltage % 100) * 10000;
        break;
    case SENSOR_CHAN_CURRENT:
        /* Current format: XX.XXX A (L-line) */
        val->val1 = data->current / 1000;
        val->val2 = (data->current % 1000) * 1000;
        break;
    case SENSOR_CHAN_POWER:
        /* Active Power (signed 16-bit) */
        val->val1 = (int16_t)data->power;
        val->val2 = 0;
        break;
    case SENSOR_CHAN_ATM90E26_REACTIVE_POWER:
        /* Reactive Power */
        val->val1 = (int16_t)data->reactive_power;
        val->val2 = 0;
        break;
    case SENSOR_CHAN_ATM90E26_APPARENT_POWER:
        /* Apparent Power */
        val->val1 = data->apparent_power;
        val->val2 = 0;
        break;
    case SENSOR_CHAN_ATM90E26_POWER_FACTOR:
        /* Power Factor: 0.XXX format (value / 1000) */
        val->val1 = 0;
        val->val2 = data->power_factor * 1000;
        break;
    case SENSOR_CHAN_FREQUENCY:
        /* Frequency: XX.XX Hz */
        val->val1 = data->freq / 100;
        val->val2 = (data->freq % 100) * 10000;
        break;
    case SENSOR_CHAN_ATM90E26_ENERGY:
        /* Active Positive Energy (auto-clear after read) */
        val->val1 = data->energy_active_p;
        val->val2 = 0;
        break;
    default:
        return -ENOTSUP;
    }

    return 0;
}

static uint16_t atm90e26_calculate_cs1(uint16_t regs[11])
{
    /* CS1 calculation for registers 21H-2BH (11 registers total)
     * Formula: CS1 = (XOR_H << 8) | SUM_L
     * where SUM_L = sum of all LSB and MSB bytes
     *       XOR_H = XOR of all LSB and MSB bytes
     */
    uint8_t sum_l = 0;
    uint8_t xor_h = 0;

    for (int i = 0; i < 11; i++)
    {
        uint8_t h = (regs[i] >> 8) & 0xFF;
        uint8_t l = regs[i] & 0xFF;
        sum_l = (sum_l + h + l) & 0xFF;
        xor_h = xor_h ^ h ^ l;
    }

    return (xor_h << 8) | sum_l;
}

static uint16_t atm90e26_calculate_cs2(uint16_t regs[10])
{
    /* CS2 calculation for registers 31H-3AH (10 registers total)
     * Formula: CS2 = (XOR_H << 8) | SUM_L
     * where SUM_L = sum of all LSB and MSB bytes
     *       XOR_H = XOR of all LSB and MSB bytes
     */
    uint8_t sum_l = 0;
    uint8_t xor_h = 0;

    for (int i = 0; i < 10; i++)
    {
        uint8_t h = (regs[i] >> 8) & 0xFF;
        uint8_t l = regs[i] & 0xFF;
        sum_l = (sum_l + h + l) & 0xFF;
        xor_h = xor_h ^ h ^ l;
    }

    return (xor_h << 8) | sum_l;
}

static int atm90e26_init_chip(const struct device *dev)
{
    uint16_t cs1, cs2;
    uint16_t metering_regs[11] = {0};
    uint16_t measurement_regs[10] = {0};

    /* 1. Soft Reset  */
    atm90e26_uart_write(dev, ATM90E26_REG_SOFTRESET, ATM90E26_SOFTRESET_MAGIC);
    k_msleep(50); /* Wait for reset */

    /* 2. Calibration Start  */
    atm90e26_uart_write(dev, ATM90E26_REG_CALSTART, ATM90E26_CALSTART_MAGIC);

    /* 3. 設定基本計量參數 (21H-2BH) */
    metering_regs[0] = PLCONSTH_DEFAULT; // 21H
    metering_regs[1] = PLCONSTL_DEFAULT; // 22H
    metering_regs[2] = 0x0000;           // 23H Lgain
    metering_regs[3] = 0x0000;           // 24H Lphi
    metering_regs[4] = 0x0000;           // 25H Ngain
    metering_regs[5] = 0x0000;           // 26H Nphi
    metering_regs[6] = PSTARTTH_DEFAULT; // 27H
    metering_regs[7] = 0x0000;           // 28H PNOLTh
    metering_regs[8] = 0x0000;           // 29H QStartTh
    metering_regs[9] = 0x0000;           // 2AH QNOLTh
    metering_regs[10] = MMODE_DEFAULT;   // 2BH

    atm90e26_uart_write(dev, ATM90E26_REG_PLCONSTH, metering_regs[0]);
    atm90e26_uart_write(dev, ATM90E26_REG_PLCONSTL, metering_regs[1]);
    atm90e26_uart_write(dev, ATM90E26_REG_PSTARTTH, metering_regs[6]);
    atm90e26_uart_write(dev, ATM90E26_REG_MMODE, metering_regs[10]);

    /* 4. Calculate and Write CS1 */
    cs1 = atm90e26_calculate_cs1(metering_regs);
    atm90e26_uart_write(dev, ATM90E26_REG_CS1, cs1);
    LOG_INF("CS1 calculated: 0x%04X", cs1);

    /* 5. 校驗計量設定 (Lock configuration) */
    atm90e26_uart_write(dev, ATM90E26_REG_CALSTART, ATM90E26_CALCHECK_MAGIC);

    /* 6. Measurement Calibration Start */
    atm90e26_uart_write(dev, ATM90E26_REG_ADJSTART, ATM90E26_ADJSTART_MAGIC);

    /* 7. 設定測量校正參數 (31H-3AH) - Using default values */
    measurement_regs[0] = 0x7A22; // 31H Ugain
    measurement_regs[1] = 0x0CB4; // 32H IgainL
    measurement_regs[2] = 0x0693; // 33H IgainN
    measurement_regs[3] = 0x0000; // 34H Uoffset
    measurement_regs[4] = 0x0000; // 35H IoffsetL
    measurement_regs[5] = 0x0000; // 36H IoffsetN
    measurement_regs[6] = 0x0000; // 37H PoffsetL
    measurement_regs[7] = 0x0000; // 38H QoffsetL
    measurement_regs[8] = 0x0000; // 39H PoffsetN
    measurement_regs[9] = 0x0000; // 3AH QoffsetN

    atm90e26_uart_write(dev, ATM90E26_REG_UGAIN, measurement_regs[0]);
    atm90e26_uart_write(dev, ATM90E26_REG_IGAINL, measurement_regs[1]);
    atm90e26_uart_write(dev, ATM90E26_REG_IGAINN, measurement_regs[2]);

    /* 8. Calculate and Write CS2 */
    cs2 = atm90e26_calculate_cs2(measurement_regs);
    atm90e26_uart_write(dev, ATM90E26_REG_CS2, cs2);
    LOG_INF("CS2 calculated: 0x%04X", cs2);

    /* 9. 校驗測量設定 (Lock configuration) */
    atm90e26_uart_write(dev, ATM90E26_REG_ADJSTART, ATM90E26_ADJCHECK_MAGIC);

    return 0;
}

static int atm90e26_init(const struct device *dev)
{
    const struct atm90e26_config *cfg = dev->config;
    int ret;

    if (!device_is_ready(cfg->uart_dev))
    {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    /* Initialize Chip */
    ret = atm90e26_init_chip(dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to init ATM90E26");
        return ret;
    }

    return 0;
}

static const struct sensor_driver_api atm90e26_api = {
    .sample_fetch = atm90e26_sample_fetch,
    .channel_get = atm90e26_channel_get,
};

#define ATM90E26_INIT(n)                                            \
    static struct atm90e26_data atm90e26_data_##n;                  \
                                                                    \
    static const struct atm90e26_config atm90e26_config_##n = {     \
        .uart_dev = DEVICE_DT_GET(DT_PROP(DT_DRV_INST(n), uart)),   \
    };                                                              \
                                                                    \
    DEVICE_DT_INST_DEFINE(n, atm90e26_init, NULL,                   \
                          &atm90e26_data_##n, &atm90e26_config_##n, \
                          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
                          &atm90e26_api);

DT_INST_FOREACH_STATUS_OKAY(ATM90E26_INIT)
