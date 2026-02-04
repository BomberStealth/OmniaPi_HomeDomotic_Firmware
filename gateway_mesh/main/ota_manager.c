/**
 * OmniaPi Gateway Mesh - OTA Manager Implementation
 *
 * Handles firmware distribution to mesh nodes:
 * 1. Download firmware from backend via HTTP
 * 2. Verify SHA256
 * 3. Broadcast OTA availability to nodes
 * 4. Serve firmware chunks on request
 * 5. Track progress and report to backend
 */

#include "ota_manager.h"
#include "mesh_network.h"
#include "mqtt_handler.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA_MGR";

// ============================================================================
// State
// ============================================================================
static ota_job_t s_job = {0};
static uint8_t s_seq = 0;

// ============================================================================
// Forward Declarations
// ============================================================================
static esp_err_t download_firmware(void);
static bool verify_sha256(void);
static void hex_to_bytes(const char* hex, uint8_t* bytes, size_t len);
static uint32_t parse_version(const char* version);
static esp_err_t send_ota_available(void);
static esp_err_t send_chunk_to_node(const uint8_t* mac, uint32_t offset, uint16_t length);
static int find_node_index(const uint8_t* mac);
static void cleanup_job(void);

// ============================================================================
// Initialization
// ============================================================================

esp_err_t ota_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA manager");
    memset(&s_job, 0, sizeof(s_job));
    s_job.state = OTA_STATE_IDLE;
    ESP_LOGI(TAG, "OTA manager initialized");
    return ESP_OK;
}

// ============================================================================
// Start OTA Job
// ============================================================================

esp_err_t ota_manager_start_job(const char* url, const char* version,
                                 const char* sha256_hex, uint32_t size,
                                 uint8_t device_type,
                                 const uint8_t target_macs[][6], uint8_t target_count)
{
    if (url == NULL || version == NULL || sha256_hex == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid OTA job parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_job.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA job already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting OTA job:");
    ESP_LOGI(TAG, "  Version: %s", version);
    ESP_LOGI(TAG, "  URL: %s", url);
    ESP_LOGI(TAG, "  Size: %lu bytes", (unsigned long)size);
    ESP_LOGI(TAG, "  Device type: 0x%02X", device_type);
    ESP_LOGI(TAG, "  Targets: %d (0=all)", target_count);

    // Initialize job
    memset(&s_job, 0, sizeof(s_job));
    strncpy(s_job.url, url, sizeof(s_job.url) - 1);
    strncpy(s_job.version, version, sizeof(s_job.version) - 1);
    s_job.version_packed = parse_version(version);
    s_job.total_size = size;
    s_job.device_type = device_type;

    // Parse SHA256 from hex string
    if (strlen(sha256_hex) != 64) {
        ESP_LOGE(TAG, "Invalid SHA256 hex length: %d", strlen(sha256_hex));
        return ESP_ERR_INVALID_ARG;
    }
    hex_to_bytes(sha256_hex, s_job.sha256, 32);

    // Copy target MACs if specified
    if (target_macs != NULL && target_count > 0) {
        s_job.target_count = (target_count > OTA_MAX_TARGETS) ? OTA_MAX_TARGETS : target_count;
        for (int i = 0; i < s_job.target_count; i++) {
            memcpy(s_job.target_macs[i], target_macs[i], 6);
        }
    }

    s_job.start_time = esp_timer_get_time() / 1000;
    s_job.last_activity = s_job.start_time;
    s_job.state = OTA_STATE_DOWNLOADING;

    // Start download in background task
    xTaskCreate((TaskFunction_t)download_firmware, "ota_download", 8192, NULL, 5, NULL);

    return ESP_OK;
}

// ============================================================================
// Download Firmware
// ============================================================================

static esp_err_t download_firmware(void)
{
    ESP_LOGI(TAG, "Downloading firmware from: %s", s_job.url);

    // Allocate firmware buffer
    s_job.firmware_data = (uint8_t*)malloc(s_job.total_size);
    if (s_job.firmware_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %lu bytes for firmware", (unsigned long)s_job.total_size);
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "Memory allocation failed");
        vTaskDelete(NULL);
        return ESP_ERR_NO_MEM;
    }

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = s_job.url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "HTTP init failed");
        vTaskDelete(NULL);
        return ESP_FAIL;
    }

    // Open connection
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "HTTP connection failed");
        vTaskDelete(NULL);
        return err;
    }

    // Get content length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "Invalid content length");
        vTaskDelete(NULL);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Content length: %d bytes", content_length);

    // Download firmware
    uint32_t downloaded = 0;
    int read_len;
    uint8_t buffer[4096];

    while (downloaded < s_job.total_size) {
        read_len = esp_http_client_read(client, (char*)buffer, sizeof(buffer));
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        }
        if (read_len == 0) {
            break;  // EOF
        }

        // Copy to firmware buffer
        if (downloaded + read_len > s_job.total_size) {
            read_len = s_job.total_size - downloaded;
        }
        memcpy(s_job.firmware_data + downloaded, buffer, read_len);
        downloaded += read_len;

        // Update progress (every 10%)
        int progress = (downloaded * 100) / s_job.total_size;
        if (progress % 10 == 0) {
            ESP_LOGI(TAG, "Download progress: %d%% (%lu/%lu)",
                     progress, (unsigned long)downloaded, (unsigned long)s_job.total_size);
        }

        s_job.last_activity = esp_timer_get_time() / 1000;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (downloaded != s_job.total_size) {
        ESP_LOGE(TAG, "Download incomplete: %lu/%lu bytes",
                 (unsigned long)downloaded, (unsigned long)s_job.total_size);
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "Download incomplete");
        vTaskDelete(NULL);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Download complete: %lu bytes", (unsigned long)downloaded);

    // Verify SHA256
    if (!verify_sha256()) {
        ESP_LOGE(TAG, "SHA256 verification failed!");
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "SHA256 mismatch");
        vTaskDelete(NULL);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SHA256 verified successfully");

    // Ready to distribute
    s_job.state = OTA_STATE_READY;

    // Broadcast OTA availability
    err = send_ota_available();
    if (err == ESP_OK) {
        s_job.state = OTA_STATE_DISTRIBUTING;
        mqtt_publish_ota_progress(0, 0, 0, "Distributing to nodes");
    } else {
        ESP_LOGE(TAG, "Failed to send OTA available");
        s_job.state = OTA_STATE_FAILED;
        mqtt_publish_ota_progress(0, 1, 1, "Broadcast failed");
    }

    vTaskDelete(NULL);
    return err;
}

// ============================================================================
// SHA256 Verification
// ============================================================================

static bool verify_sha256(void)
{
    uint8_t computed[32];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256
    mbedtls_sha256_update(&ctx, s_job.firmware_data, s_job.total_size);
    mbedtls_sha256_finish(&ctx, computed);
    mbedtls_sha256_free(&ctx);

    // Compare with expected
    if (memcmp(computed, s_job.sha256, 32) == 0) {
        return true;
    }

    ESP_LOGE(TAG, "SHA256 mismatch!");
    ESP_LOGE(TAG, "Expected: %02x%02x%02x%02x...",
             s_job.sha256[0], s_job.sha256[1], s_job.sha256[2], s_job.sha256[3]);
    ESP_LOGE(TAG, "Computed: %02x%02x%02x%02x...",
             computed[0], computed[1], computed[2], computed[3]);

    return false;
}

// ============================================================================
// Broadcast OTA Available
// ============================================================================

static esp_err_t send_ota_available(void)
{
    ESP_LOGI(TAG, "Broadcasting OTA available (device_type=0x%02X, size=%lu, version=%s)",
             s_job.device_type, (unsigned long)s_job.total_size, s_job.version);

    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_AVAILABLE, ++s_seq, sizeof(payload_ota_available_t));

    payload_ota_available_t* payload = (payload_ota_available_t*)msg.payload;
    payload->device_type = s_job.device_type;
    payload->firmware_version = s_job.version_packed;
    payload->total_size = s_job.total_size;
    memcpy(payload->sha256, s_job.sha256, 32);
    payload->chunk_size = OTA_CHUNK_SIZE;

    return mesh_network_broadcast((uint8_t*)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_available_t)));
}

// ============================================================================
// Handle OTA Request from Node
// ============================================================================

void ota_manager_handle_request(const uint8_t* src_mac, const payload_ota_request_t* request)
{
    if (src_mac == NULL || request == NULL) return;

    if (s_job.state != OTA_STATE_DISTRIBUTING && s_job.state != OTA_STATE_READY) {
        ESP_LOGW(TAG, "OTA request received but not distributing");
        return;
    }

    ESP_LOGD(TAG, "OTA request from %02X:%02X:%02X:%02X:%02X:%02X offset=%lu len=%d",
             src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
             (unsigned long)request->offset, request->length);

    // Track node
    int idx = find_node_index(request->mac);
    if (idx < 0) {
        // New node joining OTA
        if (s_job.nodes_active < OTA_MAX_TARGETS) {
            idx = s_job.nodes_active++;
            memcpy(s_job.nodes[idx].mac, request->mac, 6);
            s_job.nodes[idx].active = true;
            s_job.nodes[idx].received_bytes = 0;
            s_job.nodes[idx].retries = 0;
            s_job.nodes[idx].completed = false;
            s_job.nodes[idx].failed = false;
            ESP_LOGI(TAG, "Node %02X:%02X:%02X:%02X:%02X:%02X joined OTA (total: %d)",
                     request->mac[0], request->mac[1], request->mac[2],
                     request->mac[3], request->mac[4], request->mac[5],
                     s_job.nodes_active);
        } else {
            ESP_LOGW(TAG, "Max OTA targets reached, ignoring node");
            return;
        }
    }

    // Update node progress
    s_job.nodes[idx].received_bytes = request->offset;
    s_job.last_activity = esp_timer_get_time() / 1000;

    // Send requested chunk
    send_chunk_to_node(request->mac, request->offset, request->length);
}

// ============================================================================
// Send Chunk to Node
// ============================================================================

static esp_err_t send_chunk_to_node(const uint8_t* mac, uint32_t offset, uint16_t length)
{
    if (s_job.firmware_data == NULL) {
        ESP_LOGE(TAG, "No firmware data to send");
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp length
    if (length > OTA_CHUNK_SIZE) {
        length = OTA_CHUNK_SIZE;
    }

    // Check bounds
    if (offset >= s_job.total_size) {
        ESP_LOGW(TAG, "Offset %lu beyond firmware size %lu",
                 (unsigned long)offset, (unsigned long)s_job.total_size);
        return ESP_ERR_INVALID_ARG;
    }

    // Adjust length if near end
    if (offset + length > s_job.total_size) {
        length = s_job.total_size - offset;
    }

    // Build OTA data message - use a larger buffer since payload_ota_data_t is bigger than standard payload
    static uint8_t ota_msg_buffer[sizeof(omniapi_header_t) + sizeof(payload_ota_data_t)];
    omniapi_header_t* header = (omniapi_header_t*)ota_msg_buffer;
    payload_ota_data_t* payload = (payload_ota_data_t*)(ota_msg_buffer + sizeof(omniapi_header_t));

    size_t payload_size = sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + length;
    OMNIAPI_INIT_HEADER(header, MSG_OTA_DATA, ++s_seq, payload_size);

    payload->offset = offset;
    payload->length = length;
    payload->last_chunk = (offset + length >= s_job.total_size) ? 1 : 0;
    memcpy(payload->data, s_job.firmware_data + offset, length);

    ESP_LOGD(TAG, "Sending chunk offset=%lu len=%d last=%d to %02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned long)offset, length, payload->last_chunk,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return mesh_network_send(mac, ota_msg_buffer, sizeof(omniapi_header_t) + payload_size);
}

// ============================================================================
// Handle OTA Complete from Node
// ============================================================================

void ota_manager_handle_complete(const uint8_t* src_mac, const payload_ota_complete_t* complete)
{
    if (src_mac == NULL || complete == NULL) return;

    ESP_LOGI(TAG, "OTA COMPLETE from %02X:%02X:%02X:%02X:%02X:%02X (version=%lu.%lu.%lu)",
             complete->mac[0], complete->mac[1], complete->mac[2],
             complete->mac[3], complete->mac[4], complete->mac[5],
             (unsigned long)((complete->new_version >> 16) & 0xFF),
             (unsigned long)((complete->new_version >> 8) & 0xFF),
             (unsigned long)(complete->new_version & 0xFF));

    int idx = find_node_index(complete->mac);
    if (idx >= 0) {
        s_job.nodes[idx].completed = true;
        s_job.nodes[idx].received_bytes = s_job.total_size;
        s_job.nodes_completed++;
    }

    // Check if all nodes completed
    if (s_job.nodes_completed + s_job.nodes_failed >= s_job.nodes_active) {
        if (s_job.nodes_failed == 0) {
            ESP_LOGI(TAG, "=== OTA JOB COMPLETE ===");
            s_job.state = OTA_STATE_COMPLETE;
            mqtt_publish_ota_complete(s_job.nodes_completed, 0, s_job.version);
        } else {
            ESP_LOGW(TAG, "=== OTA JOB FINISHED WITH FAILURES ===");
            s_job.state = OTA_STATE_COMPLETE;
            mqtt_publish_ota_complete(s_job.nodes_completed, s_job.nodes_failed, s_job.version);
        }
        cleanup_job();
    } else {
        // Publish progress
        mqtt_publish_ota_progress(s_job.nodes_completed, s_job.nodes_failed,
                                  s_job.nodes_active, "In progress");
    }
}

// ============================================================================
// Handle OTA Failed from Node
// ============================================================================

void ota_manager_handle_failed(const uint8_t* src_mac, const payload_ota_failed_t* failed)
{
    if (src_mac == NULL || failed == NULL) return;

    ESP_LOGW(TAG, "OTA FAILED from %02X:%02X:%02X:%02X:%02X:%02X (error=%d: %s)",
             failed->mac[0], failed->mac[1], failed->mac[2],
             failed->mac[3], failed->mac[4], failed->mac[5],
             failed->error_code, failed->error_msg);

    int idx = find_node_index(failed->mac);
    if (idx >= 0) {
        s_job.nodes[idx].failed = true;
        s_job.nodes[idx].error_code = failed->error_code;
        s_job.nodes_failed++;
    }

    // Check if all nodes done
    if (s_job.nodes_completed + s_job.nodes_failed >= s_job.nodes_active) {
        ESP_LOGW(TAG, "=== OTA JOB FINISHED WITH FAILURES ===");
        s_job.state = OTA_STATE_COMPLETE;
        mqtt_publish_ota_complete(s_job.nodes_completed, s_job.nodes_failed, s_job.version);
        cleanup_job();
    }
}

// ============================================================================
// Abort OTA
// ============================================================================

esp_err_t ota_manager_abort(void)
{
    if (s_job.state == OTA_STATE_IDLE) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Aborting OTA job");

    // Broadcast abort to all nodes
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_ABORT, ++s_seq, sizeof(payload_ota_abort_t));

    payload_ota_abort_t* payload = (payload_ota_abort_t*)msg.payload;
    payload->device_type = s_job.device_type;

    mesh_network_broadcast((uint8_t*)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_abort_t)));

    s_job.state = OTA_STATE_ABORTED;
    mqtt_publish_ota_progress(s_job.nodes_completed, s_job.nodes_failed,
                              s_job.nodes_active, "Aborted");
    cleanup_job();

    return ESP_OK;
}

// ============================================================================
// Timeout Check
// ============================================================================

void ota_manager_check_timeout(void)
{
    if (s_job.state == OTA_STATE_IDLE ||
        s_job.state == OTA_STATE_COMPLETE ||
        s_job.state == OTA_STATE_FAILED ||
        s_job.state == OTA_STATE_ABORTED) {
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;

    // Check overall timeout
    if ((now - s_job.start_time) > OTA_TIMEOUT_MS) {
        ESP_LOGE(TAG, "OTA job timeout after %lld ms", now - s_job.start_time);
        ota_manager_abort();
        return;
    }

    // Check inactivity timeout (no requests for 60 seconds)
    if ((now - s_job.last_activity) > 60000) {
        ESP_LOGW(TAG, "OTA inactivity timeout");
        // Re-broadcast availability to wake up stuck nodes
        if (s_job.state == OTA_STATE_DISTRIBUTING) {
            send_ota_available();
            s_job.last_activity = now;
        }
    }
}

// ============================================================================
// Getters
// ============================================================================

ota_state_t ota_manager_get_state(void)
{
    return s_job.state;
}

void ota_manager_get_progress(uint8_t* completed, uint8_t* failed, uint8_t* total)
{
    if (completed) *completed = s_job.nodes_completed;
    if (failed) *failed = s_job.nodes_failed;
    if (total) *total = s_job.nodes_active;
}

bool ota_manager_is_active(void)
{
    return (s_job.state != OTA_STATE_IDLE &&
            s_job.state != OTA_STATE_COMPLETE &&
            s_job.state != OTA_STATE_FAILED &&
            s_job.state != OTA_STATE_ABORTED);
}

const ota_job_t* ota_manager_get_job(void)
{
    return &s_job;
}

// ============================================================================
// Helper Functions
// ============================================================================

static void hex_to_bytes(const char* hex, uint8_t* bytes, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned int value;
        sscanf(hex + (i * 2), "%02x", &value);
        bytes[i] = (uint8_t)value;
    }
}

static uint32_t parse_version(const char* version)
{
    uint32_t major = 0, minor = 0, patch = 0;
    sscanf(version, "%lu.%lu.%lu", (unsigned long*)&major, (unsigned long*)&minor, (unsigned long*)&patch);
    return (major << 16) | (minor << 8) | patch;
}

static int find_node_index(const uint8_t* mac)
{
    for (int i = 0; i < s_job.nodes_active; i++) {
        if (memcmp(s_job.nodes[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void cleanup_job(void)
{
    // Free firmware buffer
    if (s_job.firmware_data != NULL) {
        free(s_job.firmware_data);
        s_job.firmware_data = NULL;
    }

    // Keep state and results for reporting
    // Reset will happen on next job start
}

// ============================================================================
// Gateway Self-OTA Implementation
// ============================================================================

static struct {
    bool active;
    esp_ota_handle_t handle;
    const esp_partition_t *update_partition;
    uint32_t total_size;
    uint32_t written_bytes;
    bool header_validated;
} s_gateway_ota = {0};

esp_err_t ota_gateway_begin(uint32_t total_size)
{
    if (s_gateway_ota.active) {
        ESP_LOGW(TAG, "Gateway OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "=== GATEWAY OTA BEGIN ===");
    ESP_LOGI(TAG, "Expected firmware size: %lu bytes", (unsigned long)total_size);

    // Get the next OTA partition
    s_gateway_ota.update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_gateway_ota.update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found!");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset: 0x%lx, size: %lu)",
             s_gateway_ota.update_partition->label,
             (unsigned long)s_gateway_ota.update_partition->address,
             (unsigned long)s_gateway_ota.update_partition->size);

    // Check if firmware fits
    if (total_size > s_gateway_ota.update_partition->size) {
        ESP_LOGE(TAG, "Firmware too large! %lu > %lu",
                 (unsigned long)total_size,
                 (unsigned long)s_gateway_ota.update_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Begin OTA update
    esp_err_t ret = esp_ota_begin(s_gateway_ota.update_partition, total_size, &s_gateway_ota.handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_gateway_ota.active = true;
    s_gateway_ota.total_size = total_size;
    s_gateway_ota.written_bytes = 0;
    s_gateway_ota.header_validated = false;

    ESP_LOGI(TAG, "Gateway OTA started successfully");
    return ESP_OK;
}

esp_err_t ota_gateway_write(const uint8_t *data, size_t length)
{
    if (!s_gateway_ota.active) {
        ESP_LOGE(TAG, "Gateway OTA not started");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate firmware header on first chunk
    if (!s_gateway_ota.header_validated && s_gateway_ota.written_bytes == 0) {
        if (length >= sizeof(esp_image_header_t)) {
            esp_image_header_t *header = (esp_image_header_t *)data;

            // Check magic byte
            if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "Invalid firmware header magic: 0x%02X (expected 0x%02X)",
                         header->magic, ESP_IMAGE_HEADER_MAGIC);
                ota_gateway_abort();
                return ESP_ERR_INVALID_VERSION;
            }

            ESP_LOGI(TAG, "Firmware header validated (magic=0x%02X)", header->magic);
            s_gateway_ota.header_validated = true;
        }
    }

    // Write data to flash
    esp_err_t ret = esp_ota_write(s_gateway_ota.handle, data, length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
        ota_gateway_abort();
        return ret;
    }

    s_gateway_ota.written_bytes += length;

    // Log progress every 10%
    int progress = (s_gateway_ota.written_bytes * 100) / s_gateway_ota.total_size;
    static int last_progress = -10;
    if (progress >= last_progress + 10) {
        ESP_LOGI(TAG, "Gateway OTA progress: %d%% (%lu/%lu bytes)",
                 progress,
                 (unsigned long)s_gateway_ota.written_bytes,
                 (unsigned long)s_gateway_ota.total_size);
        last_progress = progress;
    }

    return ESP_OK;
}

esp_err_t ota_gateway_end(void)
{
    if (!s_gateway_ota.active) {
        ESP_LOGE(TAG, "Gateway OTA not started");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "=== GATEWAY OTA END ===");
    ESP_LOGI(TAG, "Total written: %lu bytes", (unsigned long)s_gateway_ota.written_bytes);

    // Verify we received expected amount of data
    if (s_gateway_ota.written_bytes != s_gateway_ota.total_size) {
        ESP_LOGW(TAG, "Size mismatch: written=%lu, expected=%lu",
                 (unsigned long)s_gateway_ota.written_bytes,
                 (unsigned long)s_gateway_ota.total_size);
        // Continue anyway, esp_ota_end will validate
    }

    // Finalize OTA update
    esp_err_t ret = esp_ota_end(s_gateway_ota.handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        s_gateway_ota.active = false;
        return ret;
    }

    // Set the new partition as boot partition
    ret = esp_ota_set_boot_partition(s_gateway_ota.update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        s_gateway_ota.active = false;
        return ret;
    }

    ESP_LOGI(TAG, "=== GATEWAY OTA COMPLETE ===");
    ESP_LOGI(TAG, "New boot partition: %s", s_gateway_ota.update_partition->label);
    ESP_LOGI(TAG, "Reboot required to apply update");

    s_gateway_ota.active = false;
    return ESP_OK;
}

esp_err_t ota_gateway_abort(void)
{
    if (!s_gateway_ota.active) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Aborting gateway OTA");

    esp_ota_abort(s_gateway_ota.handle);
    s_gateway_ota.active = false;
    s_gateway_ota.written_bytes = 0;
    s_gateway_ota.total_size = 0;
    s_gateway_ota.header_validated = false;

    return ESP_OK;
}

void ota_gateway_get_progress(uint32_t *written_bytes, uint32_t *total_bytes, uint8_t *progress_percent)
{
    if (written_bytes) *written_bytes = s_gateway_ota.written_bytes;
    if (total_bytes) *total_bytes = s_gateway_ota.total_size;
    if (progress_percent) {
        if (s_gateway_ota.total_size > 0) {
            *progress_percent = (s_gateway_ota.written_bytes * 100) / s_gateway_ota.total_size;
        } else {
            *progress_percent = 0;
        }
    }
}

bool ota_gateway_is_active(void)
{
    return s_gateway_ota.active;
}
