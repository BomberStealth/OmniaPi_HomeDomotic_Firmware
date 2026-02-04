/**
 * OmniaPi Gateway - Node OTA Manager
 *
 * Handles push-mode OTA updates to mesh nodes
 * Gateway uploads firmware via web UI, then pushes chunks to target node
 */

#include "node_ota.h"
#include "mesh_network.h"
#include "mqtt_handler.h"
#include "webserver.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_crc.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "NODE_OTA";

// ============================================================================
// Internal State
// ============================================================================

typedef struct {
    node_ota_state_t state;
    uint8_t target_mac[6];
    uint8_t *firmware_data;      // For buffered mode
    size_t firmware_size;
    uint32_t firmware_crc;
    uint16_t total_chunks;
    uint16_t current_chunk;
    uint8_t retry_count;
    int64_t last_activity;
    SemaphoreHandle_t mutex;
    // Streaming mode
    bool streaming_mode;
    bool node_ready;             // Node has ACKed OTA_BEGIN
    bool chunk_acked;            // Current chunk ACKed
    size_t bytes_written;        // Bytes written so far (for CRC calc)
    uint32_t running_crc;        // Running CRC calculation
} node_ota_ctx_t;

static node_ota_ctx_t s_ota_ctx = {
    .state = NODE_OTA_STATE_IDLE,
    .firmware_data = NULL,
    .firmware_size = 0,
    .current_chunk = 0,
    .retry_count = 0,
    .mutex = NULL,
    .streaming_mode = false,
    .node_ready = false,
    .chunk_acked = false
};

// ============================================================================
// Forward Declarations
// ============================================================================

static esp_err_t send_ota_begin(void);
static esp_err_t send_ota_chunk(uint16_t chunk_index);
static esp_err_t send_ota_end(void);
static esp_err_t send_ota_abort_msg(void);
static void cleanup_ota(void);
static void report_ota_status(const char *status, int progress);

// Flash-based OTA task handle (declared here for use in node_ota_handle_ack)
static TaskHandle_t s_ota_task_handle = NULL;

// ============================================================================
// Public Functions
// ============================================================================

esp_err_t node_ota_init(void)
{
    if (s_ota_ctx.mutex == NULL) {
        s_ota_ctx.mutex = xSemaphoreCreateMutex();
        if (s_ota_ctx.mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_ota_ctx.state = NODE_OTA_STATE_IDLE;
    s_ota_ctx.firmware_data = NULL;
    s_ota_ctx.firmware_size = 0;

    ESP_LOGI(TAG, "Node OTA manager initialized");
    return ESP_OK;
}

esp_err_t node_ota_start(const uint8_t *target_mac, const uint8_t *firmware, size_t size)
{
    if (target_mac == NULL || firmware == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_IDLE) {
        xSemaphoreGive(s_ota_ctx.mutex);
        ESP_LOGE(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // Allocate memory for firmware
    s_ota_ctx.firmware_data = malloc(size);
    if (s_ota_ctx.firmware_data == NULL) {
        xSemaphoreGive(s_ota_ctx.mutex);
        ESP_LOGE(TAG, "Failed to allocate firmware buffer (%u bytes)", (unsigned)size);
        return ESP_ERR_NO_MEM;
    }

    // Copy firmware data
    memcpy(s_ota_ctx.firmware_data, firmware, size);
    s_ota_ctx.firmware_size = size;
    memcpy(s_ota_ctx.target_mac, target_mac, 6);

    // Calculate CRC32
    s_ota_ctx.firmware_crc = esp_crc32_le(0, firmware, size);

    // Calculate total chunks
    s_ota_ctx.total_chunks = (size + NODE_OTA_CHUNK_SIZE - 1) / NODE_OTA_CHUNK_SIZE;
    s_ota_ctx.current_chunk = 0;
    s_ota_ctx.retry_count = 0;
    s_ota_ctx.last_activity = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Starting OTA to node " MACSTR ", size=%u, chunks=%u, crc=0x%08lx",
             MAC2STR(target_mac), (unsigned)size, s_ota_ctx.total_chunks,
             (unsigned long)s_ota_ctx.firmware_crc);

    // Send OTA_BEGIN
    s_ota_ctx.state = NODE_OTA_STATE_STARTING;
    esp_err_t ret = send_ota_begin();

    if (ret != ESP_OK) {
        cleanup_ota();
        xSemaphoreGive(s_ota_ctx.mutex);
        return ret;
    }

    report_ota_status("starting", 0);

    xSemaphoreGive(s_ota_ctx.mutex);
    return ESP_OK;
}

void node_ota_handle_ack(const uint8_t *src_mac, const payload_ota_ack_t *ack)
{
    if (src_mac == NULL || ack == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    // Verify it's from our target node
    if (memcmp(src_mac, s_ota_ctx.target_mac, 6) != 0) {
        ESP_LOGW(TAG, "ACK from unexpected node " MACSTR, MAC2STR(src_mac));
        xSemaphoreGive(s_ota_ctx.mutex);
        return;
    }

    s_ota_ctx.last_activity = esp_timer_get_time() / 1000;
    s_ota_ctx.retry_count = 0;

    ESP_LOGD(TAG, "Received ACK: chunk=%u, status=%u", ack->chunk_index, ack->status);

    switch (ack->status) {
        case OTA_ACK_READY:
            // Node is ready, start sending chunks
            if (s_ota_ctx.state == NODE_OTA_STATE_STARTING) {
                ESP_LOGI(TAG, "Node ready, starting chunk transfer");
                s_ota_ctx.state = NODE_OTA_STATE_SENDING;
                s_ota_ctx.current_chunk = 0;
                s_ota_ctx.node_ready = true;

                if (!s_ota_ctx.streaming_mode && s_ota_task_handle == NULL) {
                    // Buffered RAM mode only: send first chunk automatically
                    // Flash-based mode (s_ota_task_handle != NULL) handles chunks in background task
                    send_ota_chunk(0);
                }
                report_ota_status("sending", 0);
            }
            break;

        case OTA_ACK_OK:
            // Chunk received successfully
            if (s_ota_ctx.state == NODE_OTA_STATE_SENDING) {
                s_ota_ctx.current_chunk = ack->chunk_index + 1;
                s_ota_ctx.chunk_acked = true;
                int progress = (s_ota_ctx.current_chunk * 100) / s_ota_ctx.total_chunks;

                if (s_ota_ctx.streaming_mode || s_ota_task_handle != NULL) {
                    // Streaming mode or flash-based mode: just signal ACK
                    // Caller/background task handles next chunk
                    if (s_ota_ctx.current_chunk % 10 == 0) {
                        ESP_LOGI(TAG, "Progress: %d/%d chunks (%d%%)",
                                 s_ota_ctx.current_chunk, s_ota_ctx.total_chunks, progress);
                    }
                    report_ota_status("sending", progress);
                } else {
                    // Buffered RAM mode: send next chunk or finish
                    if (s_ota_ctx.current_chunk >= s_ota_ctx.total_chunks) {
                        ESP_LOGI(TAG, "All chunks sent, finalizing...");
                        s_ota_ctx.state = NODE_OTA_STATE_FINISHING;
                        send_ota_end();
                        report_ota_status("finalizing", 100);
                    } else {
                        if (s_ota_ctx.current_chunk % 10 == 0) {
                            ESP_LOGI(TAG, "Progress: %d/%d chunks (%d%%)",
                                     s_ota_ctx.current_chunk, s_ota_ctx.total_chunks, progress);
                        }
                        send_ota_chunk(s_ota_ctx.current_chunk);
                        report_ota_status("sending", progress);
                    }
                }
            }
            break;

        case OTA_ACK_CRC_ERROR:
            // CRC error, retry chunk
            ESP_LOGW(TAG, "CRC error on chunk %u, retrying", ack->chunk_index);
            s_ota_ctx.retry_count++;
            if (s_ota_ctx.retry_count >= NODE_OTA_MAX_RETRIES) {
                ESP_LOGE(TAG, "Max retries exceeded");
                s_ota_ctx.state = NODE_OTA_STATE_FAILED;
                send_ota_abort_msg();
                report_ota_status("failed", -1);
                cleanup_ota();
            } else if (s_ota_task_handle == NULL && !s_ota_ctx.streaming_mode) {
                // Buffered RAM mode only: retry chunk directly
                // Flash-based mode handles retries in background task (timeout will trigger retry)
                send_ota_chunk(ack->chunk_index);
            }
            break;

        case OTA_ACK_WRITE_ERROR:
        case OTA_ACK_ABORT:
            // Fatal error
            ESP_LOGE(TAG, "Node reported error: %u", ack->status);
            s_ota_ctx.state = NODE_OTA_STATE_FAILED;
            report_ota_status("failed", -1);
            cleanup_ota();
            break;

        default:
            ESP_LOGW(TAG, "Unknown ACK status: %u", ack->status);
            break;
    }

    xSemaphoreGive(s_ota_ctx.mutex);
}

void node_ota_handle_complete(const uint8_t *src_mac, const payload_ota_complete_t *complete)
{
    if (src_mac == NULL || complete == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (memcmp(src_mac, s_ota_ctx.target_mac, 6) != 0) {
        xSemaphoreGive(s_ota_ctx.mutex);
        return;
    }

    ESP_LOGI(TAG, "Node " MACSTR " reported OTA complete, new version: %lu.%lu.%lu",
             MAC2STR(src_mac),
             (unsigned long)(complete->new_version >> 16) & 0xFF,
             (unsigned long)(complete->new_version >> 8) & 0xFF,
             (unsigned long)complete->new_version & 0xFF);

    s_ota_ctx.state = NODE_OTA_STATE_COMPLETE;
    report_ota_status("complete", 100);
    cleanup_ota();

    xSemaphoreGive(s_ota_ctx.mutex);
}

void node_ota_handle_failed(const uint8_t *src_mac, const payload_ota_failed_t *failed)
{
    if (src_mac == NULL || failed == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (memcmp(src_mac, s_ota_ctx.target_mac, 6) != 0) {
        xSemaphoreGive(s_ota_ctx.mutex);
        return;
    }

    ESP_LOGE(TAG, "Node " MACSTR " reported OTA failed: code=%u, msg=%.*s",
             MAC2STR(src_mac), failed->error_code, 32, failed->error_msg);

    s_ota_ctx.state = NODE_OTA_STATE_FAILED;
    report_ota_status("failed", -1);
    cleanup_ota();

    xSemaphoreGive(s_ota_ctx.mutex);
}

esp_err_t node_ota_abort(void)
{
    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_ota_ctx.state == NODE_OTA_STATE_IDLE) {
        xSemaphoreGive(s_ota_ctx.mutex);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Aborting OTA to node " MACSTR, MAC2STR(s_ota_ctx.target_mac));

    send_ota_abort_msg();
    s_ota_ctx.state = NODE_OTA_STATE_ABORTED;
    report_ota_status("aborted", -1);
    cleanup_ota();

    xSemaphoreGive(s_ota_ctx.mutex);
    return ESP_OK;
}

void node_ota_check_timeout(void)
{
    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (s_ota_ctx.state == NODE_OTA_STATE_IDLE ||
        s_ota_ctx.state == NODE_OTA_STATE_COMPLETE ||
        s_ota_ctx.state == NODE_OTA_STATE_FAILED ||
        s_ota_ctx.state == NODE_OTA_STATE_ABORTED) {
        xSemaphoreGive(s_ota_ctx.mutex);
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;
    int64_t elapsed = now - s_ota_ctx.last_activity;

    if (elapsed > NODE_OTA_TIMEOUT_MS) {
        ESP_LOGE(TAG, "OTA timeout after %lld ms", elapsed);

        s_ota_ctx.retry_count++;
        if (s_ota_ctx.retry_count >= NODE_OTA_MAX_RETRIES) {
            ESP_LOGE(TAG, "Max retries exceeded, aborting");
            s_ota_ctx.state = NODE_OTA_STATE_FAILED;
            send_ota_abort_msg();
            report_ota_status("timeout", -1);
            cleanup_ota();
        } else {
            // Retry current operation
            s_ota_ctx.last_activity = now;
            switch (s_ota_ctx.state) {
                case NODE_OTA_STATE_STARTING:
                    ESP_LOGI(TAG, "Retrying OTA_BEGIN");
                    send_ota_begin();
                    break;
                case NODE_OTA_STATE_SENDING:
                    ESP_LOGI(TAG, "Retrying chunk %u", s_ota_ctx.current_chunk);
                    send_ota_chunk(s_ota_ctx.current_chunk);
                    break;
                case NODE_OTA_STATE_FINISHING:
                    ESP_LOGI(TAG, "Retrying OTA_END");
                    send_ota_end();
                    break;
                default:
                    break;
            }
        }
    }

    xSemaphoreGive(s_ota_ctx.mutex);
}

node_ota_state_t node_ota_get_state(void)
{
    return s_ota_ctx.state;
}

int node_ota_get_progress(void)
{
    if (s_ota_ctx.state == NODE_OTA_STATE_IDLE) {
        return 0;
    }
    if (s_ota_ctx.state == NODE_OTA_STATE_COMPLETE) {
        return 100;
    }
    if (s_ota_ctx.total_chunks == 0) {
        return 0;
    }
    return (s_ota_ctx.current_chunk * 100) / s_ota_ctx.total_chunks;
}

bool node_ota_is_active(void)
{
    node_ota_state_t state = s_ota_ctx.state;
    return (state == NODE_OTA_STATE_STARTING ||
            state == NODE_OTA_STATE_SENDING ||
            state == NODE_OTA_STATE_FINISHING);
}

esp_err_t node_ota_get_target_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!node_ota_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(mac, s_ota_ctx.target_mac, 6);
    return ESP_OK;
}

// ============================================================================
// Streaming Mode Functions
// ============================================================================

esp_err_t node_ota_start_stream(const uint8_t *target_mac, size_t total_size)
{
    if (target_mac == NULL || total_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_IDLE) {
        xSemaphoreGive(s_ota_ctx.mutex);
        ESP_LOGE(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize streaming mode
    s_ota_ctx.streaming_mode = true;
    s_ota_ctx.firmware_data = NULL;  // No buffer in streaming mode
    s_ota_ctx.firmware_size = total_size;
    memcpy(s_ota_ctx.target_mac, target_mac, 6);

    // We'll calculate CRC as we stream
    s_ota_ctx.running_crc = 0;
    s_ota_ctx.bytes_written = 0;
    s_ota_ctx.firmware_crc = 0;  // Will be set after all data received

    s_ota_ctx.total_chunks = (total_size + NODE_OTA_CHUNK_SIZE - 1) / NODE_OTA_CHUNK_SIZE;
    s_ota_ctx.current_chunk = 0;
    s_ota_ctx.retry_count = 0;
    s_ota_ctx.node_ready = false;
    s_ota_ctx.chunk_acked = false;
    s_ota_ctx.last_activity = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Starting STREAMING OTA to " MACSTR ", size=%u, chunks=%u",
             MAC2STR(target_mac), (unsigned)total_size, s_ota_ctx.total_chunks);

    // Send OTA_BEGIN (with CRC=0, node will ignore CRC check in streaming mode)
    s_ota_ctx.state = NODE_OTA_STATE_STARTING;
    esp_err_t ret = send_ota_begin();

    if (ret != ESP_OK) {
        s_ota_ctx.state = NODE_OTA_STATE_IDLE;
        s_ota_ctx.streaming_mode = false;
        xSemaphoreGive(s_ota_ctx.mutex);
        return ret;
    }

    report_ota_status("starting", 0);
    xSemaphoreGive(s_ota_ctx.mutex);
    return ESP_OK;
}

esp_err_t node_ota_wait_ack(uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time() / 1000;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;
        if ((now - start) > timeout_ms) {
            ESP_LOGW(TAG, "Wait ACK timeout after %lu ms", (unsigned long)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }

        // Check if we got ACK
        if (s_ota_ctx.state == NODE_OTA_STATE_SENDING && s_ota_ctx.node_ready) {
            // Node is ready after OTA_BEGIN
            return ESP_OK;
        }

        if (s_ota_ctx.chunk_acked) {
            s_ota_ctx.chunk_acked = false;
            return ESP_OK;
        }

        if (s_ota_ctx.state == NODE_OTA_STATE_FAILED ||
            s_ota_ctx.state == NODE_OTA_STATE_ABORTED) {
            return ESP_FAIL;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool node_ota_node_ready(void)
{
    return s_ota_ctx.node_ready && s_ota_ctx.state == NODE_OTA_STATE_SENDING;
}

esp_err_t node_ota_write_chunk(const uint8_t *data, size_t len, bool is_last)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ota_ctx.streaming_mode) {
        ESP_LOGE(TAG, "Not in streaming mode");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_SENDING) {
        ESP_LOGE(TAG, "Invalid state for write: %d", s_ota_ctx.state);
        return ESP_ERR_INVALID_STATE;
    }

    // Update running CRC
    s_ota_ctx.running_crc = esp_crc32_le(s_ota_ctx.running_crc, data, len);
    s_ota_ctx.bytes_written += len;

    // Build and send chunk message
    omniapi_message_t msg;
    size_t offset = (size_t)s_ota_ctx.current_chunk * NODE_OTA_CHUNK_SIZE;

    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_DATA, s_ota_ctx.current_chunk & 0xFF,
                        sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + len);

    payload_ota_data_t *payload = (payload_ota_data_t *)msg.payload;
    payload->offset = offset;
    payload->length = len;
    payload->last_chunk = is_last ? 1 : 0;
    memcpy(payload->data, data, len);

    ESP_LOGD(TAG, "Streaming chunk %u: offset=%u, len=%u, last=%d",
             s_ota_ctx.current_chunk, (unsigned)offset, (unsigned)len, is_last);

    s_ota_ctx.chunk_acked = false;
    s_ota_ctx.last_activity = esp_timer_get_time() / 1000;

    esp_err_t ret = mesh_network_send(s_ota_ctx.target_mac, (uint8_t *)&msg,
                                       OMNIAPI_MSG_SIZE(sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + len));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send chunk: %s", esp_err_to_name(ret));
        return ret;
    }

    // If last chunk, set final CRC
    if (is_last) {
        s_ota_ctx.firmware_crc = s_ota_ctx.running_crc;
        ESP_LOGI(TAG, "All chunks streamed, final CRC=0x%08lx", (unsigned long)s_ota_ctx.firmware_crc);
    }

    return ESP_OK;
}

esp_err_t node_ota_finish_stream(void)
{
    if (!s_ota_ctx.streaming_mode) {
        ESP_LOGE(TAG, "Not in streaming mode");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_SENDING) {
        ESP_LOGE(TAG, "Invalid state for finish: %d", s_ota_ctx.state);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Finishing streaming OTA, total bytes=%u, CRC=0x%08lx",
             (unsigned)s_ota_ctx.bytes_written, (unsigned long)s_ota_ctx.firmware_crc);

    s_ota_ctx.state = NODE_OTA_STATE_FINISHING;
    esp_err_t ret = send_ota_end();

    if (ret == ESP_OK) {
        report_ota_status("finalizing", 100);
    }

    return ret;
}

// ============================================================================
// Internal Functions
// ============================================================================

static esp_err_t send_ota_begin(void)
{
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_BEGIN, 0, sizeof(payload_ota_begin_t));

    payload_ota_begin_t *payload = (payload_ota_begin_t *)msg.payload;
    memcpy(payload->target_mac, s_ota_ctx.target_mac, 6);
    payload->total_size = s_ota_ctx.firmware_size;
    payload->chunk_size = NODE_OTA_CHUNK_SIZE;
    payload->total_chunks = s_ota_ctx.total_chunks;
    payload->firmware_crc = s_ota_ctx.firmware_crc;

    ESP_LOGI(TAG, "Sending OTA_BEGIN to " MACSTR ": size=%u, chunks=%u",
             MAC2STR(s_ota_ctx.target_mac), (unsigned)s_ota_ctx.firmware_size,
             s_ota_ctx.total_chunks);

    return mesh_network_send(s_ota_ctx.target_mac, (uint8_t *)&msg,
                            OMNIAPI_MSG_SIZE(sizeof(payload_ota_begin_t)));
}

// Disable array-bounds warning - payload_ota_data_t (187 bytes) fits in OMNIAPI_MAX_PAYLOAD (200 bytes)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

static esp_err_t send_ota_chunk(uint16_t chunk_index)
{
    if (chunk_index >= s_ota_ctx.total_chunks) {
        return ESP_ERR_INVALID_ARG;
    }

    omniapi_message_t msg;

    // Calculate chunk offset and size
    size_t offset = (size_t)chunk_index * NODE_OTA_CHUNK_SIZE;
    size_t remaining = s_ota_ctx.firmware_size - offset;
    uint16_t chunk_len = (remaining > NODE_OTA_CHUNK_SIZE) ? NODE_OTA_CHUNK_SIZE : remaining;

    // Build message
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_DATA, chunk_index & 0xFF,
                        sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + chunk_len);

    payload_ota_data_t *payload = (payload_ota_data_t *)msg.payload;
    payload->offset = offset;
    payload->length = chunk_len;
    payload->last_chunk = (chunk_index == s_ota_ctx.total_chunks - 1) ? 1 : 0;
    memcpy(payload->data, s_ota_ctx.firmware_data + offset, chunk_len);

    ESP_LOGD(TAG, "Sending chunk %u/%u: offset=%u, len=%u",
             chunk_index + 1, s_ota_ctx.total_chunks, (unsigned)offset, chunk_len);

    return mesh_network_send(s_ota_ctx.target_mac, (uint8_t *)&msg,
                            OMNIAPI_MSG_SIZE(sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + chunk_len));
}

#pragma GCC diagnostic pop

static esp_err_t send_ota_end(void)
{
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_END, 0, sizeof(payload_ota_end_t));

    payload_ota_end_t *payload = (payload_ota_end_t *)msg.payload;
    memcpy(payload->target_mac, s_ota_ctx.target_mac, 6);
    payload->total_chunks = s_ota_ctx.total_chunks;
    payload->firmware_crc = s_ota_ctx.firmware_crc;

    ESP_LOGI(TAG, "Sending OTA_END to " MACSTR, MAC2STR(s_ota_ctx.target_mac));

    return mesh_network_send(s_ota_ctx.target_mac, (uint8_t *)&msg,
                            OMNIAPI_MSG_SIZE(sizeof(payload_ota_end_t)));
}

static esp_err_t send_ota_abort_msg(void)
{
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_ABORT, 0, sizeof(payload_ota_abort_t));

    payload_ota_abort_t *payload = (payload_ota_abort_t *)msg.payload;
    payload->device_type = 0; // Target specific node via MAC

    ESP_LOGI(TAG, "Sending OTA_ABORT to " MACSTR, MAC2STR(s_ota_ctx.target_mac));

    return mesh_network_send(s_ota_ctx.target_mac, (uint8_t *)&msg,
                            OMNIAPI_MSG_SIZE(sizeof(payload_ota_abort_t)));
}

static void cleanup_ota(void)
{
    if (s_ota_ctx.firmware_data != NULL) {
        free(s_ota_ctx.firmware_data);
        s_ota_ctx.firmware_data = NULL;
    }
    s_ota_ctx.firmware_size = 0;
    s_ota_ctx.total_chunks = 0;
    s_ota_ctx.current_chunk = 0;
    s_ota_ctx.retry_count = 0;
    s_ota_ctx.streaming_mode = false;
    s_ota_ctx.node_ready = false;
    s_ota_ctx.chunk_acked = false;
    s_ota_ctx.bytes_written = 0;
    s_ota_ctx.running_crc = 0;

    // Keep state as is (COMPLETE, FAILED, ABORTED) for status reporting
    // Will be reset to IDLE on next node_ota_start()
}

static void report_ota_status(const char *status, int progress)
{
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(s_ota_ctx.target_mac));

    char json[256];
    snprintf(json, sizeof(json),
             "{\"node\":\"%s\",\"status\":\"%s\",\"progress\":%d}",
             mac_str, status, progress);

    mqtt_publish("omniapi/gateway/node_ota/status", json, 0, false);

    ESP_LOGI(TAG, "OTA status: node=%s, status=%s, progress=%d", mac_str, status, progress);
}

// ============================================================================
// Flash-Based Async OTA Implementation
// ============================================================================

// Flash staging state
typedef struct {
    bool active;
    const esp_partition_t *staging_partition;
    uint8_t target_mac[6];
    size_t total_size;
    size_t bytes_written;
    uint32_t crc;
} flash_staging_t;

static flash_staging_t s_flash_staging = {0};
static uint32_t s_last_erased_sector = 0xFFFFFFFF;

// Forward declaration
static void node_ota_background_task(void *param);

esp_err_t node_ota_flash_begin(const uint8_t *target_mac, size_t total_size)
{
    if (target_mac == NULL || total_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_flash_staging.active || node_ota_is_active()) {
        ESP_LOGE(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // Find staging partition (use inactive OTA partition)
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *staging = esp_ota_get_next_update_partition(running);

    if (staging == NULL) {
        ESP_LOGE(TAG, "No staging partition found");
        return ESP_ERR_NOT_FOUND;
    }

    // Check size
    if (total_size > staging->size) {
        ESP_LOGE(TAG, "Firmware too large for staging: %u > %lu",
                 (unsigned)total_size, (unsigned long)staging->size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Preparing staging partition %s for %lu bytes (erase during write)",
             staging->label, (unsigned long)total_size);

    // Reset sector tracker
    s_last_erased_sector = 0xFFFFFFFF;

    // Initialize staging state - NO upfront erase, will erase progressively
    s_flash_staging.staging_partition = staging;
    memcpy(s_flash_staging.target_mac, target_mac, 6);
    s_flash_staging.total_size = total_size;
    s_flash_staging.bytes_written = 0;
    s_flash_staging.crc = 0;
    s_flash_staging.active = true;

    ESP_LOGI(TAG, "Flash staging ready for " MACSTR ", size=%u",
             MAC2STR(target_mac), (unsigned)total_size);

    return ESP_OK;
}

esp_err_t node_ota_flash_write(const uint8_t *data, size_t len)
{
    if (!s_flash_staging.active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_flash_staging.bytes_written + len > s_flash_staging.total_size) {
        ESP_LOGE(TAG, "Write would exceed total size");
        return ESP_ERR_INVALID_SIZE;
    }

    // Erase sectors as needed (4KB sectors)
    size_t write_end = s_flash_staging.bytes_written + len;
    size_t sector_start = s_flash_staging.bytes_written / 4096;
    size_t sector_end = (write_end + 4095) / 4096;

    for (size_t sector = sector_start; sector < sector_end; sector++) {
        if (sector != s_last_erased_sector) {
            esp_err_t ret = esp_partition_erase_range(
                s_flash_staging.staging_partition,
                sector * 4096,
                4096
            );
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to erase sector %u: %s", (unsigned)sector, esp_err_to_name(ret));
                s_flash_staging.active = false;
                return ret;
            }
            s_last_erased_sector = sector;
        }
    }

    // Write to flash
    esp_err_t ret = esp_partition_write(s_flash_staging.staging_partition,
                                         s_flash_staging.bytes_written,
                                         data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash write failed: %s", esp_err_to_name(ret));
        s_flash_staging.active = false;
        return ret;
    }

    // Update CRC
    s_flash_staging.crc = esp_crc32_le(s_flash_staging.crc, data, len);
    s_flash_staging.bytes_written += len;

    return ESP_OK;
}

esp_err_t node_ota_flash_finish(void)
{
    if (!s_flash_staging.active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_flash_staging.bytes_written != s_flash_staging.total_size) {
        ESP_LOGE(TAG, "Incomplete upload: %u/%u bytes",
                 (unsigned)s_flash_staging.bytes_written,
                 (unsigned)s_flash_staging.total_size);
        s_flash_staging.active = false;
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Flash staging complete: %u bytes, CRC=0x%08lx",
             (unsigned)s_flash_staging.bytes_written,
             (unsigned long)s_flash_staging.crc);

    // Mark staging as done (but keep data for background task)
    s_flash_staging.active = false;

    // Start background task to send OTA to node
    if (s_ota_task_handle != NULL) {
        ESP_LOGW(TAG, "OTA task already running");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result = xTaskCreate(
        node_ota_background_task,
        "node_ota_task",
        4096,
        NULL,
        5,  // Priority
        &s_ota_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA background task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA background task started");
    return ESP_OK;
}

bool node_ota_flash_staging_active(void)
{
    return s_flash_staging.active;
}

/**
 * Background task that reads firmware from flash and sends to node
 */
static void node_ota_background_task(void *param)
{
    ESP_LOGI(TAG, "=== OTA Background Task Started ===");
    webserver_log("[OTA] Background task started");

    uint8_t target_mac[6];
    memcpy(target_mac, s_flash_staging.target_mac, 6);
    size_t total_size = s_flash_staging.total_size;
    uint32_t firmware_crc = s_flash_staging.crc;
    const esp_partition_t *partition = s_flash_staging.staging_partition;

    // Check if node is in routing table
    bool node_found = mesh_network_is_node_reachable(target_mac);
    ESP_LOGI(TAG, "Target node " MACSTR " reachable: %s",
             MAC2STR(target_mac), node_found ? "YES" : "NO");
    webserver_log("[OTA] Node " MACSTR " in routing table: %s",
                  MAC2STR(target_mac), node_found ? "YES" : "NO");

    if (!node_found) {
        ESP_LOGW(TAG, "Target node not in routing table, will try anyway...");
        webserver_log("[OTA] WARNING: Node not in routing table, trying anyway...");
    }

    // Initialize OTA context
    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        webserver_log("[OTA] ERROR: Failed to acquire mutex");
        goto task_exit;
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_IDLE) {
        ESP_LOGE(TAG, "OTA already in progress");
        webserver_log("[OTA] ERROR: OTA already in progress");
        xSemaphoreGive(s_ota_ctx.mutex);
        goto task_exit;
    }

    // Setup context
    memcpy(s_ota_ctx.target_mac, target_mac, 6);
    s_ota_ctx.firmware_size = total_size;
    s_ota_ctx.firmware_crc = firmware_crc;
    s_ota_ctx.total_chunks = (total_size + NODE_OTA_CHUNK_SIZE - 1) / NODE_OTA_CHUNK_SIZE;
    s_ota_ctx.current_chunk = 0;
    s_ota_ctx.retry_count = 0;
    s_ota_ctx.streaming_mode = false;
    s_ota_ctx.node_ready = false;
    s_ota_ctx.chunk_acked = false;
    s_ota_ctx.last_activity = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Starting OTA to " MACSTR ": size=%u, chunks=%u, CRC=0x%08lx",
             MAC2STR(target_mac), (unsigned)total_size,
             s_ota_ctx.total_chunks, (unsigned long)firmware_crc);
    webserver_log("[OTA] Starting to " MACSTR ", %u bytes, %u chunks",
                  MAC2STR(target_mac), (unsigned)total_size, s_ota_ctx.total_chunks);

    // Send OTA_BEGIN
    s_ota_ctx.state = NODE_OTA_STATE_STARTING;
    xSemaphoreGive(s_ota_ctx.mutex);

    report_ota_status("starting", 0);

    // Build and send OTA_BEGIN message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_BEGIN, 0, sizeof(payload_ota_begin_t));

    payload_ota_begin_t *begin = (payload_ota_begin_t *)msg.payload;
    memcpy(begin->target_mac, target_mac, 6);
    begin->total_size = total_size;
    begin->chunk_size = NODE_OTA_CHUNK_SIZE;
    begin->total_chunks = s_ota_ctx.total_chunks;
    begin->firmware_crc = firmware_crc;

    ESP_LOGI(TAG, "Sending OTA_BEGIN to " MACSTR " (msg_type=0x%02X, payload_len=%u)",
             MAC2STR(target_mac), msg.header.msg_type, msg.header.payload_len);
    webserver_log("[OTA] Sending OTA_BEGIN (msg_type=0x%02X)", msg.header.msg_type);

    esp_err_t send_ret = mesh_network_send(target_mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_begin_t)));
    if (send_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send OTA_BEGIN: %s", esp_err_to_name(send_ret));
        webserver_log("[OTA] ERROR: Failed to send OTA_BEGIN: %s", esp_err_to_name(send_ret));
        s_ota_ctx.state = NODE_OTA_STATE_FAILED;
        report_ota_status("failed", -1);
        goto task_exit;
    }
    ESP_LOGI(TAG, "OTA_BEGIN sent successfully, waiting for node ACK...");
    webserver_log("[OTA] OTA_BEGIN sent OK, waiting for node ACK (30s timeout)...");

    // Wait for node ready (OTA_ACK with READY status)
    int64_t wait_start = esp_timer_get_time() / 1000;
    while (!s_ota_ctx.node_ready && s_ota_ctx.state == NODE_OTA_STATE_STARTING) {
        vTaskDelay(pdMS_TO_TICKS(100));

        int64_t elapsed = (esp_timer_get_time() / 1000) - wait_start;

        // Log progress every 5 seconds
        if (((int64_t)elapsed % 5000) < 100) {
            ESP_LOGI(TAG, "Waiting for node ACK... %lld/30s", elapsed / 1000);
        }

        if (elapsed > 30000) {  // 30s timeout
            ESP_LOGE(TAG, "Node not ready timeout after 30s");
            webserver_log("[OTA] ERROR: Node did not respond to OTA_BEGIN (30s timeout)");
            s_ota_ctx.state = NODE_OTA_STATE_FAILED;
            report_ota_status("failed", -1);
            goto task_exit;
        }
    }

    if (s_ota_ctx.state != NODE_OTA_STATE_SENDING) {
        ESP_LOGE(TAG, "Failed to start OTA, state=%d", s_ota_ctx.state);
        goto task_exit;
    }

    ESP_LOGI(TAG, "Node ready, sending %u chunks...", s_ota_ctx.total_chunks);
    report_ota_status("sending", 0);

    // Read buffer for chunks
    uint8_t chunk_buf[NODE_OTA_CHUNK_SIZE];

    // Send all chunks
    for (uint16_t i = 0; i < s_ota_ctx.total_chunks; i++) {
        size_t offset = (size_t)i * NODE_OTA_CHUNK_SIZE;
        size_t remaining = total_size - offset;
        size_t chunk_len = (remaining > NODE_OTA_CHUNK_SIZE) ? NODE_OTA_CHUNK_SIZE : remaining;

        // Read chunk from flash
        esp_err_t ret = esp_partition_read(partition, offset, chunk_buf, chunk_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Flash read failed at offset %u: %s",
                     (unsigned)offset, esp_err_to_name(ret));
            s_ota_ctx.state = NODE_OTA_STATE_FAILED;
            report_ota_status("failed", -1);
            goto task_exit;
        }

        // Build chunk message
        OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_DATA, i & 0xFF,
                            sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + chunk_len);

        payload_ota_data_t *data = (payload_ota_data_t *)msg.payload;
        data->offset = offset;
        data->length = chunk_len;
        data->last_chunk = (i == s_ota_ctx.total_chunks - 1) ? 1 : 0;
        memcpy(data->data, chunk_buf, chunk_len);

        // Send with retry
        int retries = 0;
        s_ota_ctx.chunk_acked = false;

        while (retries < NODE_OTA_MAX_RETRIES) {
            mesh_network_send(target_mac, (uint8_t *)&msg,
                             OMNIAPI_MSG_SIZE(sizeof(payload_ota_data_t) - OTA_CHUNK_SIZE + chunk_len));

            // Wait for ACK (fast polling)
            wait_start = esp_timer_get_time() / 1000;
            while (!s_ota_ctx.chunk_acked) {
                vTaskDelay(pdMS_TO_TICKS(5));  // Fast 5ms polling

                int64_t elapsed = (esp_timer_get_time() / 1000) - wait_start;
                if (elapsed > 5000) {  // 5s per chunk timeout
                    break;
                }

                if (s_ota_ctx.state == NODE_OTA_STATE_FAILED) {
                    goto task_exit;
                }
            }

            if (s_ota_ctx.chunk_acked) {
                break;
            }

            retries++;
            ESP_LOGW(TAG, "Chunk %u ACK timeout, retry %d/%d", i, retries, NODE_OTA_MAX_RETRIES);
        }

        if (!s_ota_ctx.chunk_acked) {
            ESP_LOGE(TAG, "Chunk %u failed after %d retries", i, NODE_OTA_MAX_RETRIES);
            s_ota_ctx.state = NODE_OTA_STATE_FAILED;
            report_ota_status("failed", -1);
            goto task_exit;
        }

        s_ota_ctx.current_chunk = i + 1;

        // Report progress
        int progress = ((i + 1) * 100) / s_ota_ctx.total_chunks;
        if ((i + 1) % 50 == 0 || i == s_ota_ctx.total_chunks - 1) {
            ESP_LOGI(TAG, "Progress: %u/%u chunks (%d%%)", i + 1, s_ota_ctx.total_chunks, progress);
            report_ota_status("sending", progress);
        }

        // No delay - ACK-based flow control is sufficient
    }

    ESP_LOGI(TAG, "All chunks sent, sending OTA_END");
    s_ota_ctx.state = NODE_OTA_STATE_FINISHING;
    report_ota_status("finishing", 100);

    // Send OTA_END
    OMNIAPI_INIT_HEADER(&msg.header, MSG_OTA_END, 0, sizeof(payload_ota_end_t));

    payload_ota_end_t *end = (payload_ota_end_t *)msg.payload;
    memcpy(end->target_mac, target_mac, 6);
    end->total_chunks = s_ota_ctx.total_chunks;
    end->firmware_crc = firmware_crc;

    mesh_network_send(target_mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_ota_end_t)));

    // Wait for completion (node will reboot)
    ESP_LOGI(TAG, "Waiting for node to verify and reboot...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Check final state
    if (s_ota_ctx.state == NODE_OTA_STATE_COMPLETE) {
        ESP_LOGI(TAG, "=== OTA COMPLETE ===");
    } else if (s_ota_ctx.state == NODE_OTA_STATE_FINISHING) {
        // Node didn't respond with COMPLETE, but might have rebooted
        ESP_LOGI(TAG, "OTA finished, node may have rebooted");
        s_ota_ctx.state = NODE_OTA_STATE_COMPLETE;
        report_ota_status("complete", 100);
    }

task_exit:
    ESP_LOGI(TAG, "OTA background task exiting");

    // Cleanup
    if (xSemaphoreTake(s_ota_ctx.mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (s_ota_ctx.state != NODE_OTA_STATE_COMPLETE) {
            s_ota_ctx.state = NODE_OTA_STATE_IDLE;
        }
        xSemaphoreGive(s_ota_ctx.mutex);
    }

    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}
