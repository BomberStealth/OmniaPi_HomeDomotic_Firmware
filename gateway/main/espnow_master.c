/**
 * OmniaPi Gateway - ESP-NOW Master
 * Handles ESP-NOW communication with nodes
 */

#include "espnow_master.h"
#include "node_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_now.h"
#include "esp_log.h"

static const char *TAG = "espnow_master";

// Broadcast address
static const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Counters
static volatile uint32_t s_rx_count = 0;
static volatile uint32_t s_tx_count = 0;

// State change callback
static espnow_state_change_cb_t s_state_cb = NULL;

// ============== ESP-NOW Callbacks ==============
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len < 1 || data == NULL) return;

    s_rx_count++;

    const uint8_t *src_addr = recv_info->src_addr;
    int8_t rssi = recv_info->rx_ctrl->rssi;
    uint8_t msg_type = data[0];

    // Track node
    int node_idx = node_manager_find_or_add(src_addr, rssi);

    char mac_str[18];
    node_manager_mac_to_string(src_addr, mac_str);
    ESP_LOGD(TAG, "RX from %s type=0x%02X len=%d rssi=%d", mac_str, msg_type, len, rssi);

    switch (msg_type) {
        case MSG_HEARTBEAT_ACK:
            // Extract version from response
            if (node_idx >= 0 && len > 2) {
                char version[16] = {0};
                size_t copy_len = (len - 2 < 15) ? len - 2 : 15;
                memcpy(version, data + 2, copy_len);

                // Sanitize: remove non-printable characters
                for (int i = 0; i < 15 && version[i]; i++) {
                    if (version[i] < 32 || version[i] > 126) {
                        version[i] = '\0';
                        break;
                    }
                }
                node_manager_update_version(node_idx, version);
            }
            break;

        case MSG_COMMAND_ACK:
        case MSG_STATE:
            // Format: [msgType][channel][state]
            if (node_idx >= 0 && len >= 3) {
                uint8_t channel = data[1];
                uint8_t state = data[2];

                if (channel >= 1 && channel <= 2) {
                    node_manager_update_relay(node_idx, channel, state);

                    // Notify callback
                    if (s_state_cb != NULL) {
                        s_state_cb(node_idx, channel, state);
                    }
                }
            }
            break;

        case MSG_OTA_READY:
        case MSG_OTA_ACK:
        case MSG_OTA_DONE:
        case MSG_OTA_ERROR:
            // OTA messages handled by ota_handler
            ESP_LOGI(TAG, "OTA message 0x%02X from %s", msg_type, mac_str);
            break;

        default:
            ESP_LOGD(TAG, "Unknown message type 0x%02X from %s", msg_type, mac_str);
            break;
    }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;  // unused
    s_tx_count++;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW send failed");
    }
}

// ============== Public Functions ==============
esp_err_t espnow_master_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW Master");

    // ESP-NOW requires WiFi to be started
    // WiFi should be initialized and started before calling this

    return ESP_OK;
}

esp_err_t espnow_master_start(void)
{
    ESP_LOGI(TAG, "Starting ESP-NOW");

    // Initialize ESP-NOW
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);

    // Add broadcast peer
    esp_now_peer_info_t peer_info = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer_info.peer_addr, BROADCAST_ADDR, 6);

    ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ESP-NOW started successfully");
    return ESP_OK;
}

void espnow_master_send_heartbeat(void)
{
    uint8_t msg[1] = {MSG_HEARTBEAT};
    esp_err_t ret = esp_now_send(BROADCAST_ADDR, msg, sizeof(msg));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Heartbeat send failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t espnow_master_send_command(const uint8_t *mac, uint8_t channel, uint8_t action)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    // Add peer if not exists
    esp_now_peer_info_t peer_info = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer_info.peer_addr, mac, 6);
    esp_now_add_peer(&peer_info);  // Ignore if already exists

    // Send command
    uint8_t msg[3] = {MSG_COMMAND, channel, action};
    esp_err_t ret = esp_now_send(mac, msg, sizeof(msg));

    char mac_str[18];
    node_manager_mac_to_string(mac, mac_str);
    ESP_LOGI(TAG, "Sent command to %s: ch%d %s",
             mac_str, channel,
             action == CMD_ON ? "ON" : (action == CMD_OFF ? "OFF" : "TOGGLE"));

    return ret;
}

void espnow_master_register_state_cb(espnow_state_change_cb_t callback)
{
    s_state_cb = callback;
}

uint32_t espnow_master_get_rx_count(void)
{
    return s_rx_count;
}

uint32_t espnow_master_get_tx_count(void)
{
    return s_tx_count;
}
