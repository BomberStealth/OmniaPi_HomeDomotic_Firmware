/**
 * OmniaPi Gateway - Ethernet Manager
 * LAN8720 Ethernet support for WT32-ETH01
 *
 * Hardware: WT32-ETH01 with integrated LAN8720
 * Pinout:
 *   ETH_PHY_MDC   = GPIO23
 *   ETH_PHY_MDIO  = GPIO18
 *   ETH_PHY_POWER = GPIO16
 *   ETH_CLK_MODE  = GPIO0 (50MHz from LAN8720)
 */

#ifndef ETH_MANAGER_H
#define ETH_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Network mode enum
typedef enum {
    NETWORK_MODE_NONE = 0,
    NETWORK_MODE_ETH,
    NETWORK_MODE_WIFI,
    NETWORK_MODE_AP
} network_mode_t;

// Event callback for connection status changes
typedef void (*eth_event_callback_t)(bool connected);

/**
 * Initialize Ethernet LAN8720 for WT32-ETH01
 * Must be called after NVS and netif initialization
 *
 * @return ESP_OK on success
 */
esp_err_t eth_manager_init(void);

/**
 * Start Ethernet connection
 * Non-blocking, connection status via callback or polling
 *
 * @return ESP_OK on success
 */
esp_err_t eth_manager_start(void);

/**
 * Stop Ethernet
 *
 * @return ESP_OK on success
 */
esp_err_t eth_manager_stop(void);

/**
 * Check if Ethernet is connected (link up + IP acquired)
 *
 * @return true if connected with valid IP
 */
bool eth_manager_is_connected(void);

/**
 * Check if Ethernet link is up (cable connected)
 *
 * @return true if link is up
 */
bool eth_manager_is_link_up(void);

/**
 * Get Ethernet IP address as string
 *
 * @param buffer Output buffer for IP string
 * @param len Buffer length (minimum 16 bytes)
 * @return ESP_OK on success
 */
esp_err_t eth_manager_get_ip(char *buffer, size_t len);

/**
 * Get Ethernet MAC address as string
 *
 * @param buffer Output buffer for MAC string (format: XX:XX:XX:XX:XX:XX)
 * @param len Buffer length (minimum 18 bytes)
 * @return ESP_OK on success
 */
esp_err_t eth_manager_get_mac(char *buffer, size_t len);

/**
 * Register callback for Ethernet connection events
 * Callback is called when connection status changes
 *
 * @param callback Function to call on status change
 */
void eth_manager_set_callback(eth_event_callback_t callback);

/**
 * Get current Ethernet RSSI equivalent (always 0 for wired)
 * For API compatibility with WiFi
 *
 * @return Always 0 (wired connection)
 */
int8_t eth_manager_get_rssi(void);

#endif // ETH_MANAGER_H
