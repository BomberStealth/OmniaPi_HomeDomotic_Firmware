#include "espnow_handler.h"
#include "led_controller.h"
#include "effects.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "ESPNOW_LED";

// NVS namespace and key
#define NVS_NAMESPACE "espnow"
#define NVS_KEY_CHANNEL "channel"

// OTA state
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = NULL;
static uint32_t s_ota_total_size = 0;
static uint32_t s_ota_received = 0;
static bool s_ota_in_progress = false;

// Firmware version
#define FIRMWARE_VERSION "1.3.0"

// Gateway MAC address (E8:9F:6D:BB:F8:F8)
static const uint8_t GATEWAY_MAC[] = {0xe8, 0x9f, 0x6d, 0xbb, 0xf8, 0xf8};
// Broadcast for discovery
static const uint8_t BROADCAST_MAC[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static bool s_gateway_known = false;
static uint32_t s_last_heartbeat = 0;
static uint8_t s_current_channel = 0;

// Discovery state
static volatile bool s_discovery_received = false;
static volatile uint8_t s_discovered_channel = 0;

// ============== NVS Functions ==============

void espnow_save_channel(uint8_t channel) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_CHANNEL, channel);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Channel %d saved to NVS", channel);
    }
}

uint8_t espnow_load_channel(void) {
    nvs_handle_t handle;
    uint8_t channel = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_CHANNEL, &channel);
        nvs_close(handle);
        if (channel > 0 && channel <= 13) {
            ESP_LOGI(TAG, "Channel %d loaded from NVS", channel);
        } else {
            channel = 0;
        }
    }
    return channel;
}

// ============== Message Functions ==============

// Send heartbeat ACK: [0x02][device_type][version_string...]
static void send_heartbeat_ack(void) {
    uint8_t response[12] = {0};
    response[0] = MSG_HEARTBEAT_ACK;
    response[1] = DEVICE_TYPE_LED_STRIP;  // Device type = LED_STRIP (0x10)

    const char* ver = FIRMWARE_VERSION;
    size_t ver_len = strlen(ver);
    if (ver_len > 9) ver_len = 9;
    memcpy(&response[2], ver, ver_len);

    esp_err_t result = esp_now_send(GATEWAY_MAC, response, 2 + ver_len);
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "HEARTBEAT_ACK sent, type=LED_STRIP, ver=%s", ver);
    } else {
        ESP_LOGW(TAG, "HEARTBEAT_ACK failed: %s", esp_err_to_name(result));
    }
}

// Send LED state ACK: [0x41][power][r][g][b][brightness][effect][speed]
void espnow_send_led_state(void) {
    led_state_t* state = led_get_state();

    uint8_t response[8] = {
        MSG_LED_ACK,
        state->power ? 1 : 0,
        state->r,
        state->g,
        state->b,
        state->brightness,
        state->effect_id,
        state->effect_speed
    };

    esp_err_t result = esp_now_send(GATEWAY_MAC, response, sizeof(response));
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "LED_ACK sent: power=%d RGB=%d,%d,%d bright=%d effect=%d speed=%d",
                 state->power, state->r, state->g, state->b,
                 state->brightness, state->effect_id, state->effect_speed);
    } else {
        ESP_LOGW(TAG, "LED_ACK failed: %s", esp_err_to_name(result));
    }
}

// ============== OTA Functions ==============

static void send_ota_response(uint8_t msg_type, uint32_t chunk_num) {
    uint8_t response[5] = {msg_type};
    response[1] = chunk_num & 0xFF;
    response[2] = (chunk_num >> 8) & 0xFF;
    response[3] = (chunk_num >> 16) & 0xFF;
    response[4] = (chunk_num >> 24) & 0xFF;
    esp_now_send(GATEWAY_MAC, response, 5);
}

static void handle_ota_begin(const uint8_t *data, int len) {
    ESP_LOGI(TAG, "=== handle_ota_begin CALLED ===");

    if (len < 5) {
        ESP_LOGE(TAG, "OTA BEGIN: invalid length %d", len);
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    s_ota_total_size = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    ESP_LOGI(TAG, ">>> OTA BEGIN: size=%lu bytes <<<", (unsigned long)s_ota_total_size);

    s_ota_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition");
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

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

static void handle_ota_data(const uint8_t *data, int len) {
    if (!s_ota_in_progress) return;
    if (len < 6) return;

    uint32_t chunk_num = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    const uint8_t *chunk_data = data + 5;
    size_t chunk_len = len - 5;

    esp_err_t err = esp_ota_write(s_ota_handle, chunk_data, chunk_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed");
        esp_ota_abort(s_ota_handle);
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, chunk_num);
        return;
    }

    s_ota_received += chunk_len;

    if (s_ota_total_size > 0) {
        int progress = (s_ota_received * 100) / s_ota_total_size;
        static int last_progress = -10;
        if (progress >= last_progress + 10) {
            ESP_LOGI(TAG, "OTA: %d%%", progress);
            last_progress = progress;
        }
    }

    send_ota_response(MSG_OTA_ACK, chunk_num);
}

static void handle_ota_end(void) {
    if (!s_ota_in_progress) return;

    ESP_LOGI(TAG, "OTA END, finalizing...");

    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed");
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    err = esp_ota_set_boot_partition(s_ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        s_ota_in_progress = false;
        send_ota_response(MSG_OTA_ERROR, 0);
        return;
    }

    s_ota_in_progress = false;
    ESP_LOGI(TAG, "OTA complete! Rebooting...");
    send_ota_response(MSG_OTA_DONE, 0);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

// ============== LED Command Handler ==============

static void handle_led_command(const uint8_t *data, int len) {
    if (len < 2) return;

    uint8_t cmd = data[1];

    ESP_LOGI(TAG, "LED Command: 0x%02X, len=%d", cmd, len);

    switch (cmd) {
        case LED_CMD_ON:
            ESP_LOGI(TAG, "LED: ON");
            led_set_power_on();
            break;

        case LED_CMD_OFF:
            ESP_LOGI(TAG, "LED: OFF");
            led_set_power_off();
            break;

        case LED_CMD_SET_COLOR:
            if (len >= 5) {
                uint8_t r = data[2];
                uint8_t g = data[3];
                uint8_t b = data[4];
                ESP_LOGI(TAG, "LED: SET_COLOR RGB=%d,%d,%d", r, g, b);
                led_set_color(r, g, b);
            }
            break;

        case LED_CMD_SET_BRIGHT:
            if (len >= 3) {
                uint8_t bright = data[2];
                ESP_LOGI(TAG, "LED: SET_BRIGHTNESS %d", bright);
                led_set_brightness(bright);
            }
            break;

        case LED_CMD_SET_EFFECT:
            if (len >= 3) {
                uint8_t effect = data[2];
                ESP_LOGI(TAG, "LED: SET_EFFECT %d", effect);
                led_set_effect(effect);
            }
            break;

        case LED_CMD_SET_SPEED:
            if (len >= 3) {
                uint8_t speed = data[2];
                ESP_LOGI(TAG, "LED: SET_SPEED %d", speed);
                led_set_effect_speed(speed);
            }
            break;

        case LED_CMD_SET_NUM_LEDS:
            if (len >= 4) {
                uint16_t num = data[2] | (data[3] << 8);
                ESP_LOGI(TAG, "LED: SET_NUM_LEDS %d", num);
                if (led_set_num_leds(num)) {
                    ESP_LOGI(TAG, "LED: num_leds updated to %d", num);
                } else {
                    ESP_LOGW(TAG, "LED: failed to set num_leds %d", num);
                }
            }
            break;

        case LED_CMD_CUSTOM_EFFECT:
            // Format: [0x40][0x07][r1][g1][b1][r2][g2][b2][r3][g3][b3]
            if (len >= 11) {
                uint8_t r1 = data[2], g1 = data[3], b1 = data[4];
                uint8_t r2 = data[5], g2 = data[6], b2 = data[7];
                uint8_t r3 = data[8], g3 = data[9], b3 = data[10];
                ESP_LOGI(TAG, "LED: CUSTOM_EFFECT RGB1=%d,%d,%d RGB2=%d,%d,%d RGB3=%d,%d,%d",
                         r1, g1, b1, r2, g2, b2, r3, g3, b3);
                led_set_custom_effect(r1, g1, b1, r2, g2, b2, r3, g3, b3);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown LED command: 0x%02X", cmd);
            return;
    }

    // Send ACK with current state
    espnow_send_led_state();

    // Save state to NVS
    led_save_state();
}

// ============== ESP-NOW Callbacks ==============

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len < 1) return;

    uint8_t msg_type = data[0];

    // Handle discovery ACK (for channel scan)
    if (msg_type == MSG_DISCOVERY_ACK && len >= 2) {
        s_discovered_channel = data[1];
        s_discovery_received = true;
        ESP_LOGI(TAG, "DISCOVERY_ACK received! Channel=%d", s_discovered_channel);
        return;
    }

    // Handle heartbeat
    if (len == 1 && msg_type == MSG_HEARTBEAT) {
        s_gateway_known = true;
        s_last_heartbeat = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Add peer only if not exists
        if (!esp_now_is_peer_exist(recv_info->src_addr)) {
            esp_now_peer_info_t peer_info = {0};
            memcpy(peer_info.peer_addr, recv_info->src_addr, 6);
            peer_info.channel = 0;
            peer_info.ifidx = WIFI_IF_STA;
            peer_info.encrypt = false;
            esp_now_add_peer(&peer_info);
        }

        send_heartbeat_ack();
        return;
    }

    // Handle LED strip commands
    if (msg_type == MSG_LED_COMMAND) {
        handle_led_command(data, len);
        return;
    }

    // Handle OTA
    if (msg_type == MSG_OTA_BEGIN) {
        handle_ota_begin(data, len);
        return;
    }
    if (msg_type == MSG_OTA_DATA) {
        handle_ota_data(data, len);
        return;
    }
    if (msg_type == MSG_OTA_END) {
        handle_ota_end();
        return;
    }
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed");
    }
}

// ============== Channel Scan ==============

static bool try_channel(uint8_t channel) {
    ESP_LOGI(TAG, "Trying channel %d...", channel);

    // Set channel
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Reset discovery state
    s_discovery_received = false;
    s_discovered_channel = 0;

    // Send discovery broadcast
    uint8_t msg[1] = {MSG_DISCOVERY};
    esp_now_send(BROADCAST_MAC, msg, 1);

    // Wait for response (300ms)
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (s_discovery_received) {
            ESP_LOGI(TAG, "Gateway found on channel %d!", channel);
            return true;
        }
    }

    return false;
}

uint8_t espnow_channel_scan(void) {
    ESP_LOGI(TAG, "Starting channel scan...");

    // Init WiFi in STA mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // Init ESP-NOW
    esp_now_init();
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);

    // Add broadcast peer (if not exists)
    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, BROADCAST_MAC, 6);
        peer_info.channel = 0;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        esp_now_add_peer(&peer_info);
    }

    // Scan channels 1-13
    for (uint8_t ch = 1; ch <= 13; ch++) {
        if (try_channel(ch)) {
            // Found! Save and return
            espnow_save_channel(ch);
            s_current_channel = ch;

            // Add Gateway peer (if not exists)
            if (!esp_now_is_peer_exist(GATEWAY_MAC)) {
                esp_now_peer_info_t gw_peer = {0};
                memcpy(gw_peer.peer_addr, GATEWAY_MAC, 6);
                gw_peer.channel = ch;
                gw_peer.ifidx = WIFI_IF_STA;
                gw_peer.encrypt = false;
                esp_now_add_peer(&gw_peer);
            }

            return ch;
        }
    }

    ESP_LOGW(TAG, "Gateway not found on any channel!");
    return 0;
}

// ============== Init ==============

void espnow_handler_init(uint8_t wifi_channel) {
    ESP_LOGI(TAG, "Initializing ESP-NOW on channel %d", wifi_channel);
    s_current_channel = wifi_channel;

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

    // Add Gateway peer (if not exists)
    if (!esp_now_is_peer_exist(GATEWAY_MAC)) {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, GATEWAY_MAC, 6);
        peer_info.channel = wifi_channel;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        esp_now_add_peer(&peer_info);
    }

    // Add broadcast peer (if not exists)
    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t bcast_peer = {0};
        memcpy(bcast_peer.peer_addr, BROADCAST_MAC, 6);
        bcast_peer.channel = 0;
        bcast_peer.ifidx = WIFI_IF_STA;
        bcast_peer.encrypt = false;
        esp_now_add_peer(&bcast_peer);
    }

    // Log own MAC
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "LED Strip MAC: " MACSTR, MAC2STR(mac));
}

bool espnow_is_gateway_known(void) {
    return s_gateway_known;
}

uint32_t espnow_get_last_heartbeat_time(void) {
    return s_last_heartbeat;
}
