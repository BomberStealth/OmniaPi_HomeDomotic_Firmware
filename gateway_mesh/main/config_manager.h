/**
 * OmniaPi Gateway - Configuration Manager
 *
 * Manages gateway configuration with NVS persistence.
 * Falls back to Kconfig defaults if NVS is empty.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Structures
// ============================================================================

/**
 * WiFi Station configuration (connect to home router)
 */
typedef struct {
    char ssid[33];          // Max 32 chars + null
    char password[65];      // Max 64 chars + null
    bool configured;        // true if set via provisioning
} config_wifi_sta_t;

/**
 * WiFi AP configuration (for provisioning mode)
 */
typedef struct {
    char ssid[33];          // Auto-generated: OmniaPi_Gateway_XXXX
    char password[65];      // Default: "omniapi123"
    uint8_t channel;
} config_wifi_ap_t;

/**
 * MQTT configuration
 */
typedef struct {
    char broker_uri[128];   // e.g., "mqtt://192.168.1.100:1883"
    char username[33];
    char password[65];
    char client_id[33];     // Auto-generated from MAC if empty
    bool configured;
} config_mqtt_t;

/**
 * Mesh network configuration
 */
typedef struct {
    char ap_password[65];   // Password for mesh AP (nodes connect with this)
    uint8_t channel;        // WiFi channel (1-14)
    uint8_t max_layer;      // Max mesh layers
    uint8_t max_connections;// Max child connections per node
} config_mesh_t;

/**
 * Gateway provisioning state
 */
typedef enum {
    PROVISION_STATE_UNCONFIGURED = 0,   // No config, needs setup
    PROVISION_STATE_WIFI_ONLY,          // WiFi configured, MQTT not
    PROVISION_STATE_CONFIGURED,         // Fully configured
} provision_state_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize configuration manager
 * Loads config from NVS, falls back to Kconfig defaults
 * @return ESP_OK on success
 */
esp_err_t config_manager_init(void);

// ============================================================================
// Getters - Always return valid config (NVS or defaults)
// ============================================================================

/**
 * Get WiFi STA configuration
 */
const config_wifi_sta_t* config_get_wifi_sta(void);

/**
 * Get WiFi AP configuration (for provisioning)
 */
const config_wifi_ap_t* config_get_wifi_ap(void);

/**
 * Get MQTT configuration
 */
const config_mqtt_t* config_get_mqtt(void);

/**
 * Get Mesh configuration
 */
const config_mesh_t* config_get_mesh(void);

/**
 * Get current provisioning state
 */
provision_state_t config_get_provision_state(void);

/**
 * Check if gateway is fully configured
 */
bool config_is_configured(void);

// ============================================================================
// Setters - Save to NVS and update runtime config
// ============================================================================

/**
 * Set WiFi STA credentials
 * @param ssid     WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t config_set_wifi_sta(const char *ssid, const char *password);

/**
 * Set MQTT configuration
 * @param broker_uri MQTT broker URI (e.g., "mqtt://host:port")
 * @param username   MQTT username (can be NULL)
 * @param password   MQTT password (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t config_set_mqtt(const char *broker_uri, const char *username, const char *password);

/**
 * Set Mesh configuration
 * @param ap_password Mesh AP password
 * @param channel     WiFi channel (1-14, 0 = auto)
 * @return ESP_OK on success
 */
esp_err_t config_set_mesh(const char *ap_password, uint8_t channel);

// ============================================================================
// Factory Reset
// ============================================================================

/**
 * Clear all configuration from NVS
 * Gateway will revert to Kconfig defaults on next boot
 * @return ESP_OK on success
 */
esp_err_t config_factory_reset(void);

// ============================================================================
// Utility
// ============================================================================

/**
 * Get gateway unique ID (based on MAC address)
 * Format: "XXXXXXXXXXXX" (12 hex chars, no colons)
 */
const char* config_get_gateway_id(void);

/**
 * Get gateway hostname for mDNS
 * Format: "omniapi-XXXX" (last 4 hex of MAC)
 */
const char* config_get_hostname(void);

/**
 * Print current configuration to log (passwords masked)
 */
void config_print_current(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
