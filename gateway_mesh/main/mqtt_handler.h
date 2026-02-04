/**
 * OmniaPi Gateway Mesh - MQTT Handler
 *
 * Handles MQTT communication with backend for commissioning,
 * device control, and status reporting.
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "esp_err.h"
#include "omniapi_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize MQTT handler
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_init(void);

/**
 * Start MQTT client
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_start(void);

/**
 * Stop MQTT client
 * @return ESP_OK on success
 */
esp_err_t mqtt_handler_stop(void);

/**
 * Process MQTT events (call periodically)
 */
void mqtt_handler_process(void);

/**
 * Check if MQTT is connected
 * @return true if connected
 */
bool mqtt_handler_is_connected(void);

// ============================================================================
// Generic Publishing
// ============================================================================

/**
 * Publish a message to any topic
 * @param topic   MQTT topic
 * @param payload Message payload
 * @param qos     QoS level (0, 1, or 2)
 * @param retain  Retain flag
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish(const char *topic, const char *payload, int qos, bool retain);

// ============================================================================
// Status Publishing
// ============================================================================

/**
 * Publish gateway online/offline status
 * @param online true if online
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_gateway_status(bool online);

/**
 * Publish node connected event
 * @param mac Node MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_node_connected(const uint8_t *mac);

/**
 * Publish node disconnected event
 * @param mac Node MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_node_disconnected(const uint8_t *mac);

/**
 * Publish node state (device-specific JSON)
 * @param mac        Node MAC address (6 bytes)
 * @param state_json JSON string with device state
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_node_state(const uint8_t *mac, const char *state_json);

// ============================================================================
// Commissioning Publishing
// ============================================================================

/**
 * Publish scan results to backend
 * @param results Array of scan results
 * @param count   Number of results
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_scan_results(const scan_result_t *results, int count);

/**
 * Publish commissioning result
 * @param mac     Node MAC address (6 bytes)
 * @param success true if successful
 * @param message Optional message (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_commission_result(const uint8_t *mac, bool success, const char *message);

/**
 * Publish decommissioning result
 * @param mac     Node MAC address (6 bytes)
 * @param success true if successful
 * @param message Optional message (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_decommission_result(const uint8_t *mac, bool success, const char *message);

// ============================================================================
// OTA Publishing
// ============================================================================

/**
 * Publish OTA progress update
 * @param completed Number of nodes completed
 * @param failed    Number of nodes failed
 * @param total     Total number of participating nodes
 * @param status    Status message
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_ota_progress(uint8_t completed, uint8_t failed, uint8_t total, const char *status);

/**
 * Publish OTA job completion
 * @param completed Number of nodes completed
 * @param failed    Number of nodes failed
 * @param version   Firmware version that was distributed
 * @return ESP_OK on success
 */
esp_err_t mqtt_publish_ota_complete(uint8_t completed, uint8_t failed, const char *version);

#ifdef __cplusplus
}
#endif

#endif // MQTT_HANDLER_H
