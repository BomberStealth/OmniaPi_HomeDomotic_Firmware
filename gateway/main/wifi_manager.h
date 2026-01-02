/**
 * OmniaPi Gateway - WiFi Manager
 * Handles WiFi Station connection
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WiFi in Station mode
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * Start WiFi connection
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start(void);

/**
 * Check if WiFi is connected
 * @return true if connected
 */
bool wifi_manager_is_connected(void);

/**
 * Get current IP address as string
 * @param buffer Buffer to store IP string
 * @param len Buffer length
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char *buffer, size_t len);

/**
 * Get WiFi RSSI
 * @return RSSI value in dBm
 */
int8_t wifi_manager_get_rssi(void);

/**
 * Get WiFi channel
 * @return Channel number
 */
uint8_t wifi_manager_get_channel(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
