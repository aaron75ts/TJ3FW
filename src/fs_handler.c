#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "fs_handler.h"
#include <zephyr/drivers/flash.h>
#include <flash_map_pm.h>

LOG_MODULE_REGISTER(fs_handler, CONFIG_LOG_DEFAULT_LEVEL);

#define MOUNT_POINT "/lfs"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &cstorage,
    .storage_dev = (void *)PM_ID(littlefs_storage),
    .mnt_point = MOUNT_POINT,
};

static void dump_lfs_flash_params(void)
{
    const struct flash_area *fa;
    int rc = flash_area_open(PM_ID(littlefs_storage), &fa);
    if (rc)
    {
        LOG_ERR("flash_area_open failed: %d", rc);
        return;
    }

    const struct device *dev = fa->fa_dev;

    struct flash_pages_info info;
    rc = flash_get_page_info_by_offs(dev, fa->fa_off, &info);
    if (rc)
    {
        LOG_ERR("flash_get_page_info_by_offs failed: %d", rc);
    }
    else
    {
        LOG_INF("LFS area off=0x%lx size=0x%x erase(page)=0x%x",
                fa->fa_off, fa->fa_size, info.size);
    }

    LOG_INF("read_size=%u prog_size=%u cache_size=%u lookahead=%u",
            cstorage.cfg.read_size, cstorage.cfg.prog_size,
            cstorage.cfg.cache_size, cstorage.cfg.lookahead_size);

    flash_area_close(fa);
}

static bool allow_format_on_mount_error(int mount_rc)
{
    /* mount_rc is negative errno */
    switch (mount_rc)
    {
    case -EIO:
        /* Often indicates "no valid FS" or erased media. Allow formatting. */
        return true;

    case -EINVAL:
        /*
         * Very often indicates geometry/layout mismatch (e.g., block count mismatch),
         * or incompatible on-disk format. DO NOT auto-format; preserve data.
         */
        return false;

    default:
        /* For unexpected errors, be conservative: preserve data. */
        return false;
    }
}

int fs_handler_init(void)
{
    int rc = fs_mount(&lfs_storage_mnt);
    if (rc == 0)
    {
        LOG_INF("LittleFS mounted.");
        dump_lfs_flash_params();

        /* Optional: query stats */
        struct fs_statvfs sb;
        if (fs_statvfs(lfs_storage_mnt.mnt_point, &sb) == 0)
        {
            size_t total = sb.f_frsize * sb.f_blocks;
            size_t free = sb.f_frsize * sb.f_bfree;
            LOG_INF("Size: %u bytes, Free: %u bytes", (unsigned)total, (unsigned)free);
        }
        return 0;
    }

    LOG_ERR("LittleFS mount failed: %d", rc);
    dump_lfs_flash_params();

    if (!allow_format_on_mount_error(rc))
    {
        LOG_ERR("Refusing to format to preserve data. Manual intervention required.");
        return rc;
    }

    LOG_WRN("Mount failed in a way that suggests unformatted media; formatting...");
    rc = fs_handler_fast_format();
    if (rc)
    {
        LOG_ERR("Fast format failed: %d", rc);
        return rc;
    }

    rc = fs_mount(&lfs_storage_mnt);
    if (rc)
    {
        LOG_ERR("Mount after format failed: %d", rc);
        return rc;
    }

    LOG_INF("LittleFS mounted after format.");
    dump_lfs_flash_params();
    return 0;
}

int fs_handler_test(void)
{
    struct fs_file_t file;
    int rc;
    char write_data[] = "Test File System Operation\n";
    char read_data[64] = {0};

    fs_file_t_init(&file);

    LOG_INF("Opening test file...");
    rc = fs_open(&file, MOUNT_POINT "/test.txt", FS_O_CREATE | FS_O_RDWR);
    if (rc < 0)
    {
        LOG_ERR("Fail to open file: %d", rc);
        return rc;
    }

    LOG_INF("Writing...");
    rc = fs_write(&file, write_data, strlen(write_data));
    if (rc < 0)
    {
        LOG_ERR("Fail to write: %d", rc);
        fs_close(&file);
        return rc;
    }

    LOG_INF("Seeking to start...");
    rc = fs_seek(&file, 0, FS_SEEK_SET);
    if (rc < 0)
    {
        LOG_ERR("Fail to seek: %d", rc);
        fs_close(&file);
        return rc;
    }

    LOG_INF("Reading...");
    rc = fs_read(&file, read_data, sizeof(read_data) - 1);
    if (rc < 0)
    {
        LOG_ERR("Fail to read: %d", rc);
        fs_close(&file);
        return rc;
    }

    LOG_INF("Read Data: %s", read_data);

    fs_close(&file);
    return 0;
}

int fs_handler_append_log(const char *filename, const char *line)
{
    struct fs_file_t file;
    int rc;
    char path[64];

    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, filename);

    fs_file_t_init(&file);

    rc = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
    if (rc < 0)
    {
        LOG_ERR("Failed to open %s (err %d)", path, rc);
        /* 嘗試再次挖載檔案系統 */
        if (rc == -EIO || rc == -ENOENT)
        {
            LOG_WRN("Attempting to remount filesystem...");
            /* 只是記錄錯誤，不再次挖載以避免遞迴 */
        }
        return rc;
    }

    rc = fs_write(&file, line, strlen(line));
    if (rc < 0)
    {
        LOG_ERR("Failed to write to %s (err %d)", path, rc);
    }

    // Auto-add newline if not present, for CSV readability
    if (line[strlen(line) - 1] != '\n')
    {
        fs_write(&file, "\n", 1);
    }

    fs_close(&file);
    return rc;
}

void fs_handler_ls(void)
{
    struct fs_dir_t dir;
    int rc;
    static struct fs_dirent entry;

    fs_dir_t_init(&dir);

    rc = fs_opendir(&dir, MOUNT_POINT);
    if (rc < 0)
    {
        LOG_ERR("Error opening dir %s: %d", MOUNT_POINT, rc);
        return;
    }

    LOG_INF("Listing files in %s:", MOUNT_POINT);
    while (1)
    {
        rc = fs_readdir(&dir, &entry);
        if (rc < 0)
        {
            LOG_ERR("Error reading dir: %d", rc);
            break;
        }
        if (entry.name[0] == 0)
        {
            break;
        }

        LOG_INF("  %s (%s, %u bytes)",
                entry.name,
                (entry.type == FS_DIR_ENTRY_DIR) ? "DIR" : "FILE",
                entry.size);
    }

    fs_closedir(&dir);
}

int fs_handler_fast_format(void)
{
    int rc;

    LOG_INF("Starting Fast Format (fs_mkfs)...");

    /* Call fs_mkfs to quickly rebuild filesystem metadata */
    rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)PM_ID(littlefs_storage), NULL, 0);
    if (rc < 0)
    {
        LOG_ERR("fs_mkfs failed: %d", rc);
        return rc;
    }

    LOG_INF("Fast format complete.");
    return 0;
}

int fs_handler_low_level_format(void)
{
    int rc;
    const struct flash_area *fa;

    LOG_INF("Starting File System Format...");

    /* 1. Unmount existing filesystem */
    rc = fs_unmount(&lfs_storage_mnt);
    if (rc < 0 && rc != -EINVAL)
    {
        LOG_WRN("Unmount failed or not mounted: %d", rc);
        /* Continue to erase anyway */
    }

    /* 2. Open Flash Area */
    rc = flash_area_open(PM_ID(littlefs_storage), &fa);
    if (rc < 0)
    {
        LOG_ERR("Failed to open flash area: %d", rc);
        return rc;
    }

    /* 3. Erase the entire partition */
    LOG_INF("Erasing flash area (Size: %u)...", fa->fa_size);
    rc = flash_area_erase(fa, 0, fa->fa_size);
    flash_area_close(fa);

    if (rc < 0)
    {
        LOG_ERR("Failed to erase flash: %d", rc);
        return rc;
    }

    /* 4. Remount to re-initialize LittleFS */
    LOG_INF("Remounting LittleFS...");
    rc = fs_mount(&lfs_storage_mnt);
    if (rc < 0)
    {
        LOG_ERR("Failed to remount after format: %d", rc);
        return rc;
    }

    LOG_INF("Format complete and remounted.");
    return 0;
}
