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

static const char *TAG = "ESPNOW";

// Firmware version
#define FIRMWARE_VERSION "2.0.0"

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

    // Log received message
    ESP_LOGI(TAG, "RX: len=%d type=0x%02X from=" MACSTR,
             len, data[0], MAC2STR(recv_info->src_addr));

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
