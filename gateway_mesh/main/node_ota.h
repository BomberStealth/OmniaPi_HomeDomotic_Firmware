/**
 * OmniaPi Gateway - Node OTA Manager
 *
 * Handles push-mode OTA updates to mesh nodes
 * Gateway uploads firmware via web UI, then pushes chunks to target node
 */

#ifndef NODE_OTA_H
#define NODE_OTA_H

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
#define NODE_OTA_CHUNK_SIZE         180     // Chunk size (must match OTA_CHUNK_SIZE in protocol)
#define NODE_OTA_TIMEOUT_MS         60000   // Timeout waiting for ACK (60s)
#define NODE_OTA_MAX_RETRIES        3       // Max retries per chunk

// ============================================================================
// OTA State
// ============================================================================
typedef enum {
    NODE_OTA_STATE_IDLE = 0,
    NODE_OTA_STATE_STARTING,        // Sending OTA_BEGIN, waiting for ACK
    NODE_OTA_STATE_SENDING,         // Sending chunks
    NODE_OTA_STATE_FINISHING,       // Sent OTA_END, waiting for COMPLETE
    NODE_OTA_STATE_COMPLETE,        // Node reported success
    NODE_OTA_STATE_FAILED,          // OTA failed
    NODE_OTA_STATE_ABORTED          // OTA aborted
} node_ota_state_t;

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize node OTA manager
 * @return ESP_OK on success
 */
esp_err_t node_ota_init(void);

/**
 * Start OTA to a specific node (buffered mode - requires full firmware in RAM)
 * @param target_mac Target node MAC address (6 bytes)
 * @param firmware   Pointer to firmware data
 * @param size       Firmware size in bytes
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if OTA already in progress
 */
esp_err_t node_ota_start(const uint8_t *target_mac, const uint8_t *firmware, size_t size);

/**
 * Start streaming OTA to a specific node (no buffering needed)
 * @param target_mac    Target node MAC address (6 bytes)
 * @param total_size    Total firmware size in bytes
 * @return ESP_OK on success
 */
esp_err_t node_ota_start_stream(const uint8_t *target_mac, size_t total_size);

/**
 * Write a chunk in streaming mode
 * @param data      Chunk data
 * @param len       Chunk length
 * @param is_last   true if this is the last chunk
 * @return ESP_OK on success
 */
esp_err_t node_ota_write_chunk(const uint8_t *data, size_t len, bool is_last);

/**
 * Wait for node to acknowledge current chunk (for streaming mode)
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK if ACK received, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t node_ota_wait_ack(uint32_t timeout_ms);

/**
 * Check if node is ready to receive (after OTA_BEGIN)
 * @return true if ready
 */
bool node_ota_node_ready(void);

/**
 * Finish streaming OTA (send OTA_END)
 * @return ESP_OK on success
 */
esp_err_t node_ota_finish_stream(void);

/**
 * Handle OTA ACK from node
 * @param src_mac    Source MAC of the ACK
 * @param ack        ACK payload
 */
void node_ota_handle_ack(const uint8_t *src_mac, const payload_ota_ack_t *ack);

/**
 * Handle OTA Complete from node
 * @param src_mac    Source MAC
 * @param complete   Complete payload
 */
void node_ota_handle_complete(const uint8_t *src_mac, const payload_ota_complete_t *complete);

/**
 * Handle OTA Failed from node
 * @param src_mac    Source MAC
 * @param failed     Failed payload
 */
void node_ota_handle_failed(const uint8_t *src_mac, const payload_ota_failed_t *failed);

/**
 * Abort current OTA
 * @return ESP_OK on success
 */
esp_err_t node_ota_abort(void);

/**
 * Check for timeout (call periodically)
 */
void node_ota_check_timeout(void);

/**
 * Get current OTA state
 * @return Current state
 */
node_ota_state_t node_ota_get_state(void);

/**
 * Get OTA progress
 * @return Progress percentage (0-100)
 */
int node_ota_get_progress(void);

/**
 * Check if OTA is active
 * @return true if OTA in progress
 */
bool node_ota_is_active(void);

/**
 * Get target node MAC (if OTA active)
 * @param mac Buffer to store MAC (6 bytes)
 * @return ESP_OK if OTA active, ESP_ERR_INVALID_STATE otherwise
 */
esp_err_t node_ota_get_target_mac(uint8_t *mac);

// ============================================================================
// Async Flash-Based OTA (firmware buffered on gateway flash)
// ============================================================================

/**
 * Prepare flash storage for node firmware upload
 * Uses the gateway's inactive OTA partition as staging area
 * @param target_mac  Target node MAC address
 * @param total_size  Total firmware size
 * @return ESP_OK on success
 */
esp_err_t node_ota_flash_begin(const uint8_t *target_mac, size_t total_size);

/**
 * Write firmware chunk to flash staging area
 * @param data   Chunk data
 * @param len    Chunk length
 * @return ESP_OK on success
 */
esp_err_t node_ota_flash_write(const uint8_t *data, size_t len);

/**
 * Finish writing to flash and start async OTA to node
 * Returns immediately - OTA runs in background task
 * @return ESP_OK on success
 */
esp_err_t node_ota_flash_finish(void);

/**
 * Check if flash staging is in progress (HTTP upload phase)
 * @return true if staging active
 */
bool node_ota_flash_staging_active(void);

#ifdef __cplusplus
}
#endif

#endif // NODE_OTA_H
