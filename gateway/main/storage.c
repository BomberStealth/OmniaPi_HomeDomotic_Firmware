/**
 * OmniaPi Gateway - Storage
 * SPIFFS operations for web files and firmware
 */

#include "storage.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define STORAGE_BASE_PATH "/spiffs"

// ============== Public Functions ==============
esp_err_t storage_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS storage");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    // Get partition info
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", total, used);
    }

    return ESP_OK;
}

int storage_read_file(const char *path, uint8_t *buffer, size_t len)
{
    if (path == NULL || buffer == NULL) return -1;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    FILE *f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open file: %s", full_path);
        return -1;
    }

    int bytes_read = fread(buffer, 1, len, f);
    fclose(f);

    return bytes_read;
}

esp_err_t storage_write_file(const char *path, const uint8_t *data, size_t len)
{
    if (path == NULL || data == NULL) return ESP_ERR_INVALID_ARG;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    FILE *f = fopen(full_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create file: %s", full_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Write incomplete: %d/%d bytes", written, len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wrote %d bytes to %s", len, full_path);
    return ESP_OK;
}

esp_err_t storage_delete_file(const char *path)
{
    if (path == NULL) return ESP_ERR_INVALID_ARG;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    if (unlink(full_path) != 0) {
        ESP_LOGW(TAG, "Failed to delete file: %s", full_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted: %s", full_path);
    return ESP_OK;
}

bool storage_file_exists(const char *path)
{
    if (path == NULL) return false;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    struct stat st;
    return (stat(full_path, &st) == 0);
}

int storage_get_file_size(const char *path)
{
    if (path == NULL) return -1;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        return -1;
    }

    return (int)st.st_size;
}

void *storage_open_write(const char *path)
{
    if (path == NULL) return NULL;

    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s%s", STORAGE_BASE_PATH, path);

    FILE *f = fopen(full_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create file: %s", full_path);
        return NULL;
    }

    ESP_LOGI(TAG, "Opened for streaming write: %s", full_path);
    return (void *)f;
}

esp_err_t storage_write_chunk(void *handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL) return ESP_ERR_INVALID_ARG;

    FILE *f = (FILE *)handle;
    size_t written = fwrite(data, 1, len, f);

    if (written != len) {
        ESP_LOGE(TAG, "Write chunk failed: %d/%d", written, len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t storage_close_write(void *handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    FILE *f = (FILE *)handle;
    fclose(f);
    ESP_LOGI(TAG, "Closed streaming write");
    return ESP_OK;
}
