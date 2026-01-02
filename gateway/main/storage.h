/**
 * OmniaPi Gateway - Storage
 * SPIFFS operations for web files and firmware
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize SPIFFS storage
 * @return ESP_OK on success
 */
esp_err_t storage_init(void);

/**
 * Read file from SPIFFS
 * @param path File path
 * @param buffer Output buffer
 * @param len Buffer length
 * @return Number of bytes read, or -1 on error
 */
int storage_read_file(const char *path, uint8_t *buffer, size_t len);

/**
 * Write file to SPIFFS
 * @param path File path
 * @param data Data to write
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t storage_write_file(const char *path, const uint8_t *data, size_t len);

/**
 * Delete file from SPIFFS
 * @param path File path
 * @return ESP_OK on success
 */
esp_err_t storage_delete_file(const char *path);

/**
 * Check if file exists
 * @param path File path
 * @return true if file exists
 */
bool storage_file_exists(const char *path);

/**
 * Get file size
 * @param path File path
 * @return File size, or -1 on error
 */
int storage_get_file_size(const char *path);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_H
