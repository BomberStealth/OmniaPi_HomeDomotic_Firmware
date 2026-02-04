/**
 * OmniaPi Gateway Mesh - OTA Manager
 *
 * Manages firmware distribution to mesh nodes via OTA
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "omniapi_protocol.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================
#define OTA_MAX_TARGETS             16      // Max nodes per OTA job
#define OTA_TIMEOUT_MS              600000  // 10 minutes timeout
#define OTA_RETRY_COUNT             3       // Retries per chunk

// ============================================================================
// OTA Job State
// ============================================================================
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,          // Downloading firmware from backend
    OTA_STATE_READY,                // Firmware downloaded, ready to distribute
    OTA_STATE_DISTRIBUTING,         // Sending to nodes
    OTA_STATE_COMPLETE,             // All nodes updated
    OTA_STATE_FAILED,               // OTA failed
    OTA_STATE_ABORTED               // OTA aborted
} ota_state_t;

// ============================================================================
// OTA Node Status
// ============================================================================
typedef struct {
    uint8_t  mac[6];                // Node MAC
    bool     active;                // Node is participating
    uint32_t received_bytes;        // Bytes received by node
    uint8_t  retries;               // Retry count
    bool     completed;             // Node completed OTA
    bool     failed;                // Node failed OTA
    uint8_t  error_code;            // Error code if failed
} ota_node_status_t;

// ============================================================================
// OTA Job Structure
// ============================================================================
typedef struct {
    // Job info
    char     version[16];           // Version string "x.y.z"
    uint32_t version_packed;        // Packed version (major<<16 | minor<<8 | patch)
    char     url[256];              // Firmware download URL
    uint8_t  sha256[32];            // Expected SHA256 hash
    uint32_t total_size;            // Firmware total size
    uint8_t  device_type;           // Target device type

    // Firmware buffer
    uint8_t* firmware_data;         // Downloaded firmware (malloc'd)

    // Target nodes
    uint8_t  target_macs[OTA_MAX_TARGETS][6];  // Target MACs (empty = all)
    uint8_t  target_count;          // Number of specific targets (0 = all of device_type)

    // Progress tracking
    ota_node_status_t nodes[OTA_MAX_TARGETS];  // Node status tracking
    uint8_t  nodes_active;          // Nodes participating
    uint8_t  nodes_completed;       // Nodes that completed
    uint8_t  nodes_failed;          // Nodes that failed

    // State
    ota_state_t state;              // Current state
    int64_t  start_time;            // Job start timestamp
    int64_t  last_activity;         // Last activity timestamp
} ota_job_t;

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize OTA manager
 */
esp_err_t ota_manager_init(void);

/**
 * Start a new OTA job
 * @param url Firmware download URL
 * @param version Version string "x.y.z"
 * @param sha256_hex SHA256 hash as hex string (64 chars)
 * @param size Firmware size in bytes
 * @param device_type Target device type (DEVICE_TYPE_RELAY, etc.)
 * @param target_macs Array of target MACs (NULL = all nodes of device_type)
 * @param target_count Number of targets (0 = all)
 */
esp_err_t ota_manager_start_job(const char* url, const char* version,
                                 const char* sha256_hex, uint32_t size,
                                 uint8_t device_type,
                                 const uint8_t target_macs[][6], uint8_t target_count);

/**
 * Abort current OTA job
 */
esp_err_t ota_manager_abort(void);

/**
 * Handle OTA request from node (MSG_OTA_REQUEST)
 * @param src_mac Source node MAC
 * @param request OTA request payload
 */
void ota_manager_handle_request(const uint8_t* src_mac, const payload_ota_request_t* request);

/**
 * Handle OTA complete from node (MSG_OTA_COMPLETE)
 * @param src_mac Source node MAC
 * @param complete OTA complete payload
 */
void ota_manager_handle_complete(const uint8_t* src_mac, const payload_ota_complete_t* complete);

/**
 * Handle OTA failed from node (MSG_OTA_FAILED)
 * @param src_mac Source node MAC
 * @param failed OTA failed payload
 */
void ota_manager_handle_failed(const uint8_t* src_mac, const payload_ota_failed_t* failed);

/**
 * Check for OTA timeout (call periodically)
 */
void ota_manager_check_timeout(void);

/**
 * Get current OTA state
 */
ota_state_t ota_manager_get_state(void);

/**
 * Get OTA progress
 * @param completed Number of nodes completed
 * @param failed Number of nodes failed
 * @param total Total number of participating nodes
 */
void ota_manager_get_progress(uint8_t* completed, uint8_t* failed, uint8_t* total);

/**
 * Check if OTA is in progress
 */
bool ota_manager_is_active(void);

/**
 * Get current OTA job info (read-only)
 */
const ota_job_t* ota_manager_get_job(void);

// ============================================================================
// Gateway Self-OTA Functions (for flashing gateway via Web UI upload)
// ============================================================================

/**
 * Begin gateway OTA update
 * @param total_size Expected firmware size in bytes
 * @return ESP_OK on success
 */
esp_err_t ota_gateway_begin(uint32_t total_size);

/**
 * Write chunk of firmware data to flash
 * @param data Pointer to data buffer
 * @param length Length of data to write
 * @return ESP_OK on success
 */
esp_err_t ota_gateway_write(const uint8_t *data, size_t length);

/**
 * End gateway OTA update, validate and switch boot partition
 * @return ESP_OK on success
 */
esp_err_t ota_gateway_end(void);

/**
 * Abort gateway OTA update
 * @return ESP_OK on success
 */
esp_err_t ota_gateway_abort(void);

/**
 * Get gateway OTA progress
 * @param written_bytes Bytes written so far
 * @param total_bytes Total bytes expected
 * @param progress_percent Progress percentage (0-100)
 */
void ota_gateway_get_progress(uint32_t *written_bytes, uint32_t *total_bytes, uint8_t *progress_percent);

/**
 * Check if gateway OTA is in progress
 * @return true if OTA is active
 */
bool ota_gateway_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_MANAGER_H
