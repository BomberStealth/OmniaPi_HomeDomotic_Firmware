/**
 * OmniaPi Gateway - OTA Handler
 * Gateway self-update and node firmware distribution
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_CHUNK_SIZE 200
#define NODE_FW_PATH "/spiffs/node_fw.bin"

/**
 * OTA status structure
 */
typedef struct {
    bool in_progress;
    uint32_t total_size;
    uint32_t sent_size;
    int progress_percent;
    char status_message[64];
    bool success;
    bool error;
} ota_status_t;

/**
 * Initialize OTA handler
 * @return ESP_OK on success
 */
esp_err_t ota_handler_init(void);

/**
 * Start gateway OTA update
 * @param data Firmware data chunk
 * @param len Data length
 * @param is_first True if first chunk
 * @param is_last True if last chunk
 * @return ESP_OK on success
 */
esp_err_t ota_handler_gateway_update(const uint8_t *data, size_t len, bool is_first, bool is_last);

/**
 * Store node firmware for later distribution
 * @param data Firmware data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ota_handler_store_node_firmware(const uint8_t *data, size_t len);

/**
 * Start node OTA update via ESP-NOW
 * @param mac Target node MAC address
 * @return ESP_OK on success
 */
esp_err_t ota_handler_start_node_update(const uint8_t *mac);

/**
 * Get current OTA status
 * @return Pointer to status structure
 */
const ota_status_t *ota_handler_get_status(void);

/**
 * Process OTA (call periodically for node OTA)
 */
void ota_handler_process(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_HANDLER_H
