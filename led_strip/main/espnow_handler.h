#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================
// MESSAGE TYPES (compatible with Gateway)
// ============================================

// Heartbeat messages
#define MSG_HEARTBEAT       0x01
#define MSG_HEARTBEAT_ACK   0x02

// Standard command messages (relay nodes)
#define MSG_COMMAND         0x20
#define MSG_COMMAND_ACK     0x21
#define MSG_STATE           0x22

// Discovery messages (for channel scan)
#define MSG_DISCOVERY       0x30
#define MSG_DISCOVERY_ACK   0x31

// OTA message types
#define MSG_OTA_BEGIN       0x10
#define MSG_OTA_READY       0x11
#define MSG_OTA_DATA        0x12
#define MSG_OTA_ACK         0x13
#define MSG_OTA_END         0x14
#define MSG_OTA_DONE        0x15
#define MSG_OTA_ERROR       0x1F

// ============================================
// LED STRIP COMMAND TYPES (0x40-0x4F range)
// ============================================

#define MSG_LED_COMMAND     0x40    // LED strip command
#define MSG_LED_ACK         0x41    // LED strip ACK response

// LED Strip command actions (sent as payload[0] after MSG_LED_COMMAND)
// MUST match Gateway espnow_master.h LED_ACTION_* values!
#define LED_CMD_OFF         0x00    // Turn off
#define LED_CMD_ON          0x01    // Turn on (restore last state)
#define LED_CMD_SET_COLOR   0x02    // Set RGB color: [R, G, B]
#define LED_CMD_SET_BRIGHT  0x03    // Set brightness: [0-255]
#define LED_CMD_SET_EFFECT  0x04    // Set effect: [effect_id]
#define LED_CMD_SET_SPEED   0x05    // Set effect speed: [0-255]
#define LED_CMD_SET_NUM_LEDS 0x06   // Set number of LEDs: [low_byte, high_byte]
#define LED_CMD_CUSTOM_EFFECT 0x07  // Custom 3-color rainbow: [r1,g1,b1,r2,g2,b2,r3,g3,b3]

// Effect IDs
#define EFFECT_STATIC       0x00    // Solid color
#define EFFECT_RAINBOW      0x01    // Rainbow cycle
#define EFFECT_BREATHING    0x02    // Breathing/pulse
#define EFFECT_CHASE        0x03    // Chase/running light
#define EFFECT_SPARKLE      0x04    // Random sparkle
#define EFFECT_FIRE         0x05    // Fire simulation
#define EFFECT_CUSTOM       0x06    // Custom 3-color rainbow

// Device type identifier
#define DEVICE_TYPE_LED_STRIP  0x10

// ============================================
// FUNCTION PROTOTYPES
// ============================================

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
 * Get last heartbeat timestamp (ms)
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

/**
 * Send current LED state as ACK to Gateway
 */
void espnow_send_led_state(void);

#endif // ESPNOW_HANDLER_H
