/**
 * OmniaPi Node Mesh - OTA Receiver
 *
 * Handles OTA firmware updates received from gateway via mesh.
 * Flow:
 * 1. Receive MSG_OTA_AVAILABLE broadcast
 * 2. Check if update applies to this device
 * 3. Request chunks via MSG_OTA_REQUEST
 * 4. Receive chunks via MSG_OTA_DATA
 * 5. Verify SHA256, set boot partition
 * 6. Send MSG_OTA_COMPLETE/MSG_OTA_FAILED
 * 7. Reboot if successful
 */

#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

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
#define OTA_REQUEST_TIMEOUT_MS      5000    // Timeout waiting for chunk
#define OTA_MAX_RETRIES             3       // Max retries per chunk
#define OTA_TOTAL_TIMEOUT_MS        600000  // 10 minutes total timeout

// ============================================================================
// OTA State
// ============================================================================
typedef enum {
    OTA_RX_STATE_IDLE = 0,
    OTA_RX_STATE_RECEIVING,
    OTA_RX_STATE_VERIFYING,
    OTA_RX_STATE_COMPLETE,
    OTA_RX_STATE_FAILED
} ota_rx_state_t;

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize OTA receiver
 */
esp_err_t ota_receiver_init(void);

/**
 * Handle OTA available message from gateway (MSG_OTA_AVAILABLE)
 * @param available OTA available payload
 */
void ota_receiver_handle_available(const payload_ota_available_t *available);

/**
 * Handle OTA data message from gateway (MSG_OTA_DATA)
 * @param data OTA data payload
 */
void ota_receiver_handle_data(const payload_ota_data_t *data);

/**
 * Handle OTA abort message from gateway (MSG_OTA_ABORT)
 * @param abort OTA abort payload
 */
void ota_receiver_handle_abort(const payload_ota_abort_t *abort);

// ============================================================================
// Push-Mode OTA Functions (Gateway pushes firmware to node)
// ============================================================================

/**
 * Handle OTA begin message from gateway (MSG_OTA_BEGIN)
 * Starts a push-mode OTA session where gateway sends chunks
 * @param begin OTA begin payload
 */
void ota_receiver_handle_begin(const payload_ota_begin_t *begin);

/**
 * Handle OTA end message from gateway (MSG_OTA_END)
 * Finalizes push-mode OTA after all chunks received
 * @param end OTA end payload
 */
void ota_receiver_handle_end(const payload_ota_end_t *end);

/**
 * Check for OTA timeouts (call periodically)
 */
void ota_receiver_check_timeout(void);

/**
 * Get current OTA state
 */
ota_rx_state_t ota_receiver_get_state(void);

/**
 * Get OTA progress (0-100%)
 */
int ota_receiver_get_progress(void);

/**
 * Check if OTA is in progress
 */
bool ota_receiver_is_active(void);

/**
 * Abort current OTA
 */
void ota_receiver_abort(void);

/**
 * Check if node just booted from new firmware (call at startup)
 * Returns true if OTA complete message should be sent
 */
bool ota_receiver_check_post_update(void);

/**
 * Send OTA complete/failed message to gateway
 * @param success true if OTA succeeded
 * @param error_code Error code if failed
 * @param error_msg Error message if failed (can be NULL)
 */
void ota_receiver_send_result(bool success, uint8_t error_code, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif // OTA_RECEIVER_H
