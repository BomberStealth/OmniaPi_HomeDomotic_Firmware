/**
 * OmniaPi Gateway Mesh - MQTT Handler Implementation
 *
 * Handles MQTT communication with backend for commissioning,
 * device control, and status reporting.
 */

#include "mqtt_handler.h"
#include "commissioning.h"
#include "ota_manager.h"
#include "omniapi_protocol.h"
#include "config_manager.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "MQTT_HDL";

// ============================================================================
// State
// ============================================================================
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// Callbacks from main.c
extern void on_mqtt_connected(void);
extern void on_mqtt_disconnected(void);

// ============================================================================
// Forward Declarations
// ============================================================================
static void handle_scan_command(const char *data, int data_len);
static void handle_commission_command(const char *data, int data_len);
static void handle_decommission_command(const char *data, int data_len);
static void handle_credentials_command(const char *data, int data_len);
static void handle_identify_command(const char *data, int data_len);
static void handle_ota_start_command(const char *data, int data_len);
static void handle_ota_abort_command(const char *data, int data_len);
static bool parse_mac_address(const char *mac_str, uint8_t *mac_out);

// ============================================================================
// Event Handler
// ============================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            s_connected = true;
            on_mqtt_connected();

            // Subscribe to command topics
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD "/#", 1);
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_SCAN, 1);
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_COMMISSION, 1);
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_OTA_START, 1);
            esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_OTA_ABORT, 1);
            ESP_LOGI(TAG, "Subscribed to command topics");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected");
            s_connected = false;
            on_mqtt_disconnected();
            break;

        case MQTT_EVENT_DATA: {
            // Null-terminate topic for comparison
            char topic[128];
            int topic_len = (event->topic_len < 127) ? event->topic_len : 127;
            memcpy(topic, event->topic, topic_len);
            topic[topic_len] = '\0';

            ESP_LOGI(TAG, "MQTT Data: topic=%s", topic);

            // Route to appropriate handler
            if (strcmp(topic, MQTT_TOPIC_SCAN) == 0) {
                handle_scan_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_COMMISSION) == 0) {
                handle_commission_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_CMD "/credentials") == 0) {
                handle_credentials_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_CMD "/decommission") == 0) {
                handle_decommission_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_CMD "/identify") == 0) {
                handle_identify_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_OTA_START) == 0) {
                handle_ota_start_command(event->data, event->data_len);
            }
            else if (strcmp(topic, MQTT_TOPIC_OTA_ABORT) == 0) {
                handle_ota_abort_command(event->data, event->data_len);
            }
            else {
                ESP_LOGW(TAG, "Unknown topic: %s", topic);
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;

        default:
            break;
    }
}

// ============================================================================
// Initialization
// ============================================================================

esp_err_t mqtt_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT...");

    // Get MQTT config from config_manager (NVS with Kconfig fallback)
    const config_mqtt_t *mqtt_config = config_get_mqtt();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_config->broker_uri,
        .credentials.username = mqtt_config->username,
        .credentials.authentication.password = mqtt_config->password,
        .credentials.client_id = mqtt_config->client_id,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "MQTT initialized, broker: %s (configured: %s)",
             mqtt_config->broker_uri, mqtt_config->configured ? "YES" : "NO/defaults");
    return ESP_OK;
}

esp_err_t mqtt_handler_start(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_handler_stop(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_stop(s_client);
}

void mqtt_handler_process(void)
{
    // MQTT events are handled via callbacks
}

bool mqtt_handler_is_connected(void)
{
    return s_connected;
}

// ============================================================================
// Command Handlers
// ============================================================================

static void handle_scan_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "Scan command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        // Simple "start" command
        commissioning_start_scan();
        return;
    }

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (action && cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "start") == 0) {
            commissioning_start_scan();
        } else if (strcmp(action->valuestring, "stop") == 0) {
            commissioning_stop_scan();
        } else if (strcmp(action->valuestring, "results") == 0) {
            // Return current results immediately
            scan_result_t results[MAX_SCAN_RESULTS];
            int count = commissioning_get_scan_results(results, MAX_SCAN_RESULTS);
            mqtt_publish_scan_results(results, count);
        }
    } else {
        // Default: start scan
        commissioning_start_scan();
    }

    cJSON_Delete(json);
}

static void handle_commission_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "Commission command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse commission JSON");
        return;
    }

    cJSON *mac_json = cJSON_GetObjectItem(json, "mac");
    cJSON *name_json = cJSON_GetObjectItem(json, "name");

    if (!mac_json || !cJSON_IsString(mac_json)) {
        ESP_LOGE(TAG, "Missing 'mac' in commission command");
        cJSON_Delete(json);
        return;
    }

    uint8_t mac[6];
    if (!parse_mac_address(mac_json->valuestring, mac)) {
        ESP_LOGE(TAG, "Invalid MAC format: %s", mac_json->valuestring);
        cJSON_Delete(json);
        return;
    }

    const char *name = NULL;
    if (name_json && cJSON_IsString(name_json)) {
        name = name_json->valuestring;
    }

    commissioning_add_node(mac, name);

    cJSON_Delete(json);
}

static void handle_decommission_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "Decommission command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse decommission JSON");
        return;
    }

    cJSON *mac_json = cJSON_GetObjectItem(json, "mac");
    if (!mac_json || !cJSON_IsString(mac_json)) {
        ESP_LOGE(TAG, "Missing 'mac' in decommission command");
        cJSON_Delete(json);
        return;
    }

    uint8_t mac[6];
    if (!parse_mac_address(mac_json->valuestring, mac)) {
        ESP_LOGE(TAG, "Invalid MAC format: %s", mac_json->valuestring);
        cJSON_Delete(json);
        return;
    }

    commissioning_remove_node(mac);

    cJSON_Delete(json);
}

static void handle_credentials_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "Credentials command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse credentials JSON");
        return;
    }

    cJSON *network_id_json = cJSON_GetObjectItem(json, "network_id");
    cJSON *network_key_json = cJSON_GetObjectItem(json, "network_key");
    cJSON *plant_id_json = cJSON_GetObjectItem(json, "plant_id");

    if (!network_id_json || !cJSON_IsString(network_id_json) ||
        !network_key_json || !cJSON_IsString(network_key_json) ||
        !plant_id_json || !cJSON_IsString(plant_id_json)) {
        ESP_LOGE(TAG, "Missing fields in credentials command");
        cJSON_Delete(json);
        return;
    }

    // Parse network_id (format: "AA:BB:CC:DD:EE:FF" or "AABBCCDDEEFF")
    uint8_t network_id[6];
    if (!parse_mac_address(network_id_json->valuestring, network_id)) {
        ESP_LOGE(TAG, "Invalid network_id format");
        cJSON_Delete(json);
        return;
    }

    commissioning_set_credentials(network_id,
                                  network_key_json->valuestring,
                                  plant_id_json->valuestring);

    cJSON_Delete(json);
}

static void handle_identify_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "Identify command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse identify JSON");
        return;
    }

    cJSON *mac_json = cJSON_GetObjectItem(json, "mac");
    if (!mac_json || !cJSON_IsString(mac_json)) {
        ESP_LOGE(TAG, "Missing 'mac' in identify command");
        cJSON_Delete(json);
        return;
    }

    uint8_t mac[6];
    if (!parse_mac_address(mac_json->valuestring, mac)) {
        ESP_LOGE(TAG, "Invalid MAC format: %s", mac_json->valuestring);
        cJSON_Delete(json);
        return;
    }

    commissioning_identify_node(mac);

    cJSON_Delete(json);
}

// ============================================================================
// Helper Functions
// ============================================================================

static bool parse_mac_address(const char *mac_str, uint8_t *mac_out)
{
    if (mac_str == NULL || mac_out == NULL) return false;

    // Try format with colons: "AA:BB:CC:DD:EE:FF"
    if (strlen(mac_str) == 17) {
        if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &mac_out[0], &mac_out[1], &mac_out[2],
                   &mac_out[3], &mac_out[4], &mac_out[5]) == 6) {
            return true;
        }
    }

    // Try format without separators: "AABBCCDDEEFF"
    if (strlen(mac_str) == 12) {
        if (sscanf(mac_str, "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
                   &mac_out[0], &mac_out[1], &mac_out[2],
                   &mac_out[3], &mac_out[4], &mac_out[5]) == 6) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Generic Publishing
// ============================================================================

esp_err_t mqtt_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!s_connected || topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain ? 1 : 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

// ============================================================================
// Publishing Functions - Status
// ============================================================================

esp_err_t mqtt_publish_gateway_status(bool online)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"version\":\"%s\"}",
             online ? "online" : "offline",
             CONFIG_GATEWAY_FIRMWARE_VERSION);

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_STATE, payload, 0, 1, 1);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_node_connected(const uint8_t *mac)
{
    if (!s_connected || mac == NULL) return ESP_ERR_INVALID_STATE;

    char topic[64];
    char payload[64];

    snprintf(topic, sizeof(topic), MQTT_TOPIC_NODES "/%02X%02X%02X%02X%02X%02X/status",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(payload, sizeof(payload), "{\"status\":\"online\"}");

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_node_disconnected(const uint8_t *mac)
{
    if (!s_connected || mac == NULL) return ESP_ERR_INVALID_STATE;

    char topic[64];
    char payload[64];

    snprintf(topic, sizeof(topic), MQTT_TOPIC_NODES "/%02X%02X%02X%02X%02X%02X/status",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(payload, sizeof(payload), "{\"status\":\"offline\"}");

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_node_state(const uint8_t *mac, const char *state_json)
{
    if (!s_connected || mac == NULL || state_json == NULL) return ESP_ERR_INVALID_STATE;

    char topic[64];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_NODES "/%02X%02X%02X%02X%02X%02X/state",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int msg_id = esp_mqtt_client_publish(s_client, topic, state_json, 0, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

// ============================================================================
// Publishing Functions - Commissioning
// ============================================================================

esp_err_t mqtt_publish_scan_results(const scan_result_t *results, int count)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    cJSON *nodes = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        cJSON *node = cJSON_CreateObject();

        // Format MAC as string
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 results[i].mac[0], results[i].mac[1], results[i].mac[2],
                 results[i].mac[3], results[i].mac[4], results[i].mac[5]);

        cJSON_AddStringToObject(node, "mac", mac_str);
        cJSON_AddNumberToObject(node, "device_type", results[i].device_type);
        cJSON_AddStringToObject(node, "firmware", results[i].firmware_version);
        cJSON_AddNumberToObject(node, "rssi", results[i].rssi);
        cJSON_AddBoolToObject(node, "commissioned", results[i].commissioned != 0);

        cJSON_AddItemToArray(nodes, node);
    }

    cJSON_AddItemToObject(root, "nodes", nodes);
    cJSON_AddNumberToObject(root, "count", count);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/results", MQTT_TOPIC_SCAN);

    int msg_id = esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Published scan results (%d nodes)", count);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_commission_result(const uint8_t *mac, bool success, const char *message)
{
    if (!s_connected || mac == NULL) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddBoolToObject(root, "success", success);
    if (message) {
        cJSON_AddStringToObject(root, "message", message);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/result", MQTT_TOPIC_COMMISSION);

    int msg_id = esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Published commission result for %s: %s", mac_str, success ? "success" : "failed");
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_decommission_result(const uint8_t *mac, bool success, const char *message)
{
    if (!s_connected || mac == NULL) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddBoolToObject(root, "success", success);
    if (message) {
        cJSON_AddStringToObject(root, "message", message);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/decommission/result", MQTT_TOPIC_CMD);

    int msg_id = esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Published decommission result for %s: %s", mac_str, success ? "success" : "failed");
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

// ============================================================================
// OTA Command Handlers
// ============================================================================

static void handle_ota_start_command(const char *data, int data_len)
{
    ESP_LOGI(TAG, "OTA start command received");

    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse OTA start JSON");
        mqtt_publish_ota_progress(0, 1, 1, "Invalid JSON");
        return;
    }

    // Required fields
    cJSON *url_json = cJSON_GetObjectItem(json, "url");
    cJSON *version_json = cJSON_GetObjectItem(json, "version");
    cJSON *sha256_json = cJSON_GetObjectItem(json, "sha256");
    cJSON *size_json = cJSON_GetObjectItem(json, "size");
    cJSON *device_type_json = cJSON_GetObjectItem(json, "device_type");

    if (!url_json || !cJSON_IsString(url_json) ||
        !version_json || !cJSON_IsString(version_json) ||
        !sha256_json || !cJSON_IsString(sha256_json) ||
        !size_json || !cJSON_IsNumber(size_json) ||
        !device_type_json || !cJSON_IsNumber(device_type_json)) {
        ESP_LOGE(TAG, "Missing required fields in OTA start command");
        mqtt_publish_ota_progress(0, 1, 1, "Missing required fields");
        cJSON_Delete(json);
        return;
    }

    // Optional: target MACs
    uint8_t target_macs[OTA_MAX_TARGETS][6];
    uint8_t target_count = 0;

    cJSON *targets_json = cJSON_GetObjectItem(json, "targets");
    if (targets_json && cJSON_IsArray(targets_json)) {
        int array_size = cJSON_GetArraySize(targets_json);
        for (int i = 0; i < array_size && target_count < OTA_MAX_TARGETS; i++) {
            cJSON *mac_item = cJSON_GetArrayItem(targets_json, i);
            if (mac_item && cJSON_IsString(mac_item)) {
                if (parse_mac_address(mac_item->valuestring, target_macs[target_count])) {
                    target_count++;
                }
            }
        }
    }

    ESP_LOGI(TAG, "Starting OTA job: version=%s, size=%d, device_type=%d, targets=%d",
             version_json->valuestring, (int)size_json->valuedouble,
             (int)device_type_json->valuedouble, target_count);

    esp_err_t ret = ota_manager_start_job(
        url_json->valuestring,
        version_json->valuestring,
        sha256_json->valuestring,
        (uint32_t)size_json->valuedouble,
        (uint8_t)device_type_json->valuedouble,
        (target_count > 0) ? target_macs : NULL,
        target_count
    );

    if (ret == ESP_OK) {
        mqtt_publish_ota_progress(0, 0, 0, "Downloading firmware");
    } else {
        mqtt_publish_ota_progress(0, 1, 1, "Failed to start OTA job");
    }

    cJSON_Delete(json);
}

static void handle_ota_abort_command(const char *data, int data_len)
{
    ESP_LOGW(TAG, "OTA abort command received");
    ota_manager_abort();
}

// ============================================================================
// Publishing Functions - OTA
// ============================================================================

esp_err_t mqtt_publish_ota_progress(uint8_t completed, uint8_t failed, uint8_t total, const char *status)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "completed", completed);
    cJSON_AddNumberToObject(root, "failed", failed);
    cJSON_AddNumberToObject(root, "total", total);
    if (status) {
        cJSON_AddStringToObject(root, "status", status);
    }

    const ota_job_t *job = ota_manager_get_job();
    if (job) {
        cJSON_AddStringToObject(root, "version", job->version);
        cJSON_AddNumberToObject(root, "device_type", job->device_type);

        const char *state_str = "unknown";
        switch (job->state) {
            case OTA_STATE_IDLE: state_str = "idle"; break;
            case OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
            case OTA_STATE_READY: state_str = "ready"; break;
            case OTA_STATE_DISTRIBUTING: state_str = "distributing"; break;
            case OTA_STATE_COMPLETE: state_str = "complete"; break;
            case OTA_STATE_FAILED: state_str = "failed"; break;
            case OTA_STATE_ABORTED: state_str = "aborted"; break;
        }
        cJSON_AddStringToObject(root, "state", state_str);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_OTA_PROGRESS, json_str, 0, 1, 0);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Published OTA progress: completed=%d, failed=%d, total=%d", completed, failed, total);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_ota_complete(uint8_t completed, uint8_t failed, const char *version)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "completed", completed);
    cJSON_AddNumberToObject(root, "failed", failed);
    cJSON_AddBoolToObject(root, "success", failed == 0);
    if (version) {
        cJSON_AddStringToObject(root, "version", version);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_OTA_COMPLETE, json_str, 0, 1, 0);

    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Published OTA complete: completed=%d, failed=%d, version=%s",
             completed, failed, version ? version : "unknown");
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}
