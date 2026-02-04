/**
 * OmniaPi Gateway Mesh - Mesh Network Handler
 *
 * ESP-WIFI-MESH network management as Fixed Root
 */

#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_mesh.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Mesh Configuration
// ============================================================================
#define MESH_ID_DEFAULT         {0x4F, 0x4D, 0x4E, 0x49, 0x41, 0x50}  // "OMNIAP"
#define MESH_MAX_ROUTING_TABLE  100  // Maximum nodes in routing table

// ============================================================================
// Callbacks (set from main.c)
// ============================================================================

/**
 * Set callback for mesh started event
 */
void mesh_network_set_started_cb(void (*cb)(void));

/**
 * Set callback for root status change
 */
void mesh_network_set_root_cb(void (*cb)(bool is_root));

/**
 * Set callback for child connected
 */
void mesh_network_set_child_connected_cb(void (*cb)(const uint8_t *mac));

/**
 * Set callback for child disconnected
 */
void mesh_network_set_child_disconnected_cb(void (*cb)(const uint8_t *mac));

/**
 * Set callback for message received from node
 */
typedef void (*mesh_rx_cb_t)(const uint8_t *src_mac, const uint8_t *data, size_t len);
void mesh_network_set_rx_cb(mesh_rx_cb_t cb);

/**
 * Set callback for router (external network) connection state change
 * @param cb Callback: true = connected to router, false = disconnected
 */
void mesh_network_set_router_cb(void (*cb)(bool connected));

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize mesh network
 * - Configures WiFi
 * - Sets mesh parameters
 * - Registers event handlers
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_network_init(void);

/**
 * Start mesh network as Fixed Root
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_network_start(void);

/**
 * Stop mesh network
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_network_stop(void);

/**
 * Start mesh with specific mesh ID and password
 * Used for switching between production and discovery mesh
 *
 * @param mesh_id   6-byte mesh network ID
 * @param password  Mesh AP password
 * @return ESP_OK on success
 */
esp_err_t mesh_network_start_with_id(const uint8_t mesh_id[6], const char *password);

// ============================================================================
// Messaging
// ============================================================================

/**
 * Send message to a specific node
 *
 * @param dest_mac Destination MAC address (6 bytes)
 * @param data     Message data
 * @param len      Data length
 * @return ESP_OK on success
 */
esp_err_t mesh_network_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);

/**
 * Broadcast message to all nodes in routing table
 *
 * @param data Message data
 * @param len  Data length
 * @return ESP_OK on success (may fail for some nodes)
 */
esp_err_t mesh_network_broadcast(const uint8_t *data, size_t len);

/**
 * Send heartbeat to all nodes
 */
void mesh_network_broadcast_heartbeat(void);

/**
 * Process received messages (call from main loop)
 */
void mesh_network_process_rx(void);

// ============================================================================
// Status & Info
// ============================================================================

/**
 * Check if mesh is initialized and started
 */
bool mesh_network_is_started(void);

/**
 * Check if a node is in the routing table (reachable)
 */
bool mesh_network_is_node_reachable(const uint8_t *mac);

/**
 * Check if this device is the mesh root
 */
bool mesh_network_is_root(void);

/**
 * Get current mesh layer (root = 1)
 */
int mesh_network_get_layer(void);

/**
 * Get number of nodes in routing table
 */
int mesh_network_get_node_count(void);

/**
 * Get routing table (list of all connected node MACs)
 *
 * @param table     Buffer to store MAC addresses
 * @param max_nodes Maximum number of nodes to retrieve
 * @return Number of nodes retrieved
 */
int mesh_network_get_routing_table(mesh_addr_t *table, int max_nodes);

/**
 * Get mesh network ID
 */
void mesh_network_get_id(uint8_t *mesh_id);

/**
 * Get mesh statistics
 */
typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t routing_table_size;
    int8_t   parent_rssi;
} mesh_stats_t;

void mesh_network_get_stats(mesh_stats_t *stats);

/**
 * Get mesh STA network interface (for route priority management)
 */
esp_netif_t *mesh_network_get_sta_netif(void);

#ifdef __cplusplus
}
#endif

#endif // MESH_NETWORK_H
