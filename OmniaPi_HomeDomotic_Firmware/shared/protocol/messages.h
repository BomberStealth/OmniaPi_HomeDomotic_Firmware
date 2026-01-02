/**
 * OmniaPi Protocol - Message Definitions
 *
 * Defines the ESP-NOW message format for communication
 * between Gateway and Nodes.
 *
 * @version 1.0.0
 * @date 2025-12-29
 */

#ifndef OMNIAPI_MESSAGES_H
#define OMNIAPI_MESSAGES_H

#include <stdint.h>

// Protocol version
#define OMNIAPI_PROTOCOL_VERSION 1

// Maximum payload size (ESP-NOW max is 250 bytes)
#define OMNIAPI_MAX_PAYLOAD 240

// Message types
typedef enum {
    // Discovery & Registration (0x01-0x0F)
    MSG_DISCOVERY           = 0x01,  // Gateway -> Broadcast: "Who's there?"
    MSG_DISCOVERY_RESPONSE  = 0x02,  // Node -> Gateway: "I'm here, this is my info"
    MSG_REGISTER            = 0x03,  // Gateway -> Node: "You are now node X"
    MSG_REGISTER_ACK        = 0x04,  // Node -> Gateway: "Registration confirmed"

    // Commands & State (0x10-0x1F)
    MSG_COMMAND             = 0x10,  // Gateway -> Node: Execute command
    MSG_COMMAND_ACK         = 0x11,  // Node -> Gateway: Command received
    MSG_STATE               = 0x12,  // Node -> Gateway: Current state
    MSG_STATE_REQUEST       = 0x13,  // Gateway -> Node: Request state

    // Heartbeat (0x20-0x2F)
    MSG_PING                = 0x20,  // Gateway -> Node: Are you alive?
    MSG_PONG                = 0x21,  // Node -> Gateway: Yes, I'm alive

    // OTA Updates (0x30-0x3F)
    MSG_OTA_START           = 0x30,  // Gateway -> Node: Starting OTA
    MSG_OTA_DATA            = 0x31,  // Gateway -> Node: Firmware chunk
    MSG_OTA_END             = 0x32,  // Gateway -> Node: OTA complete
    MSG_OTA_ACK             = 0x33,  // Node -> Gateway: Chunk received
    MSG_OTA_ERROR           = 0x34,  // Node -> Gateway: OTA error

    // Configuration (0x40-0x4F)
    MSG_CONFIG_SET          = 0x40,  // Gateway -> Node: Set configuration
    MSG_CONFIG_GET          = 0x41,  // Gateway -> Node: Get configuration
    MSG_CONFIG_RESPONSE     = 0x42,  // Node -> Gateway: Configuration data
} OmniaPiMessageType;

// Command actions
typedef enum {
    ACTION_OFF    = 0,
    ACTION_ON     = 1,
    ACTION_TOGGLE = 2,
} OmniaPiAction;

// Device types
typedef enum {
    DEVICE_RELAY    = 0x01,  // Simple relay (on/off)
    DEVICE_DIMMER   = 0x02,  // Dimmable light
    DEVICE_SHUTTER  = 0x03,  // Shutter/blind (up/down/stop)
    DEVICE_SENSOR   = 0x04,  // Generic sensor
    DEVICE_THERMO   = 0x05,  // Thermostat
} OmniaPiDeviceType;

// Error flags
typedef enum {
    ERROR_NONE          = 0x00,
    ERROR_RELAY_STUCK   = 0x01,  // Relay not responding
    ERROR_OVERTEMP      = 0x02,  // Overtemperature detected
    ERROR_COMM_FAIL     = 0x04,  // Communication failure
    ERROR_LOW_RSSI      = 0x08,  // Low signal strength
} OmniaPiErrorFlags;

// ============================================
// MESSAGE STRUCTURES
// ============================================

/**
 * Base message header (common to all messages)
 * Total: 5 bytes
 */
typedef struct __attribute__((packed)) {
    uint8_t version;      // Protocol version
    uint8_t type;         // Message type (OmniaPiMessageType)
    uint8_t nodeId;       // Node ID (0 = gateway, 1-254 = nodes, 255 = broadcast)
    uint8_t sequence;     // Sequence number for ACK tracking
    uint8_t payloadLen;   // Length of payload
} OmniaPiHeader;

/**
 * Complete message structure
 */
typedef struct __attribute__((packed)) {
    OmniaPiHeader header;
    uint8_t payload[OMNIAPI_MAX_PAYLOAD];
    uint8_t checksum;     // CRC8 of header + payload
} OmniaPiMessage;

// ============================================
// PAYLOAD STRUCTURES
// ============================================

/**
 * Discovery response payload
 * Sent by node in response to MSG_DISCOVERY
 */
typedef struct __attribute__((packed)) {
    uint8_t deviceType;       // OmniaPiDeviceType
    uint8_t channelCount;     // Number of channels (relays/outputs)
    uint8_t firmwareVersion[3]; // Major.Minor.Patch
    uint8_t macAddress[6];    // Node MAC address
    char deviceName[16];      // Human-readable name (null-terminated)
} DiscoveryPayload;

/**
 * Registration payload
 * Sent by gateway to assign node ID
 */
typedef struct __attribute__((packed)) {
    uint8_t assignedNodeId;   // Assigned node ID
    uint8_t encryptionKey[16]; // AES-128 key for secure communication
} RegisterPayload;

/**
 * Command payload
 * Sent by gateway to control a node
 */
typedef struct __attribute__((packed)) {
    uint8_t channel;          // Channel number (1-8)
    uint8_t action;           // OmniaPiAction
    uint8_t value;            // For dimmer: 0-255, for relay: ignored
    uint8_t transitionTime;   // Transition time in 100ms units (for dimmer)
} CommandPayload;

/**
 * State payload
 * Sent by node to report current state
 */
typedef struct __attribute__((packed)) {
    uint8_t channelCount;     // Number of channels
    uint8_t states[8];        // State of each channel (0=OFF, 1=ON)
    uint8_t values[8];        // Value of each channel (for dimmers)
    int8_t rssi;              // Signal strength (dBm)
    uint8_t errorFlags;       // OmniaPiErrorFlags
    uint32_t uptime;          // Uptime in seconds
} StatePayload;

/**
 * Ping/Pong payload
 * For heartbeat messages
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;       // Sender timestamp (for latency measurement)
} PingPayload;

/**
 * OTA data payload
 * For firmware updates
 */
typedef struct __attribute__((packed)) {
    uint32_t offset;          // Byte offset in firmware
    uint16_t length;          // Length of this chunk
    uint8_t data[200];        // Firmware data (max 200 bytes per message)
} OtaDataPayload;

// ============================================
// UTILITY FUNCTIONS
// ============================================

/**
 * Calculate CRC8 checksum
 */
static inline uint8_t omniapi_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
        }
    }
    return crc;
}

/**
 * Validate message checksum
 */
static inline bool omniapi_validate(const OmniaPiMessage *msg) {
    size_t len = sizeof(OmniaPiHeader) + msg->header.payloadLen;
    uint8_t expected = omniapi_crc8((const uint8_t*)msg, len);
    return msg->checksum == expected;
}

/**
 * Set message checksum
 */
static inline void omniapi_set_checksum(OmniaPiMessage *msg) {
    size_t len = sizeof(OmniaPiHeader) + msg->header.payloadLen;
    msg->checksum = omniapi_crc8((const uint8_t*)msg, len);
}

#endif // OMNIAPI_MESSAGES_H
