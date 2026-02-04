/**
 * OmniaPi Node Mesh - Main Entry Point
 *
 * ESP-WIFI-MESH Node for ESP32-C3
 * Supports: Relay, LED Strip, Sensors
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"

#include "mesh_node.h"
#include "commissioning.h"
#include "nvs_storage.h"
#include "button_handler.h"
#include "ota_receiver.h"
#include "omniapi_protocol.h"
#include "status_led.h"

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
#include "device_relay.h"
#endif

#ifdef CONFIG_NODE_DEVICE_TYPE_LED
#include "device_led.h"
#endif

static const char *TAG = "NODE_MAIN";

// Node MAC address
static uint8_t s_node_mac[6] = {0};

// ============================================================================
// Command Handlers
// ============================================================================

static void handle_relay_command(const omniapi_message_t *msg)
{
#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    const payload_relay_cmd_t *cmd = (const payload_relay_cmd_t *)msg->payload;

    ESP_LOGI(TAG, "Relay command: ch=%d action=%d", cmd->channel, cmd->action);

    switch (cmd->action) {
        case RELAY_ACTION_OFF:
            device_relay_set(cmd->channel, false);
            break;
        case RELAY_ACTION_ON:
            device_relay_set(cmd->channel, true);
            break;
        case RELAY_ACTION_TOGGLE:
            device_relay_toggle(cmd->channel);
            break;
        default:
            ESP_LOGW(TAG, "Unknown relay action: %d", cmd->action);
            break;
    }

    // Send status update back to gateway
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_RELAY_STATUS, msg->header.seq, sizeof(payload_relay_status_t));

    payload_relay_status_t *status = (payload_relay_status_t *)response.payload;
    status->channel = cmd->channel;
    status->state = device_relay_get(cmd->channel) ? 1 : 0;

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_relay_status_t)));
#else
    ESP_LOGW(TAG, "Relay command received but device is not configured as relay");
#endif
}

static void handle_led_command(const omniapi_message_t *msg)
{
#ifdef CONFIG_NODE_DEVICE_TYPE_LED
    const payload_led_cmd_t *cmd = (const payload_led_cmd_t *)msg->payload;

    ESP_LOGI(TAG, "LED command: action=%d r=%d g=%d b=%d brightness=%d",
             cmd->action, cmd->r, cmd->g, cmd->b, cmd->brightness);

    switch (cmd->action) {
        case LED_ACTION_OFF:
            device_led_off();
            break;
        case LED_ACTION_ON:
            device_led_on();
            break;
        case LED_ACTION_SET_COLOR:
            device_led_set_color(cmd->r, cmd->g, cmd->b);
            break;
        case LED_ACTION_SET_BRIGHTNESS:
            device_led_set_brightness(cmd->brightness);
            break;
        case LED_ACTION_EFFECT:
            device_led_set_effect(cmd->effect_id, cmd->effect_speed);
            break;
        default:
            ESP_LOGW(TAG, "Unknown LED action: %d", cmd->action);
            break;
    }

    // Send status update back to gateway
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_LED_STATUS, msg->header.seq, sizeof(payload_led_status_t));

    payload_led_status_t *status = (payload_led_status_t *)response.payload;
    device_led_get_state(&status->on, &status->r, &status->g, &status->b, &status->brightness);
    status->effect_id = 0;

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_led_status_t)));
#else
    ESP_LOGW(TAG, "LED command received but device is not configured as LED");
#endif
}

static void handle_heartbeat(const omniapi_message_t *msg)
{
    ESP_LOGD(TAG, "Heartbeat from gateway, responding...");

    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_HEARTBEAT_ACK, msg->header.seq, sizeof(payload_heartbeat_ack_t));

    payload_heartbeat_ack_t *ack = (payload_heartbeat_ack_t *)response.payload;
    memcpy(ack->mac, s_node_mac, 6);

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    ack->device_type = DEVICE_TYPE_RELAY;
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    ack->device_type = DEVICE_TYPE_LED_STRIP;
#else
    ack->device_type = DEVICE_TYPE_SENSOR;
#endif

    ack->status = commissioning_is_commissioned() ? NODE_STATUS_ONLINE : NODE_STATUS_DISCOVERED;
    ack->mesh_layer = (uint8_t)mesh_node_get_layer();
    ack->rssi = mesh_node_get_parent_rssi();
    ack->firmware_version = (1 << 16) | (1 << 8) | 2;  // v1.1.2
    ack->uptime = esp_timer_get_time() / 1000000;  // seconds

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_heartbeat_ack_t)));
}

static void handle_identify(const omniapi_message_t *msg)
{
    ESP_LOGI(TAG, "Identify request received - blinking...");

#ifdef CONFIG_NODE_DEVICE_TYPE_LED
    // Blink LED strip
    for (int i = 0; i < 5; i++) {
        device_led_set_color(255, 255, 255);
        vTaskDelay(pdMS_TO_TICKS(200));
        device_led_off();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
#endif

    // TODO: Add onboard LED blink for other device types
}

static void handle_reboot(const omniapi_message_t *msg)
{
    ESP_LOGW(TAG, "Reboot command received, restarting in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void handle_factory_reset(const omniapi_message_t *msg)
{
    ESP_LOGW(TAG, "Factory reset command received!");
    commissioning_factory_reset();
}

static void handle_config_set(const omniapi_message_t *msg)
{
    const payload_config_set_t *cfg = (const payload_config_set_t *)msg->payload;

    ESP_LOGI(TAG, "Config set: key=%d value_len=%d", cfg->config_key, cfg->value_len);

    // Verify MAC matches us
    if (memcmp(cfg->mac, s_node_mac, 6) != 0) {
        ESP_LOGW(TAG, "Config not for us, ignoring");
        return;
    }

    uint8_t status = 0;  // Success

    switch (cfg->config_key) {
        case CONFIG_KEY_RELAY_MODE:
#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
            if (cfg->value_len >= 1) {
                uint8_t new_mode = cfg->value[0];
                esp_err_t ret = device_relay_set_mode(new_mode);
                if (ret != ESP_OK) {
                    status = 1;  // Error
                    ESP_LOGE(TAG, "Failed to set relay mode: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGI(TAG, "Relay mode set to: %s",
                             new_mode == RELAY_MODE_GPIO ? "GPIO" : "UART");
                }
            } else {
                status = 2;  // Invalid value
            }
#else
            ESP_LOGW(TAG, "Relay mode config but device is not relay type");
            status = 3;  // Not supported
#endif
            break;

        default:
            ESP_LOGW(TAG, "Unknown config key: %d", cfg->config_key);
            status = 4;  // Unknown key
            break;
    }

    // Send ACK
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_CONFIG_ACK, msg->header.seq, sizeof(payload_config_ack_t));

    payload_config_ack_t *ack = (payload_config_ack_t *)response.payload;
    memcpy(ack->mac, s_node_mac, 6);
    ack->config_key = cfg->config_key;
    ack->status = status;

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_config_ack_t)));
}

static void handle_config_get(const omniapi_message_t *msg)
{
    const payload_config_get_t *req = (const payload_config_get_t *)msg->payload;

    ESP_LOGI(TAG, "Config get: key=%d", req->config_key);

    // Verify MAC matches us
    if (memcmp(req->mac, s_node_mac, 6) != 0) {
        ESP_LOGW(TAG, "Config get not for us, ignoring");
        return;
    }

    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_CONFIG_RESPONSE, msg->header.seq, sizeof(payload_config_response_t));

    payload_config_response_t *resp = (payload_config_response_t *)response.payload;
    memcpy(resp->mac, s_node_mac, 6);
    resp->config_key = req->config_key;
    resp->value_len = 0;
    memset(resp->value, 0, sizeof(resp->value));

    switch (req->config_key) {
        case CONFIG_KEY_RELAY_MODE:
#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
            resp->value[0] = device_relay_get_mode();
            resp->value_len = 1;
#endif
            break;

        default:
            ESP_LOGW(TAG, "Unknown config key: %d", req->config_key);
            break;
    }

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_config_response_t)));
}

// ============================================================================
// Mesh Message Handler
// ============================================================================

static void mesh_rx_handler(const uint8_t *src_mac, const uint8_t *data, size_t len)
{
    if (len < sizeof(omniapi_header_t)) {
        ESP_LOGW(TAG, "Message too short: %d bytes", len);
        return;
    }

    const omniapi_message_t *msg = (const omniapi_message_t *)data;

    // Validate magic
    if (msg->header.magic != OMNIAPI_MAGIC) {
        ESP_LOGW(TAG, "Invalid magic: 0x%04X", msg->header.magic);
        return;
    }

    // Validate length
    if (len < OMNIAPI_MSG_SIZE(msg->header.payload_len)) {
        ESP_LOGW(TAG, "Payload truncated");
        return;
    }

    ESP_LOGD(TAG, "RX msg_type=0x%02X seq=%d len=%d",
             msg->header.msg_type, msg->header.seq, msg->header.payload_len);

    // Handle message based on type
    switch (msg->header.msg_type) {
        case MSG_HEARTBEAT:
            handle_heartbeat(msg);
            break;

        case MSG_RELAY_CMD:
            handle_relay_command(msg);
            break;

        case MSG_LED_CMD:
            handle_led_command(msg);
            break;

        case MSG_SCAN_REQUEST:
            commissioning_handle_scan_request(msg);
            break;

        case MSG_COMMISSION:
            commissioning_handle_commission(msg);
            break;

        case MSG_DECOMMISSION:
            commissioning_handle_decommission(msg);
            break;

        case MSG_IDENTIFY:
            handle_identify(msg);
            break;

        case MSG_REBOOT:
            handle_reboot(msg);
            break;

        case MSG_FACTORY_RESET:
            handle_factory_reset(msg);
            break;

        case MSG_OTA_AVAILABLE:
            ota_receiver_handle_available((const payload_ota_available_t *)msg->payload);
            break;

        case MSG_OTA_DATA:
            ota_receiver_handle_data((const payload_ota_data_t *)msg->payload);
            break;

        case MSG_OTA_ABORT:
            ota_receiver_handle_abort((const payload_ota_abort_t *)msg->payload);
            break;

        // Push-mode OTA (gateway pushes firmware to specific node)
        case MSG_OTA_BEGIN:
            ota_receiver_handle_begin((const payload_ota_begin_t *)msg->payload);
            break;

        case MSG_OTA_END:
            ota_receiver_handle_end((const payload_ota_end_t *)msg->payload);
            break;

        case MSG_CONFIG_SET:
            handle_config_set(msg);
            break;

        case MSG_CONFIG_GET:
            handle_config_get(msg);
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", msg->header.msg_type);
            break;
    }
}

// ============================================================================
// Callbacks
// ============================================================================

static void on_mesh_connected(void)
{
    ESP_LOGI(TAG, "Connected to mesh network!");
    status_led_set(STATUS_LED_CONNECTED);

    // Check if we just completed an OTA update
    if (ota_receiver_check_post_update()) {
        ESP_LOGI(TAG, "Post-OTA update check completed");
    }

    // Send initial status to gateway
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_NODE_ANNOUNCE, 0, sizeof(payload_node_announce_t));

    payload_node_announce_t *announce = (payload_node_announce_t *)msg.payload;
    memcpy(announce->mac, s_node_mac, 6);

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    announce->device_type = DEVICE_TYPE_RELAY;
    announce->capabilities = CONFIG_RELAY_COUNT;
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    announce->device_type = DEVICE_TYPE_LED_STRIP;
    announce->capabilities = CONFIG_LED_STRIP_COUNT;
#else
    announce->device_type = DEVICE_TYPE_SENSOR;
    announce->capabilities = 0;
#endif

    announce->firmware_version = (1 << 16) | (1 << 8) | 2;  // v1.1.2
    announce->commissioned = commissioning_is_commissioned() ? 1 : 0;

    mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_node_announce_t)));
}

static void on_mesh_disconnected(void)
{
    ESP_LOGW(TAG, "Disconnected from mesh network");
    status_led_set(STATUS_LED_SEARCHING);
}

static void on_button_short_press(void)
{
    ESP_LOGI(TAG, "Button short press - toggling device");

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    device_relay_toggle(0);  // Toggle first channel

    // Send status update
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_RELAY_STATUS, 0, sizeof(payload_relay_status_t));

    payload_relay_status_t *status = (payload_relay_status_t *)msg.payload;
    status->channel = 0;
    status->state = device_relay_get(0) ? 1 : 0;

    mesh_node_send_to_root((uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_relay_status_t)));
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    static bool led_on = false;
    led_on = !led_on;
    if (led_on) {
        device_led_on();
    } else {
        device_led_off();
    }
#endif
}

static void on_button_long_press(void)
{
    ESP_LOGW(TAG, "Button long press - FACTORY RESET!");
    commissioning_factory_reset();
}

// ============================================================================
// Main
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  OmniaPi Node Mesh v%s", CONFIG_NODE_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "===========================================");

    // Initialize status LED (starts with BOOT pattern)
    status_led_init();

    // Get MAC address
    esp_read_mac(s_node_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Node MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_node_mac[0], s_node_mac[1], s_node_mac[2],
             s_node_mac[3], s_node_mac[4], s_node_mac[5]);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS storage helper
    nvs_storage_init();

    // Initialize commissioning
    commissioning_init();

    // Initialize button handler
    button_handler_init();
    button_handler_set_short_press_cb(on_button_short_press);
    button_handler_set_long_press_cb(on_button_long_press);

    // Initialize device-specific hardware
#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    ESP_LOGI(TAG, "Device type: RELAY (%d channels)", CONFIG_RELAY_COUNT);
    device_relay_init();
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    ESP_LOGI(TAG, "Device type: LED STRIP (%d LEDs)", CONFIG_LED_STRIP_COUNT);
    device_led_init();
#else
    ESP_LOGI(TAG, "Device type: SENSOR");
#endif

    // Initialize OTA receiver
    ota_receiver_init();

    // Initialize mesh network
    mesh_node_set_connected_cb(on_mesh_connected);
    mesh_node_set_disconnected_cb(on_mesh_disconnected);
    mesh_node_set_rx_cb(mesh_rx_handler);

    // Set LED to searching before starting mesh
    status_led_set(STATUS_LED_SEARCHING);

    ESP_ERROR_CHECK(mesh_node_init());
    ESP_ERROR_CHECK(mesh_node_start());

    // Main loop - process incoming messages
    ESP_LOGI(TAG, "Node running, waiting for mesh connection...");

    while (1) {
        // Process received mesh messages
        mesh_node_process_rx();

        // Check OTA timeout
        ota_receiver_check_timeout();

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
