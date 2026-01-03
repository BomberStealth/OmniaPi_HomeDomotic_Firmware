#include "espnow_handler.h"
#include "relay_control.h"
#include "led_status.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "ESPNOW";

// OTA state
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = NULL;
static uint32_t s_ota_total_size = 0;
static uint32_t s_ota_received = 0;
static bool s_ota_in_progress = false;

// Firmware version
#define FIRMWARE_VERSION "2.5.0"

// Gateway MAC address (E8:9F:6D:BB:F8:F8)
static const uint8_t GATEWAY_MAC[] = {0xe8, 0x9f, 0x6d, 0xbb, 0xf8, 0xf8};

static bool s_gateway_known = false;
static uint32_t s_last_heartbeat = 0;

// Send heartbeat ACK: [0x02][node_id][version_string...]
static void send_heartbeat_ack(void) {
    // Format: [MSG_HEARTBEAT_ACK][node_id][version...]
    // Gateway expects len > 2, version at bytes 2+
    uint8_t response[12] = {0};
    response[0] = MSG_HEARTBEAT_ACK;
    response[1] = 0x01;  // Node ID

    // Copy version string starting at byte 2
    const char* ver = FIRMWARE_VERSION;
    size_t ver_len = strlen(ver);
    if (ver_len > 9) ver_len = 9;
    memcpy(&response[2], ver, ver_len);

    esp_err_t result = esp_now_send(GATEWAY_MAC, response, 2 + ver_len);
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "HEARTBEAT_ACK sent, ver=%s", ver);
    } else {
        ESP_LOGW(TAG, "HEARTBEAT_ACK failed: %s", esp_err_to_name(result));
    }
}

// Send command ACK: [0x21][channel][state]
static void send_command_ack(uint8_t channel, uint8_t state) {
    uint8_t response[3] = {
        MSG_COMMAND_ACK,
        channel,
        state
    };

    esp_err_t result = esp_now_send(GATEWAY_MAC, response, 3);
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "COMMAND_ACK sent: ch=%d state=%d", channel, state);
    } else {
        ESP_LOGW(TAG, "COMMAND_ACK failed: %s", esp_err_to_name(result));
    }
}

// ============== OTA Functions ==============

// Send OTA response message
static void send_ota_response(uint8_t msg_type, uint32_t chunk_num) {
    uint8_t response[5] = {msg_type};
    response[1] = chunk_num & 0xFF;
    response[2] = (chunk_num >> 8) & 0xFF;
    response[3] = (chunk_num >> 16) & 0xFF;
    response[4] = (chunk_num >> 24) & 0xFF;
    esp_now_send(GATEWAY_MAC, response, 5);
}

// Handle OTA BEGIN: [0x10][size_4bytes]
static void handle_ota_begin(const uint8_t *data, int len) {
    ESP_LOGI(TAG, "=== handle_ota_begin CALLED ===");
    ESP_LOGI(TAG, "Data length: %d", len);

    if (len < 5) {
        ESP_LOGE(TAG, "OTA BEGIN: invalid length %d (need 5)", len);
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    s_ota_total_size = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    ESP_LOGI(TAG, ">>> OTA BEGIN: firmware size=%lu bytes <<<", (unsigned long)s_ota_total_size);

    // Get next OTA partition
    s_ota_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    // Start OTA
    esp_err_t err = esp_ota_begin(s_ota_partition, s_ota_total_size, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    s_ota_received = 0;
    s_ota_in_progress = true;

    ESP_LOGI(TAG, "OTA started, partition: %s", s_ota_partition->label);
    send_ota_response(MSG_OTA_READY, 0);
}

// Handle OTA DATA: [0x12][chunk_num_4bytes][data...]
static void handle_ota_data(const uint8_t *data, int len) {
    if (!s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA DATA received but no OTA in progress");
        return;
    }

    if (len < 6) {
        ESP_LOGE(TAG, "OTA DATA: invalid length %d", len);
        return;
    }

    uint32_t chunk_num = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    const uint8_t *chunk_data = data + 5;
    size_t chunk_len = len - 5;

    esp_err_t err = esp_ota_write(s_ota_handle, chunk_data, chunk_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        esp_ota_abort(s_ota_handle);
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, chunk_num);
        return;
    }

    s_ota_received += chunk_len;

    // Log progress every 10%
    if (s_ota_total_size > 0) {
        int progress = (s_ota_received * 100) / s_ota_total_size;
        static int last_progress = -10;
        if (progress >= last_progress + 10) {
            ESP_LOGI(TAG, "OTA progress: %d%% (%lu/%lu)", progress,
                     (unsigned long)s_ota_received, (unsigned long)s_ota_total_size);
            last_progress = progress;
        }
    }

    send_ota_response(MSG_OTA_ACK, chunk_num);
}

// Handle OTA END: [0x14]
static void handle_ota_end(void) {
    if (!s_ota_in_progress) {
        ESP_LOGW(TAG, "OTA END received but no OTA in progress");
        return;
    }

    ESP_LOGI(TAG, "OTA END received, finalizing...");

    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    err = esp_ota_set_boot_partition(s_ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    s_ota_in_progress = false;
    ESP_LOGI(TAG, "OTA complete! Rebooting in 1 second...");

    send_ota_response(MSG_OTA_DONE, 0);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// ============== Command Functions ==============

// Handle relay command
static void handle_command(uint8_t channel, uint8_t action) {
    ESP_LOGI(TAG, "Command: ch=%d action=%d", channel, action);

    // Channel 1 or 2
    if (channel < 1 || channel > 2) {
        ESP_LOGW(TAG, "Invalid channel: %d", channel);
        return;
    }

    bool new_state = false;

    switch (action) {
        case CMD_ON:
            new_state = true;
            relay_set_channel(channel, true);
            break;
        case CMD_OFF:
            new_state = false;
            relay_set_channel(channel, false);
            break;
        case CMD_TOGGLE:
            new_state = !relay_get_channel_state(channel);
            relay_set_channel(channel, new_state);
            break;
        default:
            ESP_LOGW(TAG, "Unknown action: %d", action);
            return;
    }

    // Send ACK with new state
    send_command_ack(channel, new_state ? 1 : 0);
}

// ESP-NOW receive callback
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len < 1) return;

    // Log ALL received messages with detail
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP-NOW RX: len=%d, type=0x%02X", len, data[0]);
    ESP_LOGI(TAG, "From MAC: " MACSTR, MAC2STR(recv_info->src_addr));

    // Log first bytes for debugging
    if (len >= 5) {
        ESP_LOGI(TAG, "Data[0-4]: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
                 data[0], data[1], data[2], data[3], data[4]);
    }
    ESP_LOGI(TAG, "========================================");

    // Handle heartbeat (1 byte, type 0x01)
    if (len == 1 && data[0] == MSG_HEARTBEAT) {
        s_gateway_known = true;
        s_last_heartbeat = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Add Gateway as peer if not already (for sending response)
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, recv_info->src_addr, 6);
        peer_info.channel = 0;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        esp_now_add_peer(&peer_info);  // Ignore error if already exists

        send_heartbeat_ack();
        ESP_LOGI(TAG, "Heartbeat received, ACK sent");
        return;
    }

    // Handle command (3 bytes, type 0x20)
    if (len == 3 && data[0] == MSG_COMMAND) {
        uint8_t channel = data[1];
        uint8_t action = data[2];
        handle_command(channel, action);
        return;
    }

    // Handle OTA messages
    if (data[0] == MSG_OTA_BEGIN) {
        ESP_LOGI(TAG, ">>> MSG_OTA_BEGIN (0x%02X) detected! <<<", MSG_OTA_BEGIN);
        handle_ota_begin(data, len);
        return;
    }

    if (data[0] == MSG_OTA_DATA) {
        ESP_LOGD(TAG, "MSG_OTA_DATA received");
        handle_ota_data(data, len);
        return;
    }

    if (data[0] == MSG_OTA_END) {
        ESP_LOGI(TAG, ">>> MSG_OTA_END detected! <<<");
        handle_ota_end();
        return;
    }

    ESP_LOGW(TAG, "Unknown message: len=%d type=0x%02X", len, data[0]);
}

// ESP-NOW send callback (ESP-IDF v5.5 API)
static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    (void)tx_info;  // unused
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed");
    }
}

void espnow_handler_init(uint8_t wifi_channel) {
    ESP_LOGI(TAG, "Initializing ESP-NOW on channel %d", wifi_channel);

    // Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE));

    // Init ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    // Pre-add Gateway as peer
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, GATEWAY_MAC, 6);
    peer_info.channel = wifi_channel;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;

    esp_err_t add_result = esp_now_add_peer(&peer_info);
    if (add_result == ESP_OK) {
        ESP_LOGI(TAG, "Gateway peer added: " MACSTR, MAC2STR(GATEWAY_MAC));
    } else {
        ESP_LOGW(TAG, "Failed to add Gateway peer: %s", esp_err_to_name(add_result));
    }

    // Log own MAC
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "Node MAC: " MACSTR, MAC2STR(mac));
}

bool espnow_is_gateway_known(void) {
    return s_gateway_known;
}

uint32_t espnow_get_last_heartbeat_time(void) {
    return s_last_heartbeat;
}
