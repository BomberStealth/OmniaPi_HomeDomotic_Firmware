/**
 * OmniaPi Node Mesh - NVS Storage Handler
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize NVS storage
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_init(void);

/**
 * Save string to NVS
 * @param key   NVS key
 * @param value String value
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_save_string(const char *key, const char *value);

/**
 * Load string from NVS
 * @param key     NVS key
 * @param value   Buffer to store value
 * @param max_len Maximum buffer length
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_load_string(const char *key, char *value, size_t max_len);

/**
 * Save blob to NVS
 * @param key  NVS key
 * @param data Data to save
 * @param len  Data length
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_save_blob(const char *key, const void *data, size_t len);

/**
 * Load blob from NVS
 * @param key  NVS key
 * @param data Buffer to store data
 * @param len  Pointer to length (in: max size, out: actual size)
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_load_blob(const char *key, void *data, size_t *len);

/**
 * Erase key from NVS
 * @param key NVS key
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_erase(const char *key);

/**
 * Erase all keys in namespace
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_erase_all(void);

#ifdef __cplusplus
}
#endif

#endif // NVS_STORAGE_H
