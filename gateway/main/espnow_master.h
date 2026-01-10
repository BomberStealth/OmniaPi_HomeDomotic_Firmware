/**
 * OmniaPi Gateway - ESP-NOW Master
 * Handles ESP-NOW communication with nodes
 */

#ifndef ESPNOW_MASTER_H
#define ESPNOW_MASTER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============== Message Protocol ==============
#define MSG_HEARTBEAT       0x01
#define MSG_HEARTBEAT_ACK   0x02
#define MSG_OTA_BEGIN       0x10
#define MSG_OTA_READY       0x11
#define MSG_OTA_DATA        0x12
#define MSG_OTA_ACK         0x13
#define MSG_OTA_END         0x14
#define MSG_OTA_DONE        0x15
#define MSG_OTA_ERROR       0x1F

// Relay control messages
#define MSG_COMMAND         0x20
#define MSG_COMMAND_ACK     0x21
#define MSG_STATE           0x22

// Command actions
#define CMD_OFF             0x00
#define CMD_ON              0x01
#define CMD_TOGGLE          0x02

// Discovery messages (for channel scan)
#define MSG_DISCOVERY       0x30
#define MSG_DISCOVERY_ACK   0x31

/**
 * Callback type for node state changes
 * @param node_index Index of the node that changed
 * @param channel Relay channel
 * @param state New state
 */
typedef void (*espnow_state_change_cb_t)(int node_index, uint8_t channel, uint8_t state);

/**
 * Initialize ESP-NOW master
 * @return ESP_OK on success
 */
esp_err_t espnow_master_init(void);

/**
 * Start ESP-NOW (after WiFi is connected)
 * @return ESP_OK on success
 */
esp_err_t espnow_master_start(void);

/**
 * Send heartbeat broadcast to discover nodes
 */
void espnow_master_send_heartbeat(void);

/**
 * Send command to a specific node
 * @param mac Target MAC address
 * @param channel Relay channel (1 or 2)
 * @param action Command action (CMD_ON, CMD_OFF, CMD_TOGGLE)
 * @return ESP_OK on success
 */
esp_err_t espnow_master_send_command(const uint8_t *mac, uint8_t channel, uint8_t action);

/**
 * Register callback for state changes
 * @param callback Callback function
 */
void espnow_master_register_state_cb(espnow_state_change_cb_t callback);

/**
 * Get number of messages received
 * @return Message count
 */
uint32_t espnow_master_get_rx_count(void);

/**
 * Get number of messages sent
 * @return Message count
 */
uint32_t espnow_master_get_tx_count(void);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_MASTER_H
