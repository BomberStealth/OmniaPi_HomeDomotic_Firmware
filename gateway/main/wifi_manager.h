/**
 * OmniaPi Gateway - WiFi Manager
 * Handles WiFi Station + AP mode with NVS credential storage
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi mode
typedef enum {
    WIFI_MODE_NOT_INITIALIZED,
    WIFI_MODE_STA_CONNECTING,
    WIFI_MODE_STA_CONNECTED,
    WIFI_MODE_AP_ACTIVE
} wifi_manager_mode_t;

/**
 * Initialize WiFi subsystem (NVS, netif, event loop)
 * Must be called before any other wifi_manager function
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * Check if WiFi credentials are saved in NVS
 * @return true if credentials exist
 */
bool wifi_manager_has_credentials(void);

/**
 * Get saved SSID from NVS
 * @param buffer Buffer to store SSID
 * @param len Buffer length
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_saved_ssid(char *buffer, size_t len);

/**
 * Save WiFi credentials to NVS
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * Clear saved WiFi credentials from NVS
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * Start WiFi in Station mode using saved credentials
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return ESP_OK on success, ESP_FAIL on connection failure
 */
esp_err_t wifi_manager_connect(uint32_t timeout_ms);

/**
 * Start WiFi in Station mode with specific credentials (for testing)
 * Does NOT save to NVS
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect_to(const char *ssid, const char *password, uint32_t timeout_ms);

/**
 * Start WiFi in Access Point mode
 * SSID will be "OmniaPi-XXXX" where XXXX is last 4 chars of MAC
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * Stop WiFi (both STA and AP)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop(void);

/**
 * Check if WiFi is connected (Station mode)
 * @return true if connected
 */
bool wifi_manager_is_connected(void);

/**
 * Get current WiFi mode
 * @return Current mode
 */
wifi_manager_mode_t wifi_manager_get_mode(void);

/**
 * Get current IP address as string
 * @param buffer Buffer to store IP string
 * @param len Buffer length
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char *buffer, size_t len);

/**
 * Get WiFi RSSI (Station mode only)
 * @return RSSI value in dBm
 */
int8_t wifi_manager_get_rssi(void);

/**
 * Get WiFi channel
 * @return Channel number
 */
uint8_t wifi_manager_get_channel(void);

/**
 * Get AP SSID that would be used
 * @param buffer Buffer to store SSID
 * @param len Buffer length
 */
void wifi_manager_get_ap_ssid(char *buffer, size_t len);

/**
 * Scan for available networks
 * @param ap_records Array to store results
 * @param max_records Maximum number of records
 * @return Number of networks found
 */
uint16_t wifi_manager_scan(void *ap_records, uint16_t max_records);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
