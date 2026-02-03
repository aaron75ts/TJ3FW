#include "spi_flash.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_flash, CONFIG_LOG_DEFAULT_LEVEL);

/* Get the device pointer from Device Tree Node Label 'mx25r64' */
static const struct device *flash_dev = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

int spi_flash_init(void)
{
    LOG_INF("Initializing SPI flash device...");

    if (!device_is_ready(flash_dev))
    {
        LOG_ERR("Flash device %s is not ready", flash_dev->name);
        LOG_ERR("Check Device Tree configuration and SPI pins");
        return -ENODEV;
    }

    LOG_INF("Flash device %s is ready", flash_dev->name);

    /* 給 flash 一點時間初始化 */
    k_sleep(K_MSEC(10));

    return 0;
}

int spi_flash_read(off_t offset, void *data, size_t len)
{
    int ret = flash_read(flash_dev, offset, data, len);
    if (ret != 0)
    {
        LOG_ERR("Flash read failed at offset 0x%lx (err %d)", (long)offset, ret);
    }
    return ret;
}

int spi_flash_write(off_t offset, const void *data, size_t len)
{
    int ret = flash_write(flash_dev, offset, data, len);
    if (ret != 0)
    {
        LOG_ERR("Flash write failed at offset 0x%lx (err %d)", (long)offset, ret);
    }
    return ret;
}

int spi_flash_erase(off_t offset, size_t size)
{
    int ret = flash_erase(flash_dev, offset, size);
    if (ret != 0)
    {
        LOG_ERR("Flash erase failed at offset 0x%lx, size %zu (err %d)", (long)offset, size, ret);
    }
    return ret;
}

int spi_flash_test(void)
{
    uint8_t write_buf[] = "Hello SPI Flash!";
    uint8_t read_buf[sizeof(write_buf)];
    off_t test_addr = 0x0000; // Testing at the beginning of the flash

    /* 1. Erase 4KB sector (Common minimal erase size) */
    LOG_INF("Erasing...");
    if (spi_flash_erase(test_addr, 4096) != 0)
        return -1;

    /* 2. Write */
    LOG_INF("Writing...");
    if (spi_flash_write(test_addr, write_buf, sizeof(write_buf)) != 0)
        return -2;

    /* 3. Read */
    LOG_INF("Reading...");
    memset(read_buf, 0, sizeof(read_buf));
    if (spi_flash_read(test_addr, read_buf, sizeof(read_buf)) != 0)
        return -3;

    /* 4. Compare */
    if (memcmp(write_buf, read_buf, sizeof(write_buf)) == 0)
    {
        LOG_INF("Flash Test Passed! Read: %s", read_buf);
        return 0;
    }
    else
    {
        LOG_ERR("Flash Test Failed! Read: %s", read_buf);
        return -4;
    }
}
