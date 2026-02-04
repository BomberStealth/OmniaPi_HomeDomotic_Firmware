/**
 * OmniaPi Gateway Mesh - Commissioning Handler
 *
 * Handles node discovery, commissioning, and decommissioning over ESP-WIFI-MESH
 */

#ifndef COMMISSIONING_H
#define COMMISSIONING_H

#include "esp_err.h"
#include "omniapi_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Commissioning Mode
// ============================================================================

typedef enum {
    COMMISSION_MODE_PRODUCTION,   // Normal operation on production mesh (OMNIAP)
    COMMISSION_MODE_DISCOVERY,    // Scanning/commissioning on discovery mesh (OMNIDS)
} commission_mode_t;

/**
 * Initialize commissioning handler
 * @return ESP_OK on success
 */
esp_err_t commissioning_init(void);

/**
 * Get current commissioning mode
 * @return Current mode
 */
commission_mode_t commissioning_get_mode(void);

// ============================================================================
// Credentials Management
// ============================================================================

/**
 * Set production network credentials for commissioning
 * @param network_id  6-byte mesh network ID
 * @param network_key Mesh password (max 32 chars)
 * @param plant_id    Plant identifier (max 32 chars)
 * @return ESP_OK on success
 */
esp_err_t commissioning_set_credentials(const uint8_t *network_id,
                                         const char *network_key,
                                         const char *plant_id);

/**
 * Get production network credentials
 * @param network_id Buffer for 6-byte mesh network ID (can be NULL)
 * @param network_key Buffer for mesh password (can be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not set
 */
esp_err_t commissioning_get_credentials(uint8_t *network_id, char *network_key);

// ============================================================================
// Scanning
// ============================================================================

/**
 * Start scanning for nodes
 * Sends broadcast MSG_SCAN_REQUEST
 * @return ESP_OK on success
 */
esp_err_t commissioning_start_scan(void);

/**
 * Stop scanning for nodes
 * @return ESP_OK on success
 */
esp_err_t commissioning_stop_scan(void);

/**
 * Check if currently scanning
 * @return true if scanning
 */
bool commissioning_is_scanning(void);

/**
 * Handle scan response from node
 * @param src_mac Source MAC address
 * @param msg     The scan response message
 */
void commissioning_handle_scan_response(const uint8_t *src_mac, const omniapi_message_t *msg);

/**
 * Get scan results
 * @param results     Buffer to store results
 * @param max_results Maximum number of results to return
 * @return Number of results returned
 */
int commissioning_get_scan_results(scan_result_t *results, int max_results);

/**
 * Add discovered node from NODE_ANNOUNCE (fallback when SCAN_RESPONSE not received)
 * Use this when a node connects and sends announce with commissioned=0
 * @param mac              Node MAC address (6 bytes)
 * @param device_type      Device type from announce
 * @param firmware_version Firmware version packed as uint32_t
 * @param commissioned     Whether node is already commissioned
 */
void commissioning_add_discovered_node(const uint8_t *mac, uint8_t device_type,
                                        uint32_t firmware_version, bool commissioned);

// ============================================================================
// Commissioning/Decommissioning
// ============================================================================

/**
 * Commission a node (add to network)
 * @param mac       Node MAC address (6 bytes)
 * @param node_name Optional friendly name (can be NULL for auto-generated)
 * @return ESP_OK on success
 */
esp_err_t commissioning_add_node(const uint8_t *mac, const char *node_name);

/**
 * Decommission a node (remove from network)
 * @param mac Node MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t commissioning_remove_node(const uint8_t *mac);

/**
 * Handle commission ACK from node
 * @param src_mac Source MAC address
 * @param msg     The commission ACK message
 */
void commissioning_handle_commission_ack(const uint8_t *src_mac, const omniapi_message_t *msg);

/**
 * Handle decommission ACK from node
 * @param src_mac Source MAC address
 * @param msg     The decommission ACK message
 */
void commissioning_handle_decommission_ack(const uint8_t *src_mac, const omniapi_message_t *msg);

// ============================================================================
// Utility
// ============================================================================

/**
 * Identify a node (blink LED for identification)
 * @param mac Node MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t commissioning_identify_node(const uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif // COMMISSIONING_H
