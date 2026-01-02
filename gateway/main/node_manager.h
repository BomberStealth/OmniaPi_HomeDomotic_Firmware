/**
 * OmniaPi Gateway - Node Manager
 * Tracks connected ESP-NOW nodes and their states
 */

#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NODES 20
#define MAC_ADDR_LEN 6

/**
 * Node information structure
 */
typedef struct {
    uint8_t mac[MAC_ADDR_LEN];
    int8_t rssi;
    uint32_t last_seen;
    uint32_t messages_received;
    bool online;
    char version[16];
    uint8_t relay_states[2];  // State of relay 1 and 2 (0=OFF, 1=ON)
    uint8_t relay_count;      // Number of relays (default 2)
} node_info_t;

/**
 * Initialize node manager
 * @return ESP_OK on success
 */
esp_err_t node_manager_init(void);

/**
 * Find or add a node by MAC address
 * @param mac MAC address
 * @param rssi Signal strength
 * @return Node index, or -1 if full
 */
int node_manager_find_or_add(const uint8_t *mac, int8_t rssi);

/**
 * Get node by index
 * @param index Node index
 * @return Pointer to node info, or NULL
 */
node_info_t *node_manager_get_node(int index);

/**
 * Get node by MAC address
 * @param mac MAC address
 * @return Pointer to node info, or NULL
 */
node_info_t *node_manager_get_by_mac(const uint8_t *mac);

/**
 * Update node relay state
 * @param index Node index
 * @param channel Relay channel (1 or 2)
 * @param state Relay state (0=OFF, 1=ON)
 */
void node_manager_update_relay(int index, uint8_t channel, uint8_t state);

/**
 * Update node version
 * @param index Node index
 * @param version Version string
 */
void node_manager_update_version(int index, const char *version);

/**
 * Get number of tracked nodes
 * @return Node count
 */
int node_manager_get_count(void);

/**
 * Check and update online status of all nodes
 * Marks nodes as offline if not seen for 10 seconds
 */
void node_manager_check_online_status(void);

/**
 * Convert MAC address to string
 * @param mac MAC address
 * @param buffer Output buffer (at least 18 bytes)
 */
void node_manager_mac_to_string(const uint8_t *mac, char *buffer);

/**
 * Parse MAC address from string
 * @param str MAC string (XX:XX:XX:XX:XX:XX)
 * @param mac Output MAC address
 * @return true on success
 */
bool node_manager_mac_from_string(const char *str, uint8_t *mac);

/**
 * Generate JSON array of all nodes
 * @param buffer Output buffer
 * @param len Buffer length
 * @return Number of bytes written
 */
int node_manager_get_nodes_json(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // NODE_MANAGER_H
