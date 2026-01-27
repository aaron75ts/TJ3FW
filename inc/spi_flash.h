#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>

/**
 * @brief Initialize the SPI Flash
 * @return 0 on success, negative errno on failure
 */
int spi_flash_init(void);

/**
 * @brief Read data from SPI Flash
 *
 * @param offset Address offset to read from
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return 0 on success, negative errno on failure
 */
int spi_flash_read(off_t offset, void *data, size_t len);

/**
 * @brief Write data to SPI Flash
 *
 * @param offset Address offset to write to
 * @param data Buffer containing data to write
 * @param len Number of bytes to write
 * @return 0 on success, negative errno on failure
 */
int spi_flash_write(off_t offset, const void *data, size_t len);

/**
 * @brief Erase a region in SPI Flash
 *
 * @param offset Address offset to start erase
 * @param size Size of the region to erase (must be aligned to sector size)
 * @return 0 on success, negative errno on failure
 */
int spi_flash_erase(off_t offset, size_t size);

/**
 * @brief Perform a self-test (Erase-Write-Read)
 *
 * @return 0 on success, negative errno on failure
 */
int spi_flash_test(void);

#endif /* SPI_FLASH_H */
