/**
 * OmniaPi Node Mesh - Commissioning Handler
 *
 * Handles node commissioning, network credentials storage,
 * and discovery/production mesh switching.
 */

#ifndef COMMISSIONING_H
#define COMMISSIONING_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "omniapi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize commissioning handler
 * Loads saved credentials from NVS
 * @return ESP_OK on success
 */
esp_err_t commissioning_init(void);

// ============================================================================
// Status
// ============================================================================

/**
 * Check if node is commissioned
 * @return true if commissioned
 */
bool commissioning_is_commissioned(void);

/**
 * Get network credentials (mesh ID and password)
 * @param network_id Buffer for 6-byte mesh network ID (can be NULL)
 * @param network_key Buffer for mesh password, min 33 bytes (can be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not commissioned
 */
esp_err_t commissioning_get_network_credentials(uint8_t *network_id, char *network_key);

/**
 * Get the plant ID this node is commissioned to
 * @param plant_id Buffer to store plant ID (min 33 bytes)
 * @return ESP_OK on success
 */
esp_err_t commissioning_get_plant_id(char *plant_id);

/**
 * Get the node name
 * @param name Buffer to store name (min 33 bytes)
 * @return ESP_OK on success
 */
esp_err_t commissioning_get_node_name(char *name);

// ============================================================================
// Message Handlers
// ============================================================================

/**
 * Handle scan request from gateway
 * Responds with MSG_SCAN_RESPONSE
 * @param msg The scan request message
 */
void commissioning_handle_scan_request(const omniapi_message_t *msg);

/**
 * Handle commission command from gateway
 * Saves credentials to NVS and restarts
 * @param msg The commission message
 */
void commissioning_handle_commission(const omniapi_message_t *msg);

/**
 * Handle decommission command from gateway
 * Sends ACK and performs factory reset
 * @param msg The decommission message
 */
void commissioning_handle_decommission(const omniapi_message_t *msg);

// ============================================================================
// Reset
// ============================================================================

/**
 * Perform factory reset (clear all commissioning data)
 * Device will restart after reset
 */
void commissioning_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif // COMMISSIONING_H
