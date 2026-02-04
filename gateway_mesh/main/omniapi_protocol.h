/**
 * OmniaPi Protocol Definitions
 *
 * This header defines all message types and structures for communication
 * between Gateway and Nodes over ESP-WIFI-MESH network.
 */

#ifndef OMNIAPI_PROTOCOL_H
#define OMNIAPI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Protocol Version & Constants
// ============================================================================
#define OMNIAPI_PROTOCOL_VERSION    0x02
#define OMNIAPI_MAGIC               0x4F50  // "OP" - OmniaPi

// Mesh Network IDs
#define MESH_ID_PRODUCTION          {0x4F, 0x4D, 0x4E, 0x49, 0x41, 0x50}  // "OMNIAP" - Production mesh
#define MESH_ID_DISCOVERY           {0x4F, 0x4D, 0x4E, 0x49, 0x44, 0x53}  // "OMNIDS" - Discovery mesh
#define MESH_PASSWORD_PRODUCTION    "omniapi_mesh_2024"
#define MESH_PASSWORD_DISCOVERY     "omniapi_discovery"

// ============================================================================
// Message Types (1 byte)
// ============================================================================

// System Messages (0x00 - 0x0F)
#define MSG_HEARTBEAT               0x01    // Gateway -> Nodes (broadcast)
#define MSG_HEARTBEAT_ACK           0x02    // Node -> Gateway
#define MSG_PING                    0x03    // Gateway <-> Node
#define MSG_PONG                    0x04    // Response to ping
#define MSG_REBOOT                  0x05    // Gateway -> Node: reboot command
#define MSG_FACTORY_RESET           0x06    // Gateway -> Node: factory reset
#define MSG_NODE_ANNOUNCE           0x07    // Node -> Gateway: node announcement

// Discovery & Commissioning (0x10 - 0x1F)
#define MSG_SCAN_REQUEST            0x10    // Gateway -> Broadcast: scan for nodes
#define MSG_SCAN_RESPONSE           0x11    // Node -> Gateway: node info
#define MSG_COMMISSION              0x12    // Gateway -> Node: commission with credentials
#define MSG_COMMISSION_ACK          0x13    // Node -> Gateway: commissioning accepted
#define MSG_DECOMMISSION            0x14    // Gateway -> Node: remove from network
#define MSG_DECOMMISSION_ACK        0x15    // Node -> Gateway: decommission confirmed
#define MSG_IDENTIFY                0x16    // Gateway -> Node: blink LED for identification

// Control Commands (0x20 - 0x2F)
#define MSG_RELAY_CMD               0x20    // Gateway -> Node: relay command
#define MSG_RELAY_STATUS            0x21    // Node -> Gateway: relay status
#define MSG_LED_CMD                 0x22    // Gateway -> Node: LED command
#define MSG_LED_STATUS              0x23    // Node -> Gateway: LED status

// Sensor Messages (0x30 - 0x3F)
#define MSG_SENSOR_DATA             0x30    // Node -> Gateway: sensor reading
#define MSG_SENSOR_CONFIG           0x31    // Gateway -> Node: configure sensor

// OTA Messages (0x40 - 0x4F)
#define MSG_OTA_AVAILABLE           0x40    // Gateway -> Nodes: OTA available (broadcast)
#define MSG_OTA_REQUEST             0x41    // Node -> Gateway: request chunk
#define MSG_OTA_DATA                0x42    // Gateway -> Node: OTA chunk data
#define MSG_OTA_COMPLETE            0x43    // Node -> Gateway: OTA success, rebooting
#define MSG_OTA_FAILED              0x44    // Node -> Gateway: OTA failed
#define MSG_OTA_ABORT               0x45    // Gateway -> Node: abort OTA
#define MSG_OTA_BEGIN               0x46    // Gateway -> Node: start targeted OTA (push mode)
#define MSG_OTA_ACK                 0x47    // Node -> Gateway: acknowledge chunk received
#define MSG_OTA_END                 0x48    // Gateway -> Node: all chunks sent, finalize

// Scene/Automation (0x50 - 0x5F)
#define MSG_SCENE_TRIGGER           0x50    // Gateway -> Nodes: execute scene
#define MSG_SCENE_ACK               0x51    // Node -> Gateway: scene executed

// Configuration Messages (0x60 - 0x6F)
#define MSG_CONFIG_SET              0x60    // Gateway -> Node: set configuration
#define MSG_CONFIG_ACK              0x61    // Node -> Gateway: config applied
#define MSG_CONFIG_GET              0x62    // Gateway -> Node: get configuration
#define MSG_CONFIG_RESPONSE         0x63    // Node -> Gateway: config values

// Error Messages (0xF0 - 0xFF)
#define MSG_ERROR                   0xF0    // Generic error
#define MSG_INVALID_CMD             0xF1    // Unknown command

// ============================================================================
// Command Actions
// ============================================================================
#define RELAY_ACTION_OFF            0x00
#define RELAY_ACTION_ON             0x01
#define RELAY_ACTION_TOGGLE         0x02

#define LED_ACTION_OFF              0x00
#define LED_ACTION_ON               0x01
#define LED_ACTION_SET_COLOR        0x02
#define LED_ACTION_SET_BRIGHTNESS   0x03
#define LED_ACTION_EFFECT           0x04

// ============================================================================
// Device Types
// ============================================================================
#define DEVICE_TYPE_UNKNOWN         0x00
#define DEVICE_TYPE_RELAY           0x01
#define DEVICE_TYPE_LED_STRIP       0x02
#define DEVICE_TYPE_DIMMER          0x03
#define DEVICE_TYPE_SENSOR          0x10
#define DEVICE_TYPE_SENSOR_TEMP     0x11
#define DEVICE_TYPE_SENSOR_HUMIDITY 0x12
#define DEVICE_TYPE_SENSOR_MOTION   0x13
#define DEVICE_TYPE_GATEWAY         0xFF

// ============================================================================
// Node Status
// ============================================================================
#define NODE_STATUS_UNKNOWN         0x00
#define NODE_STATUS_DISCOVERED      0x01    // Found but not commissioned
#define NODE_STATUS_ONLINE          0x02    // Commissioned and online
#define NODE_STATUS_OFFLINE         0x03    // Commissioned but offline
#define NODE_STATUS_OTA             0x04    // OTA in progress
#define NODE_STATUS_ERROR           0xFF

// ============================================================================
// LED Effects
// ============================================================================
#define LED_EFFECT_NONE             0x00
#define LED_EFFECT_SOLID            0x01
#define LED_EFFECT_BREATHE          0x02
#define LED_EFFECT_RAINBOW          0x03
#define LED_EFFECT_CHASE            0x04
#define LED_EFFECT_FLASH            0x05

// ============================================================================
// Protocol Structures
// ============================================================================

#define OMNIAPI_MAX_PAYLOAD         200
#define OMNIAPI_HEADER_SIZE         8

/**
 * Message Header (8 bytes)
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;             // OMNIAPI_MAGIC
    uint8_t  version;           // Protocol version
    uint8_t  msg_type;          // Message type
    uint8_t  seq;               // Sequence number
    uint8_t  flags;             // Reserved flags
    uint16_t payload_len;       // Payload length
} omniapi_header_t;

/**
 * Full Message Structure
 */
typedef struct __attribute__((packed)) {
    omniapi_header_t header;
    uint8_t payload[OMNIAPI_MAX_PAYLOAD];
} omniapi_message_t;

// ============================================================================
// Commissioning Structures
// ============================================================================

#define MAX_SCAN_RESULTS            32

/**
 * Scan Result (gateway side storage)
 */
typedef struct {
    uint8_t mac[6];             // Node MAC address
    uint8_t device_type;        // Device type
    char    firmware_version[16]; // Version string
    int8_t  rssi;               // Signal strength
    uint8_t commissioned;       // Already commissioned?
    int64_t last_seen;          // Timestamp
} scan_result_t;

/**
 * Scan Response payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  device_type;       // Device type
    uint32_t firmware_version;  // Version (major<<16 | minor<<8 | patch)
    uint8_t  commissioned;      // 0=not commissioned, 1=commissioned
    int8_t   rssi;              // WiFi RSSI
} payload_scan_response_t;

/**
 * Commission payload (Gateway -> Node)
 * Contains network credentials for production mesh
 */
typedef struct __attribute__((packed)) {
    uint8_t mac[6];             // Target node MAC
    uint8_t network_id[6];      // Production MESH_ID
    char    network_key[32];    // Production mesh password
    char    plant_id[32];       // Plant ID for backend
    char    node_name[32];      // Friendly name
} payload_commission_t;

/**
 * Commission ACK payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t mac[6];             // Node MAC
    uint8_t status;             // 0=success, >0=error
} payload_commission_ack_t;

/**
 * Decommission payload (Gateway -> Node)
 */
typedef struct __attribute__((packed)) {
    uint8_t mac[6];             // Target node MAC
} payload_decommission_t;

/**
 * Decommission ACK payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t mac[6];             // Node MAC
    uint8_t status;             // 0=success, >0=error
} payload_decommission_ack_t;

// ============================================================================
// Device Control Structures
// ============================================================================

/**
 * Node Announce payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  device_type;       // Device type
    uint8_t  capabilities;      // Device-specific capabilities
    uint32_t firmware_version;  // Firmware version
    uint8_t  commissioned;      // 0=not commissioned, 1=commissioned
} payload_node_announce_t;

/**
 * Heartbeat ACK payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  device_type;       // Device type
    uint8_t  status;            // Node status
    uint8_t  mesh_layer;        // Mesh layer/hop count
    int8_t   rssi;              // WiFi RSSI
    uint32_t firmware_version;  // Firmware version (major<<16 | minor<<8 | patch)
    uint32_t uptime;            // Uptime in seconds
} payload_heartbeat_ack_t;

/**
 * Relay Command payload (Gateway -> Node)
 */
typedef struct __attribute__((packed)) {
    uint8_t channel;            // Relay channel (0-based)
    uint8_t action;             // RELAY_ACTION_*
} payload_relay_cmd_t;

/**
 * Relay Status payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t channel;            // Relay channel
    uint8_t state;              // 0=off, 1=on
} payload_relay_status_t;

/**
 * LED Command payload (Gateway -> Node)
 */
typedef struct __attribute__((packed)) {
    uint8_t  action;            // LED_ACTION_*
    uint8_t  r;                 // Red (0-255)
    uint8_t  g;                 // Green (0-255)
    uint8_t  b;                 // Blue (0-255)
    uint8_t  brightness;        // Brightness (0-255)
    uint8_t  effect_id;         // Effect ID
    uint16_t effect_speed;      // Effect speed (ms)
} payload_led_cmd_t;

/**
 * LED Status payload (Node -> Gateway)
 */
typedef struct __attribute__((packed)) {
    uint8_t on;                 // 0=off, 1=on
    uint8_t r;                  // Red
    uint8_t g;                  // Green
    uint8_t b;                  // Blue
    uint8_t brightness;         // Brightness
    uint8_t effect_id;          // Current effect
} payload_led_status_t;

// ============================================================================
// OTA Structures
// ============================================================================

#define OTA_CHUNK_SIZE              180     // Max chunk payload size (must fit in OMNIAPI_MAX_PAYLOAD)
#define OTA_BLOCK_SIZE              4096    // Logical block size for node requests

/**
 * OTA Available payload (Gateway -> Nodes broadcast)
 * Announces that new firmware is available
 */
typedef struct __attribute__((packed)) {
    uint8_t  device_type;       // Target device type (DEVICE_TYPE_RELAY, etc.)
    uint32_t firmware_version;  // New version (major<<16 | minor<<8 | patch)
    uint32_t total_size;        // Total firmware size in bytes
    uint8_t  sha256[32];        // SHA256 hash of firmware
    uint16_t chunk_size;        // Size of each chunk (OTA_CHUNK_SIZE)
} payload_ota_available_t;

/**
 * OTA Request payload (Node -> Gateway)
 * Node requests a specific chunk of firmware
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint32_t offset;            // Byte offset to request
    uint16_t length;            // Requested length (usually OTA_CHUNK_SIZE)
} payload_ota_request_t;

/**
 * OTA Data payload (Gateway -> Node)
 * Contains firmware data chunk
 */
typedef struct __attribute__((packed)) {
    uint32_t offset;            // Byte offset of this chunk
    uint16_t length;            // Actual data length
    uint8_t  last_chunk;        // 1 if this is the last chunk
    uint8_t  data[OTA_CHUNK_SIZE]; // Chunk data
} payload_ota_data_t;

/**
 * OTA Complete payload (Node -> Gateway)
 * Node reports successful OTA, about to reboot
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint32_t new_version;       // New firmware version installed
} payload_ota_complete_t;

/**
 * OTA Failed payload (Node -> Gateway)
 * Node reports OTA failure
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  error_code;        // Error code
    char     error_msg[32];     // Error description
} payload_ota_failed_t;

/**
 * OTA Abort payload (Gateway -> Node)
 * Gateway tells node to abort OTA
 */
typedef struct __attribute__((packed)) {
    uint8_t  device_type;       // Target device type (0 = all)
} payload_ota_abort_t;

// ============================================================================
// Push-Mode OTA Structures (Gateway pushes firmware to specific node)
// ============================================================================

/**
 * OTA Begin payload (Gateway -> Node)
 * Starts a targeted OTA session with a specific node
 */
typedef struct __attribute__((packed)) {
    uint8_t  target_mac[6];     // Target node MAC
    uint32_t total_size;        // Total firmware size in bytes
    uint16_t chunk_size;        // Size of each chunk (typically 1024)
    uint16_t total_chunks;      // Total number of chunks
    uint32_t firmware_crc;      // CRC32 of entire firmware
} payload_ota_begin_t;

/**
 * OTA ACK payload (Node -> Gateway)
 * Node acknowledges receipt of a chunk
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint16_t chunk_index;       // Chunk index that was received
    uint8_t  status;            // 0=OK, 1=CRC_ERROR, 2=WRITE_ERROR, 3=ABORT
} payload_ota_ack_t;

/**
 * OTA End payload (Gateway -> Node)
 * Signals all chunks have been sent, node should finalize
 */
typedef struct __attribute__((packed)) {
    uint8_t  target_mac[6];     // Target node MAC
    uint16_t total_chunks;      // Total chunks sent (for verification)
    uint32_t firmware_crc;      // Final CRC for verification
} payload_ota_end_t;

// OTA ACK status codes
#define OTA_ACK_OK              0x00
#define OTA_ACK_CRC_ERROR       0x01
#define OTA_ACK_WRITE_ERROR     0x02
#define OTA_ACK_ABORT           0x03
#define OTA_ACK_READY           0x04    // Node ready to receive chunks

// ============================================================================
// Configuration Structures
// ============================================================================

// Relay control modes
#define RELAY_MODE_GPIO         0x00    // GPIO direct control (IN/CH1 pin)
#define RELAY_MODE_UART         0x01    // UART serial control (RX/TX pins)

// Config keys
#define CONFIG_KEY_RELAY_MODE   0x01    // Relay control mode

/**
 * Config Set payload (Gateway -> Node)
 * Sets a configuration value on the node
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Target node MAC
    uint8_t  config_key;        // Configuration key (CONFIG_KEY_*)
    uint8_t  value_len;         // Length of value
    uint8_t  value[32];         // Configuration value
} payload_config_set_t;

/**
 * Config ACK payload (Node -> Gateway)
 * Confirms configuration was applied
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  config_key;        // Configuration key that was set
    uint8_t  status;            // 0=success, >0=error
} payload_config_ack_t;

/**
 * Config Get payload (Gateway -> Node)
 * Requests a configuration value from the node
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Target node MAC
    uint8_t  config_key;        // Configuration key to get
} payload_config_get_t;

/**
 * Config Response payload (Node -> Gateway)
 * Returns a configuration value
 */
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];            // Node MAC
    uint8_t  config_key;        // Configuration key
    uint8_t  value_len;         // Length of value
    uint8_t  value[32];         // Configuration value
} payload_config_response_t;

/**
 * Error payload
 */
typedef struct __attribute__((packed)) {
    uint8_t error_code;         // Error code
    uint8_t original_msg_type;  // Message that caused error
    char    error_msg[32];      // Error description
} payload_error_t;

// ============================================================================
// Helper Macros
// ============================================================================

#define OMNIAPI_MSG_SIZE(payload_len)   (OMNIAPI_HEADER_SIZE + (payload_len))

#define OMNIAPI_INIT_HEADER(hdr, type, seqnum, len) do { \
    (hdr)->magic = OMNIAPI_MAGIC; \
    (hdr)->version = OMNIAPI_PROTOCOL_VERSION; \
    (hdr)->msg_type = (type); \
    (hdr)->seq = (seqnum); \
    (hdr)->flags = 0; \
    (hdr)->payload_len = (len); \
} while(0)

// ============================================================================
// MQTT Topic Prefixes
// ============================================================================
#define MQTT_TOPIC_PREFIX           "omniapi"
#define MQTT_TOPIC_GATEWAY          "omniapi/gateway"
#define MQTT_TOPIC_NODES            "omniapi/gateway/nodes"
#define MQTT_TOPIC_CMD              "omniapi/gateway/cmd"
#define MQTT_TOPIC_STATE            "omniapi/gateway/state"
#define MQTT_TOPIC_SCAN             "omniapi/gateway/scan"
#define MQTT_TOPIC_COMMISSION       "omniapi/gateway/commission"
#define MQTT_TOPIC_OTA_START        "omniapi/gateway/ota/start"
#define MQTT_TOPIC_OTA_PROGRESS     "omniapi/gateway/ota/progress"
#define MQTT_TOPIC_OTA_COMPLETE     "omniapi/gateway/ota/complete"
#define MQTT_TOPIC_OTA_ABORT        "omniapi/gateway/ota/abort"

// ============================================================================
// OTA Error Codes
// ============================================================================
#define OTA_ERR_NONE                0x00
#define OTA_ERR_DOWNLOAD_FAILED     0x01
#define OTA_ERR_SHA256_MISMATCH     0x02
#define OTA_ERR_PARTITION_ERROR     0x03
#define OTA_ERR_WRITE_FAILED        0x04
#define OTA_ERR_TIMEOUT             0x05
#define OTA_ERR_BOOT_FAILED         0x06
#define OTA_ERR_VERSION_MISMATCH    0x07

#ifdef __cplusplus
}
#endif

#endif // OMNIAPI_PROTOCOL_H
