/**
 * OmniaPi Gateway Mesh - NVS Storage
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_storage_init(void);
esp_err_t nvs_storage_save_string(const char *key, const char *value);
esp_err_t nvs_storage_load_string(const char *key, char *value, size_t max_len);
esp_err_t nvs_storage_save_blob(const char *key, const void *data, size_t len);
esp_err_t nvs_storage_load_blob(const char *key, void *data, size_t *len);
esp_err_t nvs_storage_erase(const char *key);
esp_err_t nvs_storage_erase_all(void);

#ifdef __cplusplus
}
#endif

#endif // NVS_STORAGE_H
