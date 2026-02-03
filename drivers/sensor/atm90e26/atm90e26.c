#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include "atm90e26.h"

#define DT_DRV_COMPAT atmel_atm90e26

LOG_MODULE_REGISTER(atm90e26, CONFIG_SENSOR_LOG_LEVEL);

#define UART_RX_TIMEOUT_MS 200
#define UART_WAIT_DELAY_MS 50
#define UART_FLUSH_DELAY_MS 10

static int atm90e26_uart_read(const struct device *dev, uint8_t addr, uint16_t *val)
{
    const struct atm90e26_config *cfg = dev->config;
    uint8_t tx_buf[3];
    uint8_t rx_buf[3];
    int received = 0;
    int64_t timeout_time;
    uint8_t dummy;
    int flush_count = 0;

    /* Flush RX buffer thoroughly */
    while (uart_poll_in(cfg->uart_dev, &dummy) == 0)
    {
        flush_count++;
        if (flush_count > 100)
            break; /* Prevent infinite loop */
    }
    if (flush_count > 0)
    {
        LOG_DBG("Flushed %d bytes before read", flush_count);
        k_msleep(UART_FLUSH_DELAY_MS);
    }

    /* Prepare Read Frame: 0xFE, Addr|0x80, Checksum(Addr|0x80) */
    tx_buf[0] = 0xFE;
    tx_buf[1] = addr | 0x80;
    tx_buf[2] = (addr | 0x80) & 0xFF;

    for (int i = 0; i < 3; i++)
    {
        uart_poll_out(cfg->uart_dev, tx_buf[i]);
    }

    /* Wait for response: MSB, LSB, Checksum (Big-Endian) */
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
        LOG_ERR("Read timeout on %s for reg 0x%02x (received %d/3 bytes)",
                cfg->uart_dev->name, addr, received);
        if (received > 0)
        {
            LOG_WRN("Partial data: 0x%02X 0x%02X 0x%02X",
                    rx_buf[0], rx_buf[1], rx_buf[2]);
        }
        else
        {
            LOG_ERR("No response from device - check hardware connection");
        }
        return -EIO;
    }

    /* Parse response: Big-Endian format (MSB first, LSB second) */
    *val = (rx_buf[0] << 8) | rx_buf[1];

    LOG_DBG("Read reg 0x%02x = 0x%04x", addr, *val);

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
    uint8_t dummy;
    int flush_count = 0;

    /* Flush RX buffer thoroughly */
    while (uart_poll_in(cfg->uart_dev, &dummy) == 0)
    {
        flush_count++;
        if (flush_count > 100)
            break;
    }
    if (flush_count > 0)
    {
        LOG_DBG("Flushed %d bytes before write", flush_count);
        k_msleep(UART_FLUSH_DELAY_MS);
    }

    uint8_t lsb = val & 0xFF;
    uint8_t msb = (val >> 8) & 0xFF;
    uint8_t chk = (addr + lsb + msb) & 0xFF;

    /* Big-Endian format: MSB first, LSB second */
    tx_buf[0] = 0xFE;
    tx_buf[1] = addr;
    tx_buf[2] = msb;
    tx_buf[3] = lsb;
    tx_buf[4] = chk;

    LOG_DBG("Writing to reg 0x%02x = 0x%04x", addr, val);

    for (int i = 0; i < 5; i++)
    {
        uart_poll_out(cfg->uart_dev, tx_buf[i]);
    }

    /* Special handling for Soft Reset (0x00): may not respond */
    if (addr == ATM90E26_REG_SOFTRESET)
    {
        LOG_DBG("Soft reset sent, not waiting for response");
        /* Try to read response but don't treat timeout as error */
        k_msleep(10);
        if (uart_poll_in(cfg->uart_dev, &rx_byte) == 0)
        {
            LOG_DBG("Soft reset response: 0x%02X", rx_byte);
        }
        else
        {
            LOG_DBG("No response from soft reset (normal)");
        }
        k_msleep(UART_WAIT_DELAY_MS);
        return 0;
    }

    /* For normal writes, read 1 byte response (Checksum/Status) */
    timeout_time = k_uptime_get() + UART_RX_TIMEOUT_MS;

    while (received < 1 && k_uptime_get() < timeout_time)
    {
        if (uart_poll_in(cfg->uart_dev, &rx_byte) == 0)
        {
            received++;
            LOG_DBG("Write response: 0x%02X", rx_byte);
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
    const struct atm90e26_config *cfg = dev->config;
    int ret;

    /* Check if device is initialized */
    if (!data->initialized)
    {
        LOG_ERR("ATM90E26 on %s not initialized", cfg->uart_dev->name);
        return -ENODEV;
    }

    /* Read Voltage */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_URMS, &data->voltage);
    LOG_DBG("Voltage read: %u", data->voltage);
    if (ret < 0)
        return ret;

    /* Read Current L-line */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_IRMS, &data->current);
    LOG_DBG("Current L-line read: %u", data->current);
    if (ret < 0)
        return ret;

    /* Read Current N-line */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_IRMS2, &data->current2);
    LOG_DBG("Current N-line read: %u", data->current2);
    if (ret < 0)
        return ret;

    /* Read Active Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_PMEAN, &data->power);
    LOG_DBG("Active Power read: %u", data->power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_PMEAN2, &data->power2);
    LOG_DBG("Active Power N-line read: %u", data->power2);
    if (ret < 0)
        return ret;

    /* Read Reactive Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_QMEAN, &data->reactive_power);
    LOG_DBG("Reactive Power read: %u", data->reactive_power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_QMEAN2, &data->reactive_power2);
    LOG_DBG("Reactive Power N-line read: %u", data->reactive_power2);
    if (ret < 0)
        return ret;

    /* Read Apparent Power */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_SMEAN, &data->apparent_power);
    LOG_DBG("Apparent Power read: %u", data->apparent_power);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_SMEAN2, &data->apparent_power2);
    LOG_DBG("Apparent Power N-line read: %u", data->apparent_power2);
    if (ret < 0)
        return ret;

    /* Read Frequency */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_FREQ, &data->freq);
    LOG_DBG("Frequency read: %u", data->freq);
    if (ret < 0)
        return ret;

    /* Read Power Factor */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_POWERF, &data->power_factor);
    LOG_DBG("Power Factor read: %u", data->power_factor);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_POWERF2, &data->power_factor2);
    LOG_DBG("Power Factor N-line read: %u", data->power_factor2);
    if (ret < 0)
        return ret;

    /* Read Phase Angle */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_PANGLE, &data->phase_angle);
    LOG_DBG("Phase Angle read: %u", data->phase_angle);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_PANGLE2, &data->phase_angle2);
    LOG_DBG("Phase Angle N-line read: %u", data->phase_angle2);
    if (ret < 0)
        return ret;

    /* Read Energy (Note: APenergy auto-clears after read) */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_APENERGY, &data->energy_active_p);
    LOG_DBG("Active Positive Energy read: %u", data->energy_active_p);
    if (ret < 0)
        return ret;

    ret = atm90e26_uart_read(dev, ATM90E26_REG_ANENERGY, &data->energy_active_n);
    LOG_DBG("Active Negative Energy read: %u", data->energy_active_n);
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

static int atm90e26_verify_register(const struct device *dev, uint8_t reg_addr, uint16_t *val)
{
    uint16_t read_val, lastdata_val;
    int ret;

    /* Step 1: Read target register */
    ret = atm90e26_uart_read(dev, reg_addr, &read_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to read register 0x%02X", reg_addr);
        return ret;
    }

    /* Step 2: Read LastData register to verify */
    ret = atm90e26_uart_read(dev, ATM90E26_REG_LASTDATA, &lastdata_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to read LastData register");
        return ret;
    }

    /* Step 3: Compare values */
    if (read_val != lastdata_val)
    {
        LOG_WRN("Register 0x%02X mismatch: read=0x%04X, lastdata=0x%04X (using LastData)",
                reg_addr, read_val, lastdata_val);
        *val = lastdata_val; /* Use LastData as the correct value */
    }
    else
    {
        LOG_DBG("Register 0x%02X verified: 0x%04X", reg_addr, read_val);
        *val = read_val;
    }

    return 0;
}

static int atm90e26_init_chip(const struct device *dev)
{
    const struct atm90e26_config *cfg = dev->config;
    uint16_t cs1, cs2;
    uint16_t metering_regs[11] = {0};
    uint16_t measurement_regs[10] = {0};
    uint16_t test_val;
    int ret;

    LOG_INF("Starting chip initialization for %s", cfg->uart_dev->name);

    /* Test UART by trying to clear RX buffer */
    uint8_t test_byte;
    int clear_count = 0;
    while (uart_poll_in(cfg->uart_dev, &test_byte) == 0)
    {
        clear_count++;
        if (clear_count > 100)
            break;
    }
    if (clear_count > 0)
    {
        LOG_WRN("Cleared %d bytes from UART RX buffer before init", clear_count);
    }
    LOG_DBG("UART %s ready for communication", cfg->uart_dev->name);

    /* 1. Soft Reset  */
    LOG_INF("Performing soft reset...");
    atm90e26_uart_write(dev, ATM90E26_REG_SOFTRESET, ATM90E26_SOFTRESET_MAGIC);
    k_msleep(200); /* Wait for reset - increased from 50ms to 200ms */

    /* 2. Verify chip is responding by reading System Status with LastData verification */
    LOG_DBG("Verifying chip response...");
    ret = atm90e26_verify_register(dev, ATM90E26_REG_SYSSTATUS, &test_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to read System Status after reset (error %d)", ret);
        LOG_ERR("Chip may not be responding or UART connection issue");
        return ret;
    }
    LOG_INF("System Status after reset: 0x%04X", test_val);

    /* 3. 寫入 FuncEn (功能啟用寄存器) */
    LOG_DBG("Setting FuncEn register...");
    atm90e26_uart_write(dev, ATM90E26_REG_FUNCEN, 0x000C); /* 禁用電壓驟降/方向變化中斷 */
    k_msleep(25);

    /* 3.1. 寫入 SagTh (電壓驟降閾值) */
    LOG_DBG("Setting SagTh register...");
    atm90e26_uart_write(dev, ATM90E26_REG_SAGTH, 0x1F2F);
    k_msleep(25);

    /* 4. Calibration Start  */
    LOG_DBG("Starting calibration...");
    atm90e26_uart_write(dev, ATM90E26_REG_CALSTART, ATM90E26_CALSTART_MAGIC);
    k_msleep(50); /* Wait for calibration mode */

    /* 4. 設定基本計量參數 (21H-2BH) - 使用規格書推薦的預設值 */
    metering_regs[0] = 0x0015;  // 21H PLconstH - 脈衝常數高位
    metering_regs[1] = 0xD174;  // 22H PLconstL - 脈衝常數低位
    metering_regs[2] = 0x0000;  // 23H Lgain - L線增益
    metering_regs[3] = 0x0000;  // 24H Lphi - L線相位
    metering_regs[4] = 0x0000;  // 25H Ngain - N線增益
    metering_regs[5] = 0x0000;  // 26H Nphi - N線相位
    metering_regs[6] = 0x08BD;  // 27H PStartTh - 有功功率起動閾值
    metering_regs[7] = 0x0000;  // 28H PNOLTh - 有功功率無負載閾值
    metering_regs[8] = 0x0AEC;  // 29H QStartTh - 無功功率起動閾值
    metering_regs[9] = 0x0000;  // 2AH QNOLTh - 無功功率無負載閾值
    metering_regs[10] = 0x9422; // 2BH MMode - 計量模式配置

    atm90e26_uart_write(dev, ATM90E26_REG_PLCONSTH, metering_regs[0]);
    atm90e26_uart_write(dev, ATM90E26_REG_PLCONSTL, metering_regs[1]);
    atm90e26_uart_write(dev, ATM90E26_REG_LGAIN, metering_regs[2]);
    atm90e26_uart_write(dev, ATM90E26_REG_LPHI, metering_regs[3]);
    atm90e26_uart_write(dev, ATM90E26_REG_NGAIN, metering_regs[4]);
    atm90e26_uart_write(dev, ATM90E26_REG_NPHI, metering_regs[5]);
    atm90e26_uart_write(dev, ATM90E26_REG_PSTARTTH, metering_regs[6]);
    atm90e26_uart_write(dev, ATM90E26_REG_PNOLTH, metering_regs[7]);
    atm90e26_uart_write(dev, ATM90E26_REG_QSTARTTH, metering_regs[8]);
    atm90e26_uart_write(dev, ATM90E26_REG_QNOLTH, metering_regs[9]);
    atm90e26_uart_write(dev, ATM90E26_REG_MMODE, metering_regs[10]);

    /* 6. Calculate and Write CS1 */
    cs1 = atm90e26_calculate_cs1(metering_regs);
    atm90e26_uart_write(dev, ATM90E26_REG_CS1, cs1);
    LOG_INF("CS1 calculated: 0x%04X", cs1);
    k_msleep(50);

    /* 7. 校驗計量設定 (Lock configuration) */
    LOG_DBG("Locking metering configuration...");
    atm90e26_uart_write(dev, ATM90E26_REG_CALSTART, ATM90E26_CALCHECK_MAGIC);
    k_msleep(50);

    /* 7.1. 驗證 CS1 - 讀取 System Status */
    LOG_DBG("Verifying CS1...");
    ret = atm90e26_verify_register(dev, ATM90E26_REG_SYSSTATUS, &test_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify CS1");
        return ret;
    }
    LOG_INF("System Status after CS1: 0x%04X", test_val);
    if (test_val & 0x4000)
    {
        LOG_ERR("CS1 checksum error detected (Bit 14)");
        return -EINVAL;
    }

    /* 8. Measurement Calibration Start */
    LOG_DBG("Starting measurement calibration...");
    atm90e26_uart_write(dev, ATM90E26_REG_ADJSTART, ATM90E26_ADJSTART_MAGIC);
    k_msleep(50);

    /* 9. 設定測量校正參數 (31H-3AH) - 使用規格書預設值 */
    measurement_regs[0] = 0x6720; // 31H Ugain - 電壓增益 (規格書預設值)
    measurement_regs[1] = 0x0A13; // 32H IgainL - L線電流增益
    measurement_regs[2] = 0x0A13; // 33H IgainN - N線電流增益
    measurement_regs[3] = 0x0000; // 34H Uoffset - 電壓偏移量
    measurement_regs[4] = 0x0000; // 35H IoffsetL - L線電流偏移量
    measurement_regs[5] = 0x0000; // 36H IoffsetN - N線電流偏移量
    measurement_regs[6] = 0x0000; // 37H PoffsetL - L線有功功率偏移量
    measurement_regs[7] = 0x0000; // 38H QoffsetL - L線無功功率偏移量
    measurement_regs[8] = 0x0000; // 39H PoffsetN - N線有功功率偏移量
    measurement_regs[9] = 0x0000; // 3AH QoffsetN - N線無功功率偏移量

    atm90e26_uart_write(dev, ATM90E26_REG_UGAIN, measurement_regs[0]);
    atm90e26_uart_write(dev, ATM90E26_REG_IGAINL, measurement_regs[1]);
    atm90e26_uart_write(dev, ATM90E26_REG_IGAINN, measurement_regs[2]);
    atm90e26_uart_write(dev, ATM90E26_REG_UOFFSET, measurement_regs[3]);
    atm90e26_uart_write(dev, ATM90E26_REG_IOFFSETL, measurement_regs[4]);
    atm90e26_uart_write(dev, ATM90E26_REG_IOFFSETN, measurement_regs[5]);
    atm90e26_uart_write(dev, ATM90E26_REG_POFFSETL, measurement_regs[6]);
    atm90e26_uart_write(dev, ATM90E26_REG_QOFFSETL, measurement_regs[7]);
    atm90e26_uart_write(dev, ATM90E26_REG_POFFSETN, measurement_regs[8]);
    atm90e26_uart_write(dev, ATM90E26_REG_QOFFSETN, measurement_regs[9]);

    /* 10. Calculate and Write CS2 */
    cs2 = atm90e26_calculate_cs2(measurement_regs);
    atm90e26_uart_write(dev, ATM90E26_REG_CS2, cs2);
    LOG_INF("CS2 calculated: 0x%04X", cs2);
    k_msleep(50);

    /* 11. 校驗測量設定 (Lock configuration) */
    LOG_DBG("Locking measurement configuration...");
    atm90e26_uart_write(dev, ATM90E26_REG_ADJSTART, ATM90E26_ADJCHECK_MAGIC);
    k_msleep(100); /* Final settling time */

    /* 11.1. 驗證 CS2 - 讀取 System Status */
    LOG_DBG("Verifying CS2...");
    ret = atm90e26_verify_register(dev, ATM90E26_REG_SYSSTATUS, &test_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify CS2");
        return ret;
    }
    LOG_INF("System Status after CS2: 0x%04X", test_val);
    if (test_val & 0x2000)
    {
        LOG_ERR("CS2 checksum error detected (Bit 13)");
        return -EINVAL;
    }

    /* 12. Verify configuration by reading back System Status with LastData verification */
    LOG_DBG("Final configuration verification...");
    ret = atm90e26_verify_register(dev, ATM90E26_REG_SYSSTATUS, &test_val);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify configuration (error %d)", ret);
        return ret;
    }
    LOG_INF("Configuration complete. Final System Status: 0x%04X", test_val);

    /* Check for calibration errors in System Status */
    if (test_val & 0x8000)
    {
        LOG_WRN("System Status indicates error (bit 15 set)");
    }
    if (test_val & 0x4000)
    {
        LOG_WRN("CS1 checksum error (bit 14 set)");
    }
    if (test_val & 0x2000)
    {
        LOG_WRN("CS2 checksum error (bit 13 set)");
    }

    return 0;
}

static int atm90e26_init(const struct device *dev)
{
    const struct atm90e26_config *cfg = dev->config;
    struct atm90e26_data *data = dev->data;
    int ret;
    int retry_count = 0;
    const int max_retries = 3;

    LOG_INF("========================================");
    LOG_INF("Initializing ATM90E26 on UART %s...", cfg->uart_dev->name);
    LOG_INF("UART device pointer: %p", (void *)cfg->uart_dev);
    LOG_INF("========================================");

    /* Initialize data structure */
    data->initialized = false;

    /* Check UART device */
    if (cfg->uart_dev == NULL)
    {
        LOG_ERR("UART device pointer is NULL!");
        return -ENODEV;
    }

    if (!device_is_ready(cfg->uart_dev))
    {
        LOG_ERR("UART device %s not ready!", cfg->uart_dev->name);
        LOG_ERR("Check device tree configuration");
        return -ENODEV;
    }
    LOG_INF("UART device %s is ready", cfg->uart_dev->name);

    /* Initialize Chip with retry */
    while (retry_count < max_retries)
    {
        if (retry_count > 0)
        {
            LOG_WRN("Retry %d/%d", retry_count, max_retries - 1);
            k_msleep(500);
        }

        ret = atm90e26_init_chip(dev);
        if (ret == 0)
        {
            data->initialized = true;
            LOG_INF("========================================");
            LOG_INF("ATM90E26 on %s initialized successfully!", cfg->uart_dev->name);
            LOG_INF("========================================");
            return 0;
        }

        LOG_ERR("Init attempt %d failed: %d", retry_count + 1, ret);
        retry_count++;
    }

    LOG_ERR("========================================");
    LOG_ERR("Failed to init ATM90E26 on %s after %d attempts",
            cfg->uart_dev->name, max_retries);
    LOG_ERR("Check hardware connections and power supply");
    LOG_ERR("========================================");
    return -EIO;
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
