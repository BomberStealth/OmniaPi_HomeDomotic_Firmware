/**
 * OmniaPi Gateway - Web Server
 * HTTP server with REST API
 */

#include "webserver.h"
#include "node_manager.h"
#include "espnow_master.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "ota_handler.h"
#include <string.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "webserver";

#define FIRMWARE_VERSION "1.4.0-idf"

// HTTP server handle
static httpd_handle_t s_server = NULL;

// Embedded index.html (from EMBED_FILES in CMakeLists.txt)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// ============== Helper ==============
static uint32_t get_uptime_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// ============== HTTP Handlers ==============

// GET / - Serve index.html
static esp_err_t root_handler(httpd_req_t *req)
{
    size_t html_len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, html_len);
    return ESP_OK;
}

// GET /api/status - Gateway status JSON
static esp_err_t api_status_handler(httpd_req_t *req)
{
    char ip_str[16] = "0.0.0.0";
    wifi_manager_get_ip(ip_str, sizeof(ip_str));

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "connected", wifi_manager_is_connected());
    cJSON_AddStringToObject(json, "ip", ip_str);
    cJSON_AddNumberToObject(json, "nodeCount", node_manager_get_count());
    cJSON_AddNumberToObject(json, "uptime", get_uptime_seconds());
    cJSON_AddNumberToObject(json, "received", espnow_master_get_rx_count());
    cJSON_AddNumberToObject(json, "sent", espnow_master_get_tx_count());
    cJSON_AddNumberToObject(json, "channel", wifi_manager_get_channel());
    cJSON_AddNumberToObject(json, "rssi", wifi_manager_get_rssi());
    cJSON_AddBoolToObject(json, "mqttConnected", mqtt_handler_is_connected());
    cJSON_AddStringToObject(json, "version", FIRMWARE_VERSION);

    char *response = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    free(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// GET /api/nodes - List all nodes
static esp_err_t api_nodes_handler(httpd_req_t *req)
{
    // Update online status before returning
    node_manager_check_online_status();

    char buffer[2048];
    node_manager_get_nodes_json(buffer, sizeof(buffer));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, strlen(buffer));
    return ESP_OK;
}

// POST /api/command - Send command to node
static esp_err_t api_command_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    ESP_LOGI(TAG, "Command request: %s", content);

    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    cJSON *response = cJSON_CreateObject();

    if (json) {
        // Try form data format first (from old Web UI)
        cJSON *mac_item = cJSON_GetObjectItem(json, "mac");
        cJSON *node_id_item = cJSON_GetObjectItem(json, "nodeId");
        cJSON *channel_item = cJSON_GetObjectItem(json, "channel");
        cJSON *action_item = cJSON_GetObjectItem(json, "action");

        uint8_t mac[6];
        bool mac_valid = false;

        // Get MAC from mac field or find by nodeId
        if (mac_item && cJSON_IsString(mac_item)) {
            mac_valid = node_manager_mac_from_string(mac_item->valuestring, mac);
        } else if (node_id_item && cJSON_IsNumber(node_id_item)) {
            int node_idx = node_id_item->valueint;
            node_info_t *node = node_manager_get_node(node_idx);
            if (node) {
                memcpy(mac, node->mac, 6);
                mac_valid = true;
            }
        }

        if (mac_valid && channel_item && action_item) {
            int channel = cJSON_IsNumber(channel_item) ? channel_item->valueint : 1;
            uint8_t action = CMD_TOGGLE;

            if (cJSON_IsNumber(action_item)) {
                action = action_item->valueint ? CMD_ON : CMD_OFF;
            } else if (cJSON_IsString(action_item)) {
                if (strcmp(action_item->valuestring, "on") == 0) action = CMD_ON;
                else if (strcmp(action_item->valuestring, "off") == 0) action = CMD_OFF;
                else if (strcmp(action_item->valuestring, "toggle") == 0) action = CMD_TOGGLE;
            }

            esp_err_t err = espnow_master_send_command(mac, channel, action);
            cJSON_AddBoolToObject(response, "success", err == ESP_OK);
        } else {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "error", "Missing or invalid parameters");
        }
        cJSON_Delete(json);
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Invalid JSON");
    }

    char *resp_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));

    free(resp_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /api/discover - Trigger node discovery
static esp_err_t api_discover_handler(httpd_req_t *req)
{
    espnow_master_send_heartbeat();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);

    char *resp_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));

    free(resp_str);
    cJSON_Delete(response);
    return ESP_OK;
}

// POST /update - Gateway OTA update
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update request, size: %d", req->content_len);

    char buf[1024];
    int received = 0;
    int total = req->content_len;
    bool is_first = true;
    esp_err_t err = ESP_OK;

    while (received < total) {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            err = ESP_FAIL;
            break;
        }

        bool is_last = (received + ret >= total);
        err = ota_handler_gateway_update((uint8_t *)buf, ret, is_first, is_last);
        if (err != ESP_OK) break;

        received += ret;
        is_first = false;
    }

    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "OK");
        ESP_LOGI(TAG, "OTA update complete, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed");
    }

    return err;
}

// GET /api/node-ota-status - Node OTA progress
static esp_err_t api_node_ota_status_handler(httpd_req_t *req)
{
    const ota_status_t *status = ota_handler_get_status();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "inProgress", status->in_progress);
    cJSON_AddNumberToObject(json, "progress", status->progress_percent);
    cJSON_AddStringToObject(json, "status", status->status_message);
    cJSON_AddBoolToObject(json, "success", status->success);
    cJSON_AddBoolToObject(json, "error", status->error);

    char *response = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    free(response);
    cJSON_Delete(json);
    return ESP_OK;
}

// ============== URI Definitions ==============
static const httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
};

static const httpd_uri_t uri_api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
};

static const httpd_uri_t uri_api_nodes = {
    .uri = "/api/nodes",
    .method = HTTP_GET,
    .handler = api_nodes_handler,
};

static const httpd_uri_t uri_api_command = {
    .uri = "/api/command",
    .method = HTTP_POST,
    .handler = api_command_handler,
};

static const httpd_uri_t uri_api_discover = {
    .uri = "/api/discover",
    .method = HTTP_POST,
    .handler = api_discover_handler,
};

static const httpd_uri_t uri_update = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = ota_update_handler,
};

static const httpd_uri_t uri_node_ota_status = {
    .uri = "/api/node-ota-status",
    .method = HTTP_GET,
    .handler = api_node_ota_status_handler,
};

// ============== Public Functions ==============
esp_err_t webserver_init(void)
{
    ESP_LOGI(TAG, "Starting HTTP server");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_api_status);
    httpd_register_uri_handler(s_server, &uri_api_nodes);
    httpd_register_uri_handler(s_server, &uri_api_command);
    httpd_register_uri_handler(s_server, &uri_api_discover);
    httpd_register_uri_handler(s_server, &uri_update);
    httpd_register_uri_handler(s_server, &uri_node_ota_status);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

esp_err_t webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
