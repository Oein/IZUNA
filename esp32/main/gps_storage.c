#include "gps_storage.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_littlefs.h"

#define LOCATION_MAX_PAGES 2048U
#define LOCATION_TRIM_PAGES 128U
#define LOCATION_MAX_BYTES (LOCATION_PAGE_SIZE * LOCATION_MAX_PAGES)
#define LOCATION_TRIM_BYTES (LOCATION_PAGE_SIZE * LOCATION_TRIM_PAGES)
#define LOCATION_FILE_PATH "/littlefs/location.bin"
#define LOCATION_TMP_FILE_PATH "/littlefs/location.tmp"

static const char *TAG = "GPS";
static bool littlefs_ready = false;
static SemaphoreHandle_t storage_mutex = NULL;

static uint32_t read_file_size_locked(void)
{
    uint32_t file_size = 0;
    FILE *file = fopen(LOCATION_FILE_PATH, "rb");
    if (file != NULL)
    {
        if (fseek(file, 0, SEEK_END) == 0)
        {
            long size = ftell(file);
            if (size > 0)
            {
                file_size = (uint32_t)size;
            }
        }
        fclose(file);
    }
    return file_size;
}

static bool compact_location_file_locked(void)
{
    FILE *src = fopen(LOCATION_FILE_PATH, "rb");
    if (src == NULL)
    {
        return true;
    }

    if (fseek(src, 0, SEEK_END) != 0)
    {
        fclose(src);
        return false;
    }

    long size = ftell(src);
    if (size < 0)
    {
        fclose(src);
        return false;
    }

    if ((uint32_t)size <= LOCATION_TRIM_BYTES)
    {
        fclose(src);
        int rm_result = remove(LOCATION_FILE_PATH);
        return (rm_result == 0 || errno == ENOENT);
    }

    if (fseek(src, LOCATION_TRIM_BYTES, SEEK_SET) != 0)
    {
        fclose(src);
        return false;
    }

    FILE *dst = fopen(LOCATION_TMP_FILE_PATH, "wb");
    if (dst == NULL)
    {
        fclose(src);
        return false;
    }

    uint8_t buffer[512];
    size_t read_bytes = 0;
    bool ok = true;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, read_bytes, dst) != read_bytes)
        {
            ok = false;
            break;
        }
    }

    if (ferror(src) != 0)
    {
        ok = false;
    }

    fclose(src);
    fclose(dst);

    if (!ok)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    if (remove(LOCATION_FILE_PATH) != 0)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    if (rename(LOCATION_TMP_FILE_PATH, LOCATION_FILE_PATH) != 0)
    {
        (void)remove(LOCATION_TMP_FILE_PATH);
        return false;
    }

    return true;
}

esp_err_t storage_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return ret;
    }

    if (storage_mutex == NULL)
    {
        storage_mutex = xSemaphoreCreateMutex();
        if (storage_mutex == NULL)
        {
            esp_vfs_littlefs_unregister(conf.partition_label);
            return ESP_ERR_NO_MEM;
        }
    }

    littlefs_ready = true;
    return ESP_OK;
}

bool append_location_record(const gps_ble_payload_t *payload)
{
    if (!littlefs_ready || payload == NULL || storage_mutex == NULL)
    {
        return false;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    uint32_t file_size = read_file_size_locked();

    while (file_size + sizeof(*payload) > LOCATION_MAX_BYTES)
    {
        if (!compact_location_file_locked())
        {
            xSemaphoreGive(storage_mutex);
            return false;
        }

        file_size = read_file_size_locked();
    }

    FILE *file = fopen(LOCATION_FILE_PATH, "ab");
    if (file == NULL)
    {
        xSemaphoreGive(storage_mutex);
        return false;
    }

    size_t written = fwrite(payload, 1, sizeof(*payload), file);
    fclose(file);
    xSemaphoreGive(storage_mutex);

    return written == sizeof(*payload);
}

uint32_t get_location_file_size(void)
{
    if (!littlefs_ready || storage_mutex == NULL)
    {
        return 0;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return 0;
    }

    uint32_t file_size = read_file_size_locked();

    xSemaphoreGive(storage_mutex);
    return file_size;
}

bool read_location_page(uint32_t page_index, uint8_t *out_page)
{
    if (!littlefs_ready || out_page == NULL || storage_mutex == NULL)
    {
        return false;
    }

    memset(out_page, 0, LOCATION_PAGE_SIZE);

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    FILE *file = fopen(LOCATION_FILE_PATH, "rb");
    if (file == NULL)
    {
        xSemaphoreGive(storage_mutex);
        return true;
    }

    uint32_t offset = page_index * LOCATION_PAGE_SIZE;
    if (fseek(file, (long)offset, SEEK_SET) == 0)
    {
        (void)fread(out_page, 1, LOCATION_PAGE_SIZE, file);
    }

    fclose(file);
    xSemaphoreGive(storage_mutex);
    return true;
}

bool delete_all_pages(void)
{
    if (!littlefs_ready || storage_mutex == NULL)
    {
        return false;
    }

    if (xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return false;
    }

    int result = remove(LOCATION_FILE_PATH);
    if (result != 0 && errno != ENOENT)
    {
        xSemaphoreGive(storage_mutex);
        return false;
    }

    xSemaphoreGive(storage_mutex);
    return true;
}
