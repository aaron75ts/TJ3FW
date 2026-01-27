#ifndef FS_HANDLER_H
#define FS_HANDLER_H

#include <zephyr/kernel.h>

/**
 * @brief Initialize the LittleFS File System
 * @return 0 on success, negative errno on failure
 */
int fs_handler_init(void);

/**
 * @brief Test File System (Create, Write, Read, Delete)
 * @return 0 on success
 */
int fs_handler_test(void);

/**
 * @brief Fast format (Quick format using fs_mkfs)
 * @return 0 on success
 */
int fs_handler_fast_format(void);

/**
 * @brief Low-level format (Erase & Remount)
 * @return 0 on success
 */
int fs_handler_low_level_format(void);

/**
 * @brief Append a line to a CSV file
 *
 * @param filename File name (e.g., "/lfs/log.csv")
 * @param line String content to append (should be null-terminated, function adds newline if needed)
 * @return 0 on success
 */
int fs_handler_append_log(const char *filename, const char *line);

/**
 * @brief List files in the root directory
 */
void fs_handler_ls(void);

#endif
