/**
 * OmniaPi Node Mesh - NVS Storage Implementation
 */

#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS_STOR";
static const char *NVS_NAMESPACE = "omniapi_node";

esp_err_t nvs_storage_init(void)
{
    ESP_LOGI(TAG, "NVS storage initialized");
    return ESP_OK;
}

esp_err_t nvs_storage_save_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_load_string(const char *key, char *value, size_t max_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(handle, key, value, &max_len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_save_blob(const char *key, const void *data, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_load_blob(const char *key, void *data, size_t *len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    // If len is NULL, just check if key exists
    size_t required_size = 0;
    err = nvs_get_blob(handle, key, NULL, &required_size);

    if (err == ESP_OK && data != NULL) {
        size_t read_size = (len != NULL) ? *len : required_size;
        if (read_size >= required_size) {
            err = nvs_get_blob(handle, key, data, &required_size);
            if (len != NULL) *len = required_size;
        } else {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_erase(const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(handle);
        err = ESP_OK;  // Treat not found as success for erase
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_erase_all(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
