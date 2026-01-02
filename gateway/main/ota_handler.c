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

// ============== Helper ==============
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
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

esp_err_t ota_handler_start_node_update(const uint8_t *mac)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    // Check if firmware file exists
    int fw_size = storage_get_file_size("/node_fw.bin");
    if (fw_size <= 0) {
        strcpy(s_status.status_message, "No firmware file");
        s_status.error = true;
        return ESP_ERR_NOT_FOUND;
    }

    // Open firmware file
    s_node_fw_file = fopen("/spiffs/node_fw.bin", "rb");
    if (s_node_fw_file == NULL) {
        strcpy(s_status.status_message, "Failed to open firmware");
        s_status.error = true;
        return ESP_FAIL;
    }

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

    // Send OTA_BEGIN message
    uint8_t msg[5];
    msg[0] = MSG_OTA_BEGIN;
    msg[1] = fw_size & 0xFF;
    msg[2] = (fw_size >> 8) & 0xFF;
    msg[3] = (fw_size >> 16) & 0xFF;
    msg[4] = (fw_size >> 24) & 0xFF;

    espnow_master_send_command(mac, 0, 0);  // This adds peer
    esp_now_send(mac, msg, 5);

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
        if (s_node_ota_retries > 3) {
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

        // Retry current chunk
        ESP_LOGW(TAG, "Node OTA retry %d", s_node_ota_retries);
        s_node_ota_last_ack = now;
        // Re-send would go here
    }
}
