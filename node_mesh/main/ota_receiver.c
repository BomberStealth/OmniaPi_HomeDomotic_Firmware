/**
 * OmniaPi Node Mesh - OTA Receiver Implementation
 *
 * Handles OTA firmware updates received from gateway via mesh.
 * Node-initiated chunk requests for reliable transfer.
 */

#include "ota_receiver.h"
#include "mesh_node.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_crc.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "OTA_RX";

// ============================================================================
// NVS Keys for post-update tracking
// ============================================================================
#define NVS_NAMESPACE           "ota_state"
#define NVS_KEY_PENDING         "pending"
#define NVS_KEY_VERSION         "new_ver"

// ============================================================================
// OTA Mode
// ============================================================================
typedef enum {
    OTA_MODE_NONE = 0,
    OTA_MODE_PULL,      // Node requests chunks from gateway
    OTA_MODE_PUSH       // Gateway pushes chunks to node
} ota_mode_t;

// ============================================================================
// OTA State Structure
// ============================================================================
typedef struct {
    // Update info
    uint32_t firmware_version;      // Target version
    uint32_t total_size;            // Total firmware size
    uint8_t  sha256[32];            // Expected SHA256 (pull mode)
    uint32_t firmware_crc;          // Expected CRC32 (push mode)
    uint16_t chunk_size;            // Chunk size from gateway

    // ESP OTA handles
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition;

    // Progress tracking
    uint32_t received_size;         // Bytes received so far
    uint32_t next_offset;           // Next offset to request
    uint16_t total_chunks;          // Total chunks (push mode)
    uint16_t expected_chunk;        // Next expected chunk index (push mode)
    uint8_t  retries;               // Current retry count

    // Timing
    int64_t start_time;             // OTA start time
    int64_t last_request_time;      // Last chunk request time (pull mode)
    int64_t last_chunk_time;        // Last chunk received time (push mode)

    // State
    ota_rx_state_t state;
    ota_mode_t mode;                // Pull or Push mode

    // SHA256 context for incremental computation
    mbedtls_sha256_context sha_ctx;

    // CRC32 for push mode
    uint32_t computed_crc;
} ota_receive_t;

static ota_receive_t s_ota = {0};
static uint8_t s_seq = 0;
static uint8_t s_node_mac[6] = {0};

// Device type (from Kconfig)
static uint8_t s_device_type = DEVICE_TYPE_UNKNOWN;

// ============================================================================
// Forward Declarations
// ============================================================================
static void request_next_chunk(void);
static bool verify_sha256(void);
static bool verify_crc32(void);
static void complete_ota(void);
static void fail_ota(uint8_t error_code, const char *error_msg);
static void cleanup_ota(void);
static uint8_t get_device_type(void);
static void send_ota_ack(uint16_t chunk_index, uint8_t status);

// ============================================================================
// Initialization
// ============================================================================

esp_err_t ota_receiver_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA receiver");

    memset(&s_ota, 0, sizeof(s_ota));
    s_ota.state = OTA_RX_STATE_IDLE;

    // Get our MAC
    esp_read_mac(s_node_mac, ESP_MAC_WIFI_STA);

    // Determine device type
    s_device_type = get_device_type();

    ESP_LOGI(TAG, "OTA receiver initialized (device_type=0x%02X)", s_device_type);
    return ESP_OK;
}

static uint8_t get_device_type(void)
{
#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    return DEVICE_TYPE_RELAY;
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    return DEVICE_TYPE_LED_STRIP;
#else
    return DEVICE_TYPE_SENSOR;
#endif
}

// ============================================================================
// Handle OTA Available
// ============================================================================

void ota_receiver_handle_available(const payload_ota_available_t *available)
{
    if (available == NULL) return;

    ESP_LOGI(TAG, "OTA AVAILABLE: device_type=0x%02X, version=%lu.%lu.%lu, size=%lu",
             available->device_type,
             (unsigned long)((available->firmware_version >> 16) & 0xFF),
             (unsigned long)((available->firmware_version >> 8) & 0xFF),
             (unsigned long)(available->firmware_version & 0xFF),
             (unsigned long)available->total_size);

    // Check if this update is for us
    if (available->device_type != s_device_type && available->device_type != 0) {
        ESP_LOGD(TAG, "OTA not for this device type (ours=0x%02X)", s_device_type);
        return;
    }

    // Check if we already have this version or newer
    const esp_app_desc_t *app_desc = esp_app_get_description();
    uint32_t current_version = 0;
    int major, minor, patch;
    if (sscanf(app_desc->version, "%d.%d.%d", &major, &minor, &patch) == 3) {
        current_version = (major << 16) | (minor << 8) | patch;
    }

    if (available->firmware_version <= current_version) {
        ESP_LOGI(TAG, "Already have version %lu.%lu.%lu or newer",
                 (unsigned long)((current_version >> 16) & 0xFF),
                 (unsigned long)((current_version >> 8) & 0xFF),
                 (unsigned long)(current_version & 0xFF));
        return;
    }

    // Check if OTA already in progress
    if (s_ota.state != OTA_RX_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress, ignoring");
        return;
    }

    ESP_LOGI(TAG, "Starting OTA update...");

    // Initialize OTA state
    s_ota.firmware_version = available->firmware_version;
    s_ota.total_size = available->total_size;
    memcpy(s_ota.sha256, available->sha256, 32);
    s_ota.chunk_size = available->chunk_size;
    s_ota.received_size = 0;
    s_ota.next_offset = 0;
    s_ota.retries = 0;
    s_ota.start_time = esp_timer_get_time() / 1000;
    s_ota.last_request_time = 0;

    // Get update partition
    s_ota.update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota.update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found!");
        fail_ota(OTA_ERR_PARTITION_ERROR, "No OTA partition");
        return;
    }

    ESP_LOGI(TAG, "OTA partition: %s (offset=0x%lx, size=%lu)",
             s_ota.update_partition->label,
             (unsigned long)s_ota.update_partition->address,
             (unsigned long)s_ota.update_partition->size);

    // Begin OTA
    esp_err_t err = esp_ota_begin(s_ota.update_partition, s_ota.total_size, &s_ota.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        fail_ota(OTA_ERR_PARTITION_ERROR, "OTA begin failed");
        return;
    }

    // Initialize SHA256 context
    mbedtls_sha256_init(&s_ota.sha_ctx);
    mbedtls_sha256_starts(&s_ota.sha_ctx, 0);

    s_ota.state = OTA_RX_STATE_RECEIVING;

    // Request first chunk
    request_next_chunk();
}

// ============================================================================
// Request Next Chunk
// ============================================================================

static void request_next_chunk(void)
{
    if (s_ota.state != OTA_RX_STATE_RECEIVING) return;

    // Calculate request size
    uint16_t request_len = s_ota.chunk_size;
    if (s_ota.next_offset + request_len > s_ota.total_size) {
        request_len = s_ota.total_size - s_ota.next_offset;
    }

    ESP_LOGD(TAG, "Requesting chunk: offset=%lu, len=%d",
             (unsigned long)s_ota.next_offset, request_len);

    // Build request message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_REQUEST, ++s_seq, sizeof(payload_ota_request_t));

    payload_ota_request_t *request = (payload_ota_request_t *)msg.payload;
    memcpy(request->mac, s_node_mac, 6);
    request->offset = s_ota.next_offset;
    request->length = request_len;

    // Send to gateway
    mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_request_t)));

    s_ota.last_request_time = esp_timer_get_time() / 1000;
}

// ============================================================================
// Verify SHA256
// ============================================================================

static bool verify_sha256(void)
{
    uint8_t computed[32];
    mbedtls_sha256_finish(&s_ota.sha_ctx, computed);
    mbedtls_sha256_free(&s_ota.sha_ctx);

    if (memcmp(computed, s_ota.sha256, 32) == 0) {
        ESP_LOGI(TAG, "SHA256 verified successfully");
        return true;
    }

    ESP_LOGE(TAG, "SHA256 mismatch!");
    ESP_LOGE(TAG, "Expected: %02x%02x%02x%02x...",
             s_ota.sha256[0], s_ota.sha256[1], s_ota.sha256[2], s_ota.sha256[3]);
    ESP_LOGE(TAG, "Computed: %02x%02x%02x%02x...",
             computed[0], computed[1], computed[2], computed[3]);

    return false;
}

// ============================================================================
// Complete OTA
// ============================================================================

static void complete_ota(void)
{
    ESP_LOGI(TAG, "Completing OTA...");

    // Finish OTA write
    esp_err_t err = esp_ota_end(s_ota.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        fail_ota(OTA_ERR_WRITE_FAILED, "OTA end failed");
        return;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(s_ota.update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        fail_ota(OTA_ERR_PARTITION_ERROR, "Set boot failed");
        return;
    }

    // Save pending OTA state to NVS (for post-reboot verification)
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_PENDING, 1);
        nvs_set_u32(nvs, NVS_KEY_VERSION, s_ota.firmware_version);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    s_ota.state = OTA_RX_STATE_COMPLETE;

    // Send complete message to gateway
    ota_receiver_send_result(true, OTA_ERR_NONE, NULL);

    ESP_LOGI(TAG, "=== OTA COMPLETE! Rebooting in 2 seconds... ===");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

// ============================================================================
// Fail OTA
// ============================================================================

static void fail_ota(uint8_t error_code, const char *error_msg)
{
    ESP_LOGE(TAG, "OTA FAILED: %s (code=%d)", error_msg, error_code);

    s_ota.state = OTA_RX_STATE_FAILED;

    // Send failure message
    ota_receiver_send_result(false, error_code, error_msg);

    // Cleanup
    cleanup_ota();
}

// ============================================================================
// Cleanup
// ============================================================================

static void cleanup_ota(void)
{
    if (s_ota.ota_handle != 0) {
        esp_ota_abort(s_ota.ota_handle);
        s_ota.ota_handle = 0;
    }

    mbedtls_sha256_free(&s_ota.sha_ctx);

    s_ota.state = OTA_RX_STATE_IDLE;
    s_ota.received_size = 0;
    s_ota.next_offset = 0;
}

// ============================================================================
// Handle OTA Abort
// ============================================================================

void ota_receiver_handle_abort(const payload_ota_abort_t *abort)
{
    if (abort == NULL) return;

    // Check if abort applies to us
    if (abort->device_type != 0 && abort->device_type != s_device_type) {
        return;
    }

    if (s_ota.state == OTA_RX_STATE_IDLE) {
        return;
    }

    ESP_LOGW(TAG, "OTA ABORT received from gateway");
    cleanup_ota();
}

// ============================================================================
// Timeout Check
// ============================================================================

void ota_receiver_check_timeout(void)
{
    if (s_ota.state != OTA_RX_STATE_RECEIVING) {
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;

    // Check overall timeout
    if ((now - s_ota.start_time) > OTA_TOTAL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "OTA total timeout");
        fail_ota(OTA_ERR_TIMEOUT, "Total timeout");
        return;
    }

    // Check chunk request timeout
    if (s_ota.last_request_time > 0 &&
        (now - s_ota.last_request_time) > OTA_REQUEST_TIMEOUT_MS) {

        s_ota.retries++;
        if (s_ota.retries > OTA_MAX_RETRIES) {
            ESP_LOGE(TAG, "OTA chunk request timeout after %d retries", OTA_MAX_RETRIES);
            fail_ota(OTA_ERR_TIMEOUT, "Chunk timeout");
            return;
        }

        ESP_LOGW(TAG, "Chunk request timeout, retry %d/%d", s_ota.retries, OTA_MAX_RETRIES);
        request_next_chunk();
    }
}

// ============================================================================
// Getters
// ============================================================================

ota_rx_state_t ota_receiver_get_state(void)
{
    return s_ota.state;
}

int ota_receiver_get_progress(void)
{
    if (s_ota.total_size == 0) return 0;
    return (s_ota.received_size * 100) / s_ota.total_size;
}

bool ota_receiver_is_active(void)
{
    return (s_ota.state == OTA_RX_STATE_RECEIVING ||
            s_ota.state == OTA_RX_STATE_VERIFYING);
}

void ota_receiver_abort(void)
{
    if (s_ota.state != OTA_RX_STATE_IDLE) {
        ESP_LOGW(TAG, "Aborting OTA");
        cleanup_ota();
    }
}

// ============================================================================
// Post-Update Check (call at startup)
// ============================================================================

bool ota_receiver_check_post_update(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }

    uint8_t pending = 0;
    nvs_get_u8(nvs, NVS_KEY_PENDING, &pending);

    if (pending) {
        // Clear pending flag
        nvs_erase_key(nvs, NVS_KEY_PENDING);
        nvs_commit(nvs);
        nvs_close(nvs);

        // Check if we're running from the expected partition
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *boot = esp_ota_get_boot_partition();

        if (running == boot) {
            ESP_LOGI(TAG, "Post-update check: Running from new partition");
            return true;  // Send OTA complete
        } else {
            ESP_LOGW(TAG, "Post-update check: Boot failed, rolled back");
            // Send OTA failed (rollback happened)
            ota_receiver_send_result(false, OTA_ERR_BOOT_FAILED, "Boot rollback");
            return false;
        }
    }

    nvs_close(nvs);
    return false;
}

// ============================================================================
// Send OTA Result
// ============================================================================

void ota_receiver_send_result(bool success, uint8_t error_code, const char *error_msg)
{
    omniapi_message_t msg;

    if (success) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_COMPLETE, ++s_seq, sizeof(payload_ota_complete_t));

        payload_ota_complete_t *complete = (payload_ota_complete_t *)msg.payload;
        memcpy(complete->mac, s_node_mac, 6);
        complete->new_version = s_ota.firmware_version;

        ESP_LOGI(TAG, "Sending OTA COMPLETE");
        mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_complete_t)));
    } else {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_FAILED, ++s_seq, sizeof(payload_ota_failed_t));

        payload_ota_failed_t *failed = (payload_ota_failed_t *)msg.payload;
        memcpy(failed->mac, s_node_mac, 6);
        failed->error_code = error_code;
        if (error_msg) {
            strncpy(failed->error_msg, error_msg, sizeof(failed->error_msg) - 1);
        } else {
            failed->error_msg[0] = '\0';
        }

        ESP_LOGW(TAG, "Sending OTA FAILED: %s (code=%d)", error_msg ? error_msg : "unknown", error_code);
        mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_failed_t)));
    }
}

// ============================================================================
// Push-Mode OTA Functions
// ============================================================================

/**
 * Send OTA ACK to gateway (push mode)
 */
static void send_ota_ack(uint16_t chunk_index, uint8_t status)
{
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_ACK, ++s_seq, sizeof(payload_ota_ack_t));

    payload_ota_ack_t *ack = (payload_ota_ack_t *)msg.payload;
    memcpy(ack->mac, s_node_mac, 6);
    ack->chunk_index = chunk_index;
    ack->status = status;

    ESP_LOGD(TAG, "Sending OTA ACK: chunk=%u, status=%u", chunk_index, status);
    mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_ack_t)));
}

/**
 * Verify CRC32 of received firmware (push mode)
 */
static bool verify_crc32(void)
{
    if (s_ota.computed_crc == s_ota.firmware_crc) {
        ESP_LOGI(TAG, "CRC32 verified: 0x%08lx", (unsigned long)s_ota.computed_crc);
        return true;
    }

    ESP_LOGE(TAG, "CRC32 mismatch! Expected: 0x%08lx, Computed: 0x%08lx",
             (unsigned long)s_ota.firmware_crc, (unsigned long)s_ota.computed_crc);
    return false;
}

/**
 * Handle OTA BEGIN (push mode - gateway initiates)
 */
void ota_receiver_handle_begin(const payload_ota_begin_t *begin)
{
    if (begin == NULL) return;

    // Verify this message is for us
    if (memcmp(begin->target_mac, s_node_mac, 6) != 0) {
        ESP_LOGD(TAG, "OTA_BEGIN not for this node");
        return;
    }

    ESP_LOGI(TAG, "OTA_BEGIN: size=%lu, chunks=%u, chunk_size=%u, crc=0x%08lx",
             (unsigned long)begin->total_size, begin->total_chunks,
             begin->chunk_size, (unsigned long)begin->firmware_crc);

    // Check if OTA already in progress
    if (s_ota.state != OTA_RX_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress, aborting previous");
        cleanup_ota();
    }

    // Initialize OTA state for push mode
    s_ota.mode = OTA_MODE_PUSH;
    s_ota.total_size = begin->total_size;
    s_ota.chunk_size = begin->chunk_size;
    s_ota.total_chunks = begin->total_chunks;
    s_ota.firmware_crc = begin->firmware_crc;
    s_ota.firmware_version = 0;  // Will be read from firmware header later
    s_ota.received_size = 0;
    s_ota.expected_chunk = 0;
    s_ota.computed_crc = 0;
    s_ota.retries = 0;
    s_ota.start_time = esp_timer_get_time() / 1000;
    s_ota.last_chunk_time = s_ota.start_time;

    // Get update partition
    s_ota.update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota.update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found!");
        send_ota_ack(0, OTA_ACK_ABORT);
        return;
    }

    ESP_LOGI(TAG, "OTA partition: %s (offset=0x%lx, size=%lu)",
             s_ota.update_partition->label,
             (unsigned long)s_ota.update_partition->address,
             (unsigned long)s_ota.update_partition->size);

    // Check partition size
    if (s_ota.total_size > s_ota.update_partition->size) {
        ESP_LOGE(TAG, "Firmware too large: %lu > %lu",
                 (unsigned long)s_ota.total_size,
                 (unsigned long)s_ota.update_partition->size);
        send_ota_ack(0, OTA_ACK_ABORT);
        return;
    }

    // Begin OTA
    esp_err_t err = esp_ota_begin(s_ota.update_partition, s_ota.total_size, &s_ota.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        send_ota_ack(0, OTA_ACK_ABORT);
        return;
    }

    s_ota.state = OTA_RX_STATE_RECEIVING;

    // Send READY ACK
    send_ota_ack(0, OTA_ACK_READY);
    ESP_LOGI(TAG, "Ready to receive %u chunks", s_ota.total_chunks);
}

/**
 * Handle OTA DATA - updated to support both pull and push modes
 */
void ota_receiver_handle_data(const payload_ota_data_t *data)
{
    if (data == NULL) return;

    if (s_ota.state != OTA_RX_STATE_RECEIVING) {
        ESP_LOGW(TAG, "OTA data received but not in receiving state");
        return;
    }

    s_ota.last_chunk_time = esp_timer_get_time() / 1000;

    if (s_ota.mode == OTA_MODE_PUSH) {
        // Push mode: gateway sends chunks, we send ACKs
        uint16_t chunk_index = data->offset / s_ota.chunk_size;

        ESP_LOGD(TAG, "OTA DATA (push): chunk=%u, offset=%lu, len=%u, last=%u",
                 chunk_index, (unsigned long)data->offset, data->length, data->last_chunk);

        // Verify chunk index matches expected
        if (data->offset != s_ota.received_size) {
            ESP_LOGW(TAG, "Unexpected offset: got %lu, expected %lu",
                     (unsigned long)data->offset, (unsigned long)s_ota.received_size);
            // Could request retransmit, but for now just accept if it's a retry
            if (data->offset < s_ota.received_size) {
                // Duplicate chunk, ACK it anyway
                send_ota_ack(chunk_index, OTA_ACK_OK);
                return;
            }
            // Gap in data - this shouldn't happen in push mode
            send_ota_ack(chunk_index, OTA_ACK_CRC_ERROR);
            return;
        }

        // Write data to OTA partition
        esp_err_t err = esp_ota_write(s_ota.ota_handle, data->data, data->length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            send_ota_ack(chunk_index, OTA_ACK_WRITE_ERROR);
            fail_ota(OTA_ERR_WRITE_FAILED, "Write failed");
            return;
        }

        // Update CRC32
        s_ota.computed_crc = esp_crc32_le(s_ota.computed_crc, data->data, data->length);

        // Update progress
        s_ota.received_size += data->length;
        s_ota.expected_chunk = chunk_index + 1;

        // Log progress
        int progress = (s_ota.received_size * 100) / s_ota.total_size;
        static int last_progress = -1;
        if (progress / 10 != last_progress / 10) {
            ESP_LOGI(TAG, "OTA progress: %d%% (%lu/%lu bytes)",
                     progress, (unsigned long)s_ota.received_size, (unsigned long)s_ota.total_size);
            last_progress = progress;
        }

        // Send ACK
        send_ota_ack(chunk_index, OTA_ACK_OK);

    } else {
        // Pull mode: existing behavior
        ESP_LOGD(TAG, "OTA DATA (pull): offset=%lu, len=%d, last=%d",
                 (unsigned long)data->offset, data->length, data->last_chunk);

        // Verify offset matches expected
        if (data->offset != s_ota.next_offset) {
            ESP_LOGW(TAG, "Unexpected offset: got %lu, expected %lu",
                     (unsigned long)data->offset, (unsigned long)s_ota.next_offset);
            // Re-request correct offset
            request_next_chunk();
            return;
        }

        // Write data to OTA partition
        esp_err_t err = esp_ota_write(s_ota.ota_handle, data->data, data->length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            fail_ota(OTA_ERR_WRITE_FAILED, "Write failed");
            return;
        }

        // Update SHA256
        mbedtls_sha256_update(&s_ota.sha_ctx, data->data, data->length);

        // Update progress
        s_ota.received_size += data->length;
        s_ota.next_offset += data->length;
        s_ota.retries = 0;  // Reset retry counter

        // Log progress
        int progress = (s_ota.received_size * 100) / s_ota.total_size;
        static int last_progress_pull = -1;
        if (progress / 10 != last_progress_pull / 10) {
            ESP_LOGI(TAG, "OTA progress: %d%% (%lu/%lu bytes)",
                     progress, (unsigned long)s_ota.received_size, (unsigned long)s_ota.total_size);
            last_progress_pull = progress;
        }

        // Check if complete
        if (data->last_chunk || s_ota.received_size >= s_ota.total_size) {
            ESP_LOGI(TAG, "All data received, verifying SHA256...");
            s_ota.state = OTA_RX_STATE_VERIFYING;

            if (verify_sha256()) {
                complete_ota();
            } else {
                fail_ota(OTA_ERR_SHA256_MISMATCH, "SHA256 mismatch");
            }
        } else {
            // Request next chunk
            request_next_chunk();
        }
    }
}

/**
 * Handle OTA END (push mode - all chunks sent, verify and finalize)
 */
void ota_receiver_handle_end(const payload_ota_end_t *end)
{
    if (end == NULL) return;

    // Verify this message is for us
    if (memcmp(end->target_mac, s_node_mac, 6) != 0) {
        ESP_LOGD(TAG, "OTA_END not for this node");
        return;
    }

    if (s_ota.state != OTA_RX_STATE_RECEIVING || s_ota.mode != OTA_MODE_PUSH) {
        ESP_LOGW(TAG, "OTA_END received in wrong state");
        return;
    }

    ESP_LOGI(TAG, "OTA_END: chunks=%u, crc=0x%08lx",
             end->total_chunks, (unsigned long)end->firmware_crc);

    // Verify all chunks received
    if (s_ota.received_size != s_ota.total_size) {
        ESP_LOGE(TAG, "Not all data received: %lu/%lu",
                 (unsigned long)s_ota.received_size, (unsigned long)s_ota.total_size);
        fail_ota(OTA_ERR_DOWNLOAD_FAILED, "Incomplete data");
        return;
    }

    // Verify CRC
    s_ota.state = OTA_RX_STATE_VERIFYING;
    ESP_LOGI(TAG, "Verifying CRC32...");

    if (!verify_crc32()) {
        fail_ota(OTA_ERR_SHA256_MISMATCH, "CRC mismatch");
        return;
    }

    // Complete OTA
    complete_ota();
}
