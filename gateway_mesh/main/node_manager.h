/**
 * OmniaPi Gateway Mesh - Node Manager
 */

#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include "esp_err.h"
#include "omniapi_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NODES 50

typedef struct {
    uint8_t mac[6];
    uint8_t device_type;
    uint8_t status;
    uint8_t mesh_layer;
    int8_t  rssi;
    char    firmware_version[16];
    uint32_t last_seen;
    bool    commissioned;
} node_info_t;

esp_err_t node_manager_init(void);
esp_err_t node_manager_add_node(const uint8_t *mac);
esp_err_t node_manager_remove_node(const uint8_t *mac);
esp_err_t node_manager_set_offline(const uint8_t *mac);
void node_manager_check_timeouts(void);

int node_manager_get_count(void);
node_info_t* node_manager_get_node(const uint8_t *mac);
node_info_t* node_manager_get_all(int *count);

esp_err_t node_manager_update_info(const uint8_t *mac, const payload_heartbeat_ack_t *info);

#ifdef __cplusplus
}
#endif

#endif // NODE_MANAGER_H
