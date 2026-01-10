/**
 * OmniaPi Gateway - MQTT Handler
 * Handles MQTT communication with Backend
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============== MQTT Configuration ==============
#define MQTT_BROKER_URI         "mqtt://192.168.1.252:1883"
#define MQTT_CLIENT_ID          "omniapi-gateway"

// Topics
#define MQTT_TOPIC_STATUS       "omniapi/gateway/status"
#define MQTT_TOPIC_NODES        "omniapi/gateway/nodes"
#define MQTT_TOPIC_COMMAND      "omniapi/gateway/command"
#define MQTT_TOPIC_NODE_PREFIX  "omniapi/gateway/node/"
#define MQTT_TOPIC_LWT          "omniapi/gateway/lwt"

/**
 * Initialize MQTT client
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_init(void);

/**
 * Start MQTT client (connect to broker)
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_start(void);

/**
 * Check if MQTT is connected
 * @return true if connected
 */
bool mqtt_handler_is_connected(void);

/**
 * Publish gateway status
 * Publishes to: omniapi/gateway/status
 */
void mqtt_handler_publish_status(void);

/**
 * Publish all nodes status
 * Publishes to: omniapi/gateway/nodes
 */
void mqtt_handler_publish_all_nodes(void);

/**
 * Publish single node state change
 * Publishes to: omniapi/gateway/node/{MAC}/state
 * @param node_index Index of the node
 */
void mqtt_handler_publish_node_state(int node_index);

#ifdef __cplusplus
}
#endif

#endif // MQTT_HANDLER_H
