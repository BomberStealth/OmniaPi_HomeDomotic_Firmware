/**
 * OmniaPi Node Mesh - Mesh Node Handler
 *
 * ESP-WIFI-MESH node implementation (non-root)
 * Supports discovery mesh (uncommissioned) and production mesh (commissioned)
 */

#ifndef MESH_NODE_H
#define MESH_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Callbacks
// ============================================================================

/**
 * Set callback for mesh connected event (connected to parent/root)
 */
void mesh_node_set_connected_cb(void (*cb)(void));

/**
 * Set callback for mesh disconnected event
 */
void mesh_node_set_disconnected_cb(void (*cb)(void));

/**
 * Set callback for message received from root/gateway
 */
typedef void (*mesh_node_rx_cb_t)(const uint8_t *src_mac, const uint8_t *data, size_t len);
void mesh_node_set_rx_cb(mesh_node_rx_cb_t cb);

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize mesh node
 * - Configures WiFi
 * - Sets mesh parameters
 * - Registers event handlers
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_node_init(void);

/**
 * Start mesh node (auto-join to network)
 * Uses discovery mesh if not commissioned, production mesh if commissioned
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_node_start(void);

/**
 * Stop mesh node
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_node_stop(void);

// ============================================================================
// Messaging
// ============================================================================

/**
 * Send message to root (gateway)
 *
 * @param data Message data
 * @param len  Data length
 * @return ESP_OK on success
 */
esp_err_t mesh_node_send_to_root(const uint8_t *data, size_t len);

/**
 * Process received messages (call from main loop)
 */
void mesh_node_process_rx(void);

// ============================================================================
// Status & Info
// ============================================================================

/**
 * Check if mesh is started and connected
 */
bool mesh_node_is_connected(void);

/**
 * Get current mesh layer (root = 1)
 */
int mesh_node_get_layer(void);

/**
 * Get parent RSSI
 */
int8_t mesh_node_get_parent_rssi(void);

/**
 * Get root MAC address
 */
void mesh_node_get_root_mac(uint8_t *mac);

/**
 * Get mesh network ID
 */
void mesh_node_get_mesh_id(uint8_t *mesh_id);

/**
 * Check if connected to production mesh (vs discovery mesh)
 * @return true if connected to production mesh
 */
bool mesh_node_is_production_mesh(void);

#ifdef __cplusplus
}
#endif

#endif // MESH_NODE_H
