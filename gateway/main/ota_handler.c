/**
 * OmniaPi Gateway - OTA Handler
 * Gateway self-update and node firmware distribution
 */

#include "ota_handler.h"
#include "storage.h"
#include "espnow_master.h"
#include "node_manager.h"
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "ota_handler";

// Gateway OTA state
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;

// Node OTA state
static ota_status_t s_status = {0};
static uint8_t s_node_ota_target[6] = {0};
static FILE *s_node_fw_file = NULL;
static uint32_t s_node_fw_size = 0;
static uint32_t s_node_ota_chunk = 0;
static uint32_t s_node_ota_last_ack = 0;
static int s_node_ota_retries = 0;
static bool s_waiting_for_ready = false;
static uint32_t s_node_ota_sent = 0;

// ============== Helper ==============
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Send next chunk of firmware to node
static void send_next_chunk(void)
{
    if (s_node_fw_file == NULL || !s_status.in_progress) return;

    // Read chunk from file
    uint8_t msg[5 + OTA_CHUNK_SIZE];
    msg[0] = MSG_OTA_DATA;
    msg[1] = s_node_ota_chunk & 0xFF;
    msg[2] = (s_node_ota_chunk >> 8) & 0xFF;
    msg[3] = (s_node_ota_chunk >> 16) & 0xFF;
    msg[4] = (s_node_ota_chunk >> 24) & 0xFF;

    size_t read_len = fread(msg + 5, 1, OTA_CHUNK_SIZE, s_node_fw_file);
    if (read_len == 0) {
        // End of file, send OTA_END
        ESP_LOGI(TAG, "All chunks sent, sending OTA_END");
        uint8_t end_msg[1] = {MSG_OTA_END};
        esp_now_send(s_node_ota_target, end_msg, 1);
        strcpy(s_status.status_message, "Finalizing...");
        return;
    }

    esp_err_t err = esp_now_send(s_node_ota_target, msg, 5 + read_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send chunk %lu", (unsigned long)s_node_ota_chunk);
    } else {
        ESP_LOGD(TAG, "Sent chunk %lu (%d bytes)", (unsigned long)s_node_ota_chunk, read_len);
    }

    s_node_ota_last_ack = get_time_ms();
}

// ============== Public Functions ==============
esp_err_t ota_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA Handler");
    memset(&s_status, 0, sizeof(s_status));
    strcpy(s_status.status_message, "Idle");
    return ESP_OK;
}

esp_err_t ota_handler_gateway_update(const uint8_t *data, size_t len, bool is_first, bool is_last)
{
    esp_err_t err;

    if (is_first) {
        ESP_LOGI(TAG, "Starting gateway OTA update");

        s_update_partition = esp_ota_get_next_update_partition(NULL);
        if (s_update_partition == NULL) {
            ESP_LOGE(TAG, "No OTA partition found");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Writing to partition: %s", s_update_partition->label);

        err = esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        esp_ota_abort(s_ota_handle);
        return err;
    }

    if (is_last) {
        err = esp_ota_end(s_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_ota_set_boot_partition(s_update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "Gateway OTA complete!");
    }

    return ESP_OK;
}

esp_err_t ota_handler_store_node_firmware(const uint8_t *data, size_t len)
{
    return storage_write_file("/node_fw.bin", data, len);
}

// Streaming write state
static void *s_stream_handle = NULL;

esp_err_t ota_handler_node_fw_begin(void)
{
    if (s_stream_handle != NULL) {
        storage_close_write(s_stream_handle);
        s_stream_handle = NULL;
    }

    s_stream_handle = storage_open_write("/node_fw.bin");
    if (s_stream_handle == NULL) {
        ESP_LOGE(TAG, "Failed to open node firmware file for writing");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started streaming node firmware write");
    return ESP_OK;
}

esp_err_t ota_handler_node_fw_write(const uint8_t *data, size_t len)
{
    if (s_stream_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return storage_write_chunk(s_stream_handle, data, len);
}

esp_err_t ota_handler_node_fw_end(size_t total_size)
{
    if (s_stream_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = storage_close_write(s_stream_handle);
    s_stream_handle = NULL;

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Node firmware stored: %d bytes", total_size);
    }

    return ret;
}

esp_err_t ota_handler_start_node_update(const uint8_t *mac)
{
    ESP_LOGI(TAG, "=== ota_handler_start_node_update CALLED ===");

    if (mac == NULL) {
        ESP_LOGE(TAG, "MAC is NULL!");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Target MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Check if firmware file exists
    int fw_size = storage_get_file_size("/node_fw.bin");
    ESP_LOGI(TAG, "Firmware file size: %d bytes", fw_size);

    if (fw_size <= 0) {
        ESP_LOGE(TAG, "No firmware file found on SPIFFS!");
        strcpy(s_status.status_message, "No firmware file");
        s_status.error = true;
        return ESP_ERR_NOT_FOUND;
    }

    // Open firmware file
    ESP_LOGI(TAG, "Opening /spiffs/node_fw.bin...");
    s_node_fw_file = fopen("/spiffs/node_fw.bin", "rb");
    if (s_node_fw_file == NULL) {
        ESP_LOGE(TAG, "Failed to open firmware file!");
        strcpy(s_status.status_message, "Failed to open firmware");
        s_status.error = true;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Firmware file opened successfully");

    // Initialize state
    memcpy(s_node_ota_target, mac, 6);
    s_node_fw_size = fw_size;
    s_node_ota_chunk = 0;
    s_node_ota_retries = 0;
    s_node_ota_last_ack = get_time_ms();

    s_status.in_progress = true;
    s_status.total_size = fw_size;
    s_status.sent_size = 0;
    s_status.progress_percent = 0;
    s_status.success = false;
    s_status.error = false;
    strcpy(s_status.status_message, "Starting OTA...");

    char mac_str[18];
    node_manager_mac_to_string(mac, mac_str);
    ESP_LOGI(TAG, "Starting node OTA: %s, size=%lu", mac_str, (unsigned long)fw_size);

    // Add peer for ESP-NOW
    esp_now_peer_info_t peer_info = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer_info.peer_addr, mac, 6);
    esp_err_t peer_err = esp_now_add_peer(&peer_info);
    ESP_LOGI(TAG, "esp_now_add_peer result: %s", esp_err_to_name(peer_err));

    // Send OTA_BEGIN message
    uint8_t msg[5];
    msg[0] = MSG_OTA_BEGIN;
    msg[1] = fw_size & 0xFF;
    msg[2] = (fw_size >> 8) & 0xFF;
    msg[3] = (fw_size >> 16) & 0xFF;
    msg[4] = (fw_size >> 24) & 0xFF;

    s_waiting_for_ready = true;
    s_node_ota_sent = 0;

    ESP_LOGI(TAG, ">>> SENDING MSG_OTA_BEGIN (0x%02X) to node, size=%d <<<", MSG_OTA_BEGIN, fw_size);
    esp_err_t send_err = esp_now_send(mac, msg, 5);
    ESP_LOGI(TAG, "esp_now_send result: %s", esp_err_to_name(send_err));

    strcpy(s_status.status_message, "Waiting for node...");

    return ESP_OK;
}

const ota_status_t *ota_handler_get_status(void)
{
    return &s_status;
}

void ota_handler_process(void)
{
    if (!s_status.in_progress) return;

    uint32_t now = get_time_ms();

    // Check for timeout
    if (now - s_node_ota_last_ack > 5000) {
        s_node_ota_retries++;
        if (s_node_ota_retries > 10) {
            ESP_LOGE(TAG, "Node OTA timeout");
            s_status.in_progress = false;
            s_status.error = true;
            strcpy(s_status.status_message, "Timeout error");
            if (s_node_fw_file) {
                fclose(s_node_fw_file);
                s_node_fw_file = NULL;
            }
            return;
        }

        // Retry current chunk or OTA_BEGIN
        ESP_LOGW(TAG, "Node OTA retry %d", s_node_ota_retries);
        if (s_waiting_for_ready) {
            // Resend OTA_BEGIN
            uint8_t msg[5];
            msg[0] = MSG_OTA_BEGIN;
            msg[1] = s_node_fw_size & 0xFF;
            msg[2] = (s_node_fw_size >> 8) & 0xFF;
            msg[3] = (s_node_fw_size >> 16) & 0xFF;
            msg[4] = (s_node_fw_size >> 24) & 0xFF;
            esp_now_send(s_node_ota_target, msg, 5);
        } else {
            // Resend current chunk - rewind file to current chunk position
            fseek(s_node_fw_file, s_node_ota_chunk * OTA_CHUNK_SIZE, SEEK_SET);
            send_next_chunk();
        }
        s_node_ota_last_ack = now;
    }
}

void ota_handler_on_node_message(const uint8_t *mac, uint8_t msg_type, const uint8_t *data, int len)
{
    if (!s_status.in_progress) return;

    // Verify it's from our target node
    if (memcmp(mac, s_node_ota_target, 6) != 0) {
        ESP_LOGW(TAG, "OTA message from unexpected node");
        return;
    }

    s_node_ota_last_ack = get_time_ms();
    s_node_ota_retries = 0;

    switch (msg_type) {
        case MSG_OTA_READY:
            ESP_LOGI(TAG, "Node ready for OTA");
            s_waiting_for_ready = false;
            strcpy(s_status.status_message, "Sending firmware...");
            // Start sending chunks
            send_next_chunk();
            break;

        case MSG_OTA_ACK: {
            // Extract chunk number from ACK
            uint32_t ack_chunk = 0;
            if (len >= 5) {
                ack_chunk = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
            }

            if (ack_chunk == s_node_ota_chunk) {
                s_node_ota_sent += OTA_CHUNK_SIZE;
                if (s_node_ota_sent > s_node_fw_size) {
                    s_node_ota_sent = s_node_fw_size;
                }
                s_status.sent_size = s_node_ota_sent;
                s_status.progress_percent = (s_node_ota_sent * 100) / s_node_fw_size;

                // Update status message
                snprintf(s_status.status_message, sizeof(s_status.status_message),
                         "Sending... %d%%", s_status.progress_percent);

                // Send next chunk
                s_node_ota_chunk++;
                send_next_chunk();
            } else {
                ESP_LOGW(TAG, "Unexpected ACK: got %lu, expected %lu",
                         (unsigned long)ack_chunk, (unsigned long)s_node_ota_chunk);
            }
            break;
        }

        case MSG_OTA_DONE:
            ESP_LOGI(TAG, "Node OTA complete!");
            s_status.in_progress = false;
            s_status.success = true;
            s_status.progress_percent = 100;
            strcpy(s_status.status_message, "OTA Complete!");
            if (s_node_fw_file) {
                fclose(s_node_fw_file);
                s_node_fw_file = NULL;
            }
            break;

        case MSG_OTA_ERROR:
            ESP_LOGE(TAG, "Node reported OTA error");
            s_status.in_progress = false;
            s_status.error = true;
            strcpy(s_status.status_message, "Node error");
            if (s_node_fw_file) {
                fclose(s_node_fw_file);
                s_node_fw_file = NULL;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown OTA message type: 0x%02X", msg_type);
            break;
    }
}
