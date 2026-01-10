#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// Message types (compatible with Gateway)
#define MSG_HEARTBEAT       0x01
#define MSG_HEARTBEAT_ACK   0x02
#define MSG_COMMAND         0x20
#define MSG_COMMAND_ACK     0x21
#define MSG_STATE           0x22

// Discovery messages (for channel scan)
#define MSG_DISCOVERY       0x30
#define MSG_DISCOVERY_ACK   0x31

// Command actions
#define CMD_OFF             0x00
#define CMD_ON              0x01
#define CMD_TOGGLE          0x02

// OTA message types
#define MSG_OTA_BEGIN       0x10
#define MSG_OTA_READY       0x11
#define MSG_OTA_DATA        0x12
#define MSG_OTA_ACK         0x13
#define MSG_OTA_END         0x14
#define MSG_OTA_DONE        0x15
#define MSG_OTA_ERROR       0x1F

/**
 * Scan all WiFi channels (1-13) to find Gateway
 * @return Channel where Gateway was found, or 0 if not found
 */
uint8_t espnow_channel_scan(void);

/**
 * Initialize ESP-NOW on specific channel
 * @param wifi_channel Channel to use
 */
void espnow_handler_init(uint8_t wifi_channel);

/**
 * Check if Gateway has been discovered
 */
bool espnow_is_gateway_known(void);

/**
 * Get last heartbeat timestamp
 */
uint32_t espnow_get_last_heartbeat_time(void);

/**
 * Save channel to NVS
 */
void espnow_save_channel(uint8_t channel);

/**
 * Load channel from NVS
 * @return Saved channel, or 0 if not saved
 */
uint8_t espnow_load_channel(void);

#endif
