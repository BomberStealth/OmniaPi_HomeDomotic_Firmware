/**
 * OmniaPi Gateway - MQTT Handler
 * Handles MQTT communication with Backend
 */

#include "mqtt_handler.h"
#include "node_manager.h"
#include "espnow_master.h"
#include "wifi_manager.h"
#include "eth_manager.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "mqtt_handler";

#define FIRMWARE_VERSION "1.7.0-idf"

// MQTT client handle
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_connected = false;

// ============== Helper Functions ==============
static uint32_t get_uptime_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// ============== MQTT Event Handler ==============
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_connected = true;

            // Subscribe to command topic
            esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_COMMAND, 0);
            ESP_LOGI(TAG, "Subscribed to: %s", MQTT_TOPIC_COMMAND);

            // Publish initial status
            mqtt_handler_publish_status();
            mqtt_handler_publish_all_nodes();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_connected = false;
            break;

        case MQTT_EVENT_DATA:
            // Handle incoming command
            ESP_LOGI(TAG, "MQTT data received on topic: %.*s",
                     event->topic_len, event->topic);

            // Check if it's a command
            if (strncmp(event->topic, MQTT_TOPIC_COMMAND, event->topic_len) == 0) {
                // Parse JSON payload
                char *payload = malloc(event->data_len + 1);
                if (payload) {
                    memcpy(payload, event->data, event->data_len);
                    payload[event->data_len] = '\0';

                    cJSON *json = cJSON_Parse(payload);
                    if (json) {
                        cJSON *node_mac = cJSON_GetObjectItem(json, "node_mac");
                        cJSON *channel = cJSON_GetObjectItem(json, "channel");
                        cJSON *action = cJSON_GetObjectItem(json, "action");

                        if (node_mac && action && cJSON_IsString(node_mac) && cJSON_IsString(action)) {
                            uint8_t mac[6];
                            if (node_manager_mac_from_string(node_mac->valuestring, mac)) {
                                int ch = channel ? channel->valueint : 1;
                                uint8_t act = CMD_TOGGLE;

                                if (strcmp(action->valuestring, "on") == 0) act = CMD_ON;
                                else if (strcmp(action->valuestring, "off") == 0) act = CMD_OFF;

                                ESP_LOGI(TAG, "MQTT command: %s ch%d -> %s",
                                         node_mac->valuestring, ch, action->valuestring);

                                espnow_master_send_command(mac, ch, act);
                            } else {
                                ESP_LOGW(TAG, "Invalid MAC in command: %s", node_mac->valuestring);
                            }
                        } else {
                            ESP_LOGW(TAG, "Missing fields in command JSON");
                        }
                        cJSON_Delete(json);
                    } else {
                        ESP_LOGW(TAG, "Failed to parse command JSON");
                    }
                    free(payload);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

// ============== LWT Message Buffer ==============
static char s_lwt_message[64] = {0};

// ============== Public Functions ==============
esp_err_t mqtt_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT Handler");

    // Get gateway MAC for LWT message
    char mac_str[18] = "00:00:00:00:00:00";
    eth_manager_get_mac(mac_str, sizeof(mac_str));

    // Build LWT message: {"mac":"XX:XX:XX:XX:XX:XX","offline":true}
    snprintf(s_lwt_message, sizeof(s_lwt_message),
             "{\"mac\":\"%s\",\"offline\":true}", mac_str);

    ESP_LOGI(TAG, "LWT configured: %s -> %s", MQTT_TOPIC_LWT, s_lwt_message);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
        .network.reconnect_timeout_ms = 5000,
        .buffer.size = 1024,
        // Session configuration
        .session.keepalive = 30,  // 30 seconds keep-alive for faster LWT detection
        // Last Will and Testament configuration
        .session.last_will = {
            .topic = MQTT_TOPIC_LWT,
            .msg = s_lwt_message,
            .msg_len = strlen(s_lwt_message),
            .qos = 1,
            .retain = false,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "MQTT Handler initialized (broker: %s)", MQTT_BROKER_URI);
    return ESP_OK;
}

esp_err_t mqtt_handler_start(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting MQTT client");
    return esp_mqtt_client_start(s_mqtt_client);
}

bool mqtt_handler_is_connected(void)
{
    return s_connected;
}

void mqtt_handler_publish_status(void)
{
    if (!s_connected || s_mqtt_client == NULL) return;

    char ip_str[16] = "0.0.0.0";
    wifi_manager_get_ip(ip_str, sizeof(ip_str));

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "online", true);
    cJSON_AddStringToObject(json, "ip", ip_str);
    cJSON_AddStringToObject(json, "version", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(json, "uptime", get_uptime_seconds());
    cJSON_AddNumberToObject(json, "nodes_count", node_manager_get_count());
    cJSON_AddNumberToObject(json, "wifi_channel", wifi_manager_get_channel());
    cJSON_AddNumberToObject(json, "rssi", wifi_manager_get_rssi());

    char *payload = cJSON_PrintUnformatted(json);
    if (payload) {
        esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_STATUS, payload, 0, 0, 1);
        ESP_LOGI(TAG, "Published gateway status");
        free(payload);
    }
    cJSON_Delete(json);
}

void mqtt_handler_publish_all_nodes(void)
{
    if (!s_connected || s_mqtt_client == NULL) return;

    char buffer[2048];
    node_manager_get_nodes_json(buffer, sizeof(buffer));

    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_NODES, buffer, 0, 0, 0);
    ESP_LOGI(TAG, "Published all nodes (%d)", node_manager_get_count());
}

void mqtt_handler_publish_node_state(int node_index)
{
    if (!s_connected || s_mqtt_client == NULL) return;

    node_info_t *node = node_manager_get_node(node_index);
    if (node == NULL) return;

    char mac_str[18];
    node_manager_mac_to_string(node->mac, mac_str);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mac", mac_str);
    cJSON_AddBoolToObject(json, "online", node->online);
    cJSON_AddNumberToObject(json, "rssi", node->rssi);
    cJSON_AddStringToObject(json, "version", node->version);
    cJSON_AddStringToObject(json, "relay1", node->relay_states[0] ? "on" : "off");
    cJSON_AddStringToObject(json, "relay2", node->relay_states[1] ? "on" : "off");

    char *payload = cJSON_PrintUnformatted(json);
    if (payload) {
        // Build topic: omniapi/gateway/node/XX:XX:XX:XX:XX:XX/state
        char topic[80];
        snprintf(topic, sizeof(topic), "%s%s/state", MQTT_TOPIC_NODE_PREFIX, mac_str);

        esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 0, 0);
        ESP_LOGI(TAG, "Published node state: %s", mac_str);
        free(payload);
    }
    cJSON_Delete(json);
}
