/**
 * OmniaPi Gateway Mesh - Web API Implementation
 */

#include "web_api.h"
#include "webserver.h"
#include "node_manager.h"
#include "commissioning.h"
#include "ota_manager.h"
#include "node_ota.h"
#include "mesh_network.h"
#include "mqtt_handler.h"
#include "config_manager.h"
#include "eth_manager.h"
#include "omniapi_protocol.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WEB_API";

// External references
extern void on_mqtt_connected(void);
extern void on_mqtt_disconnected(void);

// ============================================================================
// Helper: URL decode string in-place
// ============================================================================
static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // Decode %XX
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// ============================================================================
// Helper: Set CORS headers
// ============================================================================
static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ============================================================================
// Helper: Send JSON response
// ============================================================================
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON error");
        return ESP_FAIL;
    }

    esp_err_t ret = httpd_resp_send(req, json_str, strlen(json_str));
    cJSON_free(json_str);
    cJSON_Delete(json);
    return ret;
}

// ============================================================================
// Helper: Parse JSON body
// ============================================================================
static cJSON* parse_json_body(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 4096) {
        return NULL;
    }

    char *buf = malloc(content_len + 1);
    if (buf == NULL) {
        return NULL;
    }

    int received = httpd_req_recv(req, buf, content_len);
    if (received != content_len) {
        free(buf);
        return NULL;
    }
    buf[content_len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json;
}

// ============================================================================
// GET /api/status - Gateway status
// ============================================================================
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    cJSON_AddBoolToObject(json, "online", true);
    cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(json, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "heap_min", esp_get_minimum_free_heap_size());

    // Get MAC
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(json, "mac", mac_str);

    cJSON_AddStringToObject(json, "firmware", CONFIG_GATEWAY_FIRMWARE_VERSION);

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/network - Network info
// ============================================================================
static esp_err_t api_network_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    // WiFi info
    wifi_ap_record_t ap_info;
    bool wifi_conn = false;
    cJSON *wifi = cJSON_CreateObject();
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddStringToObject(wifi, "ssid", (char *)ap_info.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
        cJSON_AddNumberToObject(wifi, "channel", ap_info.primary);
        cJSON_AddBoolToObject(wifi, "connected", true);
        wifi_conn = true;
    } else {
        cJSON_AddBoolToObject(wifi, "connected", false);
    }
    cJSON_AddItemToObject(json, "wifi", wifi);

    // Ethernet info
    bool eth_conn = eth_manager_is_connected();
    cJSON *eth = cJSON_CreateObject();
    cJSON_AddBoolToObject(eth, "connected", eth_conn);
    char eth_ip[16];
    eth_manager_get_ip(eth_ip, sizeof(eth_ip));
    cJSON_AddStringToObject(eth, "ip", eth_ip);
    cJSON_AddItemToObject(json, "eth", eth);

    // Active route (ETH has priority)
    cJSON_AddStringToObject(json, "route", eth_conn ? "ETH" : (wifi_conn ? "WiFi" : "NONE"));

    // IP info (prefer ETH IP if connected, otherwise WiFi)
    if (eth_conn) {
        cJSON_AddStringToObject(json, "ip", eth_ip);
    } else {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            cJSON_AddStringToObject(json, "ip", ip_str);
        }
    }

    // MQTT status - using real connection state and config_manager
    const config_mqtt_t *mqtt_cfg = config_get_mqtt();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(mqtt, "broker", mqtt_cfg->broker_uri);
    cJSON_AddBoolToObject(mqtt, "connected", mqtt_handler_is_connected());
    cJSON_AddBoolToObject(mqtt, "configured", mqtt_cfg->configured);
    cJSON_AddItemToObject(json, "mqtt", mqtt);

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/mesh - Mesh info
// ============================================================================
static esp_err_t api_mesh_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    uint8_t mesh_id[6];
    mesh_network_get_id(mesh_id);
    char id_str[18];
    snprintf(id_str, sizeof(id_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mesh_id[0], mesh_id[1], mesh_id[2], mesh_id[3],
             mesh_id[4], mesh_id[5]);

    cJSON_AddStringToObject(json, "mesh_id", id_str);
    cJSON_AddNumberToObject(json, "channel", CONFIG_MESH_CHANNEL);
    cJSON_AddNumberToObject(json, "layer", mesh_network_get_layer());
    cJSON_AddBoolToObject(json, "is_root", mesh_network_is_root());
    cJSON_AddBoolToObject(json, "started", mesh_network_is_started());
    cJSON_AddNumberToObject(json, "node_count", mesh_network_get_node_count());

    mesh_stats_t stats;
    mesh_network_get_stats(&stats);
    cJSON *stats_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats_json, "tx_count", stats.tx_count);
    cJSON_AddNumberToObject(stats_json, "rx_count", stats.rx_count);
    cJSON_AddNumberToObject(stats_json, "tx_errors", stats.tx_errors);
    cJSON_AddNumberToObject(stats_json, "rx_errors", stats.rx_errors);
    cJSON_AddItemToObject(json, "stats", stats_json);

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/nodes - List all nodes
// ============================================================================
static esp_err_t api_nodes_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *nodes_array = cJSON_CreateArray();

    int count;
    node_info_t *nodes = node_manager_get_all(&count);

    for (int i = 0; i < count; i++) {
        cJSON *node = cJSON_CreateObject();

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
                 nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);

        cJSON_AddStringToObject(node, "mac", mac_str);
        cJSON_AddStringToObject(node, "name", mac_str);  // Use MAC as name (no name field in node_info_t)
        cJSON_AddNumberToObject(node, "device_type", nodes[i].device_type);

        const char *type_str = "Unknown";
        switch (nodes[i].device_type) {
            case DEVICE_TYPE_RELAY: type_str = "Relay"; break;
            case DEVICE_TYPE_LED_STRIP: type_str = "LED"; break;
            case DEVICE_TYPE_SENSOR: type_str = "Sensor"; break;
        }
        cJSON_AddStringToObject(node, "type_name", type_str);

        cJSON_AddNumberToObject(node, "status", nodes[i].status);
        cJSON_AddBoolToObject(node, "online", nodes[i].status == NODE_STATUS_ONLINE);
        cJSON_AddNumberToObject(node, "rssi", nodes[i].rssi);
        cJSON_AddNumberToObject(node, "mesh_layer", nodes[i].mesh_layer);
        cJSON_AddStringToObject(node, "firmware", nodes[i].firmware_version);

        uint32_t now = esp_timer_get_time() / 1000;
        uint32_t last_seen_ago = (now > nodes[i].last_seen) ? (now - nodes[i].last_seen) / 1000 : 0;
        cJSON_AddNumberToObject(node, "last_seen_sec", last_seen_ago);

        cJSON_AddItemToArray(nodes_array, node);
    }

    cJSON_AddItemToObject(json, "nodes", nodes_array);
    cJSON_AddNumberToObject(json, "count", count);

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/scan - Start node scan
// ============================================================================
static esp_err_t api_scan_handler(httpd_req_t *req)
{
    esp_err_t ret = commissioning_start_scan();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        webserver_log("Started node scan");
    }

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/scan/results - Get scan results
// ============================================================================
static esp_err_t api_scan_results_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *results_array = cJSON_CreateArray();

    scan_result_t results[MAX_SCAN_RESULTS];
    int count = commissioning_get_scan_results(results, MAX_SCAN_RESULTS);

    for (int i = 0; i < count; i++) {
        cJSON *result = cJSON_CreateObject();

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 results[i].mac[0], results[i].mac[1], results[i].mac[2],
                 results[i].mac[3], results[i].mac[4], results[i].mac[5]);

        cJSON_AddStringToObject(result, "mac", mac_str);
        cJSON_AddNumberToObject(result, "device_type", results[i].device_type);
        cJSON_AddStringToObject(result, "firmware", results[i].firmware_version);
        cJSON_AddNumberToObject(result, "rssi", results[i].rssi);
        cJSON_AddBoolToObject(result, "commissioned", results[i].commissioned);

        cJSON_AddItemToArray(results_array, result);
    }

    cJSON_AddItemToObject(json, "results", results_array);
    cJSON_AddNumberToObject(json, "count", count);
    cJSON_AddBoolToObject(json, "scanning", commissioning_is_scanning());
    cJSON_AddStringToObject(json, "mode",
        commissioning_get_mode() == COMMISSION_MODE_DISCOVERY ? "discovery" : "production");

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/scan/stop - Stop node scan and return to production
// ============================================================================
static esp_err_t api_scan_stop_handler(httpd_req_t *req)
{
    esp_err_t ret = commissioning_stop_scan();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        webserver_log("Stopped node scan - returned to production mesh");
    }

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/commission - Commission a node
// ============================================================================
static esp_err_t api_commission_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "=== API: COMMISSION REQUEST ===");

    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        ESP_LOGE(TAG, "Commission: Invalid JSON body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mac_json = cJSON_GetObjectItem(body, "mac");
    cJSON *name_json = cJSON_GetObjectItem(body, "name");

    if (!mac_json || !cJSON_IsString(mac_json)) {
        ESP_LOGE(TAG, "Commission: Missing mac field");
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac field");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Commission request for MAC: %s", mac_json->valuestring);

    // Parse MAC address
    uint8_t mac[6];
    if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        ESP_LOGE(TAG, "Commission: Invalid MAC format");
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
        return ESP_FAIL;
    }

    const char *name = (name_json && cJSON_IsString(name_json)) ? name_json->valuestring : NULL;

    ESP_LOGI(TAG, "Calling commissioning_add_node()...");
    esp_err_t ret = commissioning_add_node(mac, name);
    ESP_LOGI(TAG, "commissioning_add_node() returned: %s", esp_err_to_name(ret));

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        webserver_log("Commissioned node %s", mac_json->valuestring);
    } else {
        cJSON_AddStringToObject(response, "error", esp_err_to_name(ret));
        webserver_log("Commission FAILED for %s: %s", mac_json->valuestring, esp_err_to_name(ret));
    }

    cJSON_Delete(body);
    return send_json_response(req, response);
}

// ============================================================================
// POST /api/decommission - Decommission a node
// ============================================================================
static esp_err_t api_decommission_handler(httpd_req_t *req)
{
    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mac_json = cJSON_GetObjectItem(body, "mac");
    if (!mac_json || !cJSON_IsString(mac_json)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac field");
        return ESP_FAIL;
    }

    uint8_t mac[6];
    if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
        return ESP_FAIL;
    }

    esp_err_t ret = commissioning_remove_node(mac);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        webserver_log("Decommissioned node %s", mac_json->valuestring);
    }

    cJSON_Delete(body);
    return send_json_response(req, response);
}

// ============================================================================
// POST /api/command - Send command to node
// ============================================================================
static esp_err_t api_command_handler(httpd_req_t *req)
{
    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mac_json = cJSON_GetObjectItem(body, "mac");
    cJSON *cmd_json = cJSON_GetObjectItem(body, "cmd");

    if (!mac_json || !cJSON_IsString(mac_json) || !cmd_json || !cJSON_IsString(cmd_json)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac or cmd field");
        return ESP_FAIL;
    }

    uint8_t mac[6];
    if (sscanf(mac_json->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
        return ESP_FAIL;
    }

    const char *cmd = cmd_json->valuestring;
    esp_err_t ret = ESP_FAIL;

    // Build and send command message
    omniapi_message_t msg;

    if (strcmp(cmd, "relay_on") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_RELAY_CMD, 0, sizeof(payload_relay_cmd_t));
        payload_relay_cmd_t *payload = (payload_relay_cmd_t *)msg.payload;
        payload->channel = 0;
        payload->action = RELAY_ACTION_ON;
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_relay_cmd_t)));
    }
    else if (strcmp(cmd, "relay_off") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_RELAY_CMD, 0, sizeof(payload_relay_cmd_t));
        payload_relay_cmd_t *payload = (payload_relay_cmd_t *)msg.payload;
        payload->channel = 0;
        payload->action = RELAY_ACTION_OFF;
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_relay_cmd_t)));
    }
    else if (strcmp(cmd, "relay_toggle") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_RELAY_CMD, 0, sizeof(payload_relay_cmd_t));
        payload_relay_cmd_t *payload = (payload_relay_cmd_t *)msg.payload;
        payload->channel = 0;
        payload->action = RELAY_ACTION_TOGGLE;
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_relay_cmd_t)));
    }
    else if (strcmp(cmd, "led_on") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_LED_CMD, 0, sizeof(payload_led_cmd_t));
        payload_led_cmd_t *payload = (payload_led_cmd_t *)msg.payload;
        payload->action = LED_ACTION_ON;
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_led_cmd_t)));
    }
    else if (strcmp(cmd, "led_off") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_LED_CMD, 0, sizeof(payload_led_cmd_t));
        payload_led_cmd_t *payload = (payload_led_cmd_t *)msg.payload;
        payload->action = LED_ACTION_OFF;
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_led_cmd_t)));
    }
    else if (strcmp(cmd, "identify") == 0) {
        ret = commissioning_identify_node(mac);
    }
    else if (strcmp(cmd, "reboot") == 0) {
        OMNIAPI_INIT_HEADER(&msg.header, MSG_REBOOT, 0, 0);
        ret = mesh_network_send(mac, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(0));
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        webserver_log("Sent command '%s' to %s", cmd, mac_json->valuestring);
    }

    cJSON_Delete(body);
    return send_json_response(req, response);
}

// ============================================================================
// GET /api/logs - Get log entries
// ============================================================================
static esp_err_t api_logs_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *logs_array = cJSON_CreateArray();

    log_entry_t entries[50];
    int count = webserver_get_logs(entries, 50);

    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "ts", entries[i].timestamp);
        cJSON_AddStringToObject(entry, "msg", entries[i].message);
        cJSON_AddItemToArray(logs_array, entry);
    }

    cJSON_AddItemToObject(json, "logs", logs_array);
    cJSON_AddNumberToObject(json, "count", count);

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/ota/upload - Upload firmware for gateway OTA
// ============================================================================
static esp_err_t api_ota_upload_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    ESP_LOGI(TAG, "=== OTA UPLOAD REQUEST ===");
    ESP_LOGI(TAG, "Content-Length: %d bytes", req->content_len);

    // Check content length
    if (req->content_len <= 0) {
        ESP_LOGE(TAG, "No content in request");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No firmware data");
        return ESP_FAIL;
    }

    // Maximum firmware size check (2MB)
    if (req->content_len > 2 * 1024 * 1024) {
        ESP_LOGE(TAG, "Firmware too large: %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large (max 2MB)");
        return ESP_FAIL;
    }

    // Check if OTA already in progress
    if (ota_gateway_is_active()) {
        ESP_LOGE(TAG, "OTA already in progress");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA already in progress");
        return ESP_FAIL;
    }

    // Start OTA
    esp_err_t ret = ota_gateway_begin(req->content_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA");
        return ESP_FAIL;
    }

    webserver_log("Gateway OTA upload started (%d bytes)", req->content_len);

    // Read and write firmware data in chunks
    static uint8_t ota_buf[4096];
    int remaining = req->content_len;
    int received_total = 0;

    while (remaining > 0) {
        int to_read = (remaining > sizeof(ota_buf)) ? sizeof(ota_buf) : remaining;
        int received = httpd_req_recv(req, (char *)ota_buf, to_read);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                // Retry on timeout
                continue;
            }
            ESP_LOGE(TAG, "Error receiving data: %d", received);
            ota_gateway_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error receiving data");
            return ESP_FAIL;
        }

        // Write to flash
        ret = ota_gateway_write(ota_buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(ret));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write firmware");
            return ESP_FAIL;
        }

        remaining -= received;
        received_total += received;

        // Log progress every 100KB
        if ((received_total % (100 * 1024)) < received) {
            ESP_LOGI(TAG, "OTA upload progress: %d/%d bytes (%d%%)",
                     received_total, req->content_len,
                     (received_total * 100) / req->content_len);
        }
    }

    ESP_LOGI(TAG, "OTA upload complete: %d bytes received", received_total);

    // Finalize OTA
    ret = ota_gateway_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize OTA: %s", esp_err_to_name(ret));
        cJSON *json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Firmware validation failed");
        return send_json_response(req, json);
    }

    webserver_log("Gateway OTA complete - rebooting in 3 seconds");

    // Send success response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", true);
    cJSON_AddStringToObject(json, "message", "Firmware uploaded successfully. Rebooting in 3 seconds...");
    cJSON_AddNumberToObject(json, "bytes_written", received_total);

    esp_err_t resp_ret = send_json_response(req, json);

    // Schedule reboot after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return resp_ret;
}

// ============================================================================
// POST /api/node/ota - Upload firmware for specific node OTA (async flash-based)
// Query params: mac=XX:XX:XX:XX:XX:XX
// Firmware is buffered to flash, then sent to node in background task
// ============================================================================
static esp_err_t api_node_ota_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    ESP_LOGI(TAG, "=== NODE OTA UPLOAD REQUEST (ASYNC) ===");
    ESP_LOGI(TAG, "Content-Length: %d bytes", req->content_len);

    // Parse MAC from query string
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        ESP_LOGE(TAG, "Missing query parameters");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac parameter");
        return ESP_FAIL;
    }

    char mac_str[32] = {0};
    if (httpd_query_key_value(query, "mac", mac_str, sizeof(mac_str)) != ESP_OK) {
        ESP_LOGE(TAG, "Missing mac parameter");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac parameter");
        return ESP_FAIL;
    }

    // URL decode the MAC (handles %3A -> :)
    url_decode(mac_str);
    ESP_LOGI(TAG, "MAC after decode: %s", mac_str);

    // Parse MAC address
    uint8_t target_mac[6];
    int vals[6];
    if (sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
        // Try alternate format without colons
        if (sscanf(mac_str, "%02x%02x%02x%02x%02x%02x",
                   &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
            ESP_LOGE(TAG, "Invalid MAC format: %s", mac_str);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
            return ESP_FAIL;
        }
    }
    for (int i = 0; i < 6; i++) {
        target_mac[i] = (uint8_t)vals[i];
    }

    ESP_LOGI(TAG, "Target node: " MACSTR, MAC2STR(target_mac));

    // Check content length
    if (req->content_len <= 0) {
        ESP_LOGE(TAG, "No content in request");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No firmware data");
        return ESP_FAIL;
    }

    // Maximum firmware size check (1.5MB for nodes)
    if (req->content_len > 1536 * 1024) {
        ESP_LOGE(TAG, "Firmware too large: %d bytes", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large (max 1.5MB)");
        return ESP_FAIL;
    }

    // Check if node OTA already in progress
    if (node_ota_is_active() || node_ota_flash_staging_active()) {
        ESP_LOGE(TAG, "Node OTA already in progress");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Node OTA already in progress");
        return ESP_FAIL;
    }

    webserver_log("Node OTA upload started for " MACSTR " (%d bytes)",
                  MAC2STR(target_mac), req->content_len);

    // Start flash staging (writes to gateway's inactive OTA partition)
    esp_err_t ret = node_ota_flash_begin(target_mac, req->content_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin flash staging: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to prepare flash storage");
        return ESP_FAIL;
    }

    // Receive firmware and write to flash
    #define UPLOAD_BUF_SIZE 1024  // Larger buffer for faster HTTP upload
    uint8_t *upload_buf = malloc(UPLOAD_BUF_SIZE);
    if (upload_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate upload buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int received_total = 0;

    while (remaining > 0) {
        int to_read = (remaining > UPLOAD_BUF_SIZE) ? UPLOAD_BUF_SIZE : remaining;
        int received = httpd_req_recv(req, (char *)upload_buf, to_read);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry on timeout
            }
            ESP_LOGE(TAG, "Error receiving data: %d", received);
            free(upload_buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error receiving data");
            return ESP_FAIL;
        }

        // Write to flash
        ret = node_ota_flash_write(upload_buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to flash: %s", esp_err_to_name(ret));
            free(upload_buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
            return ESP_FAIL;
        }

        remaining -= received;
        received_total += received;

        // Log progress every 100KB
        if ((received_total % (100 * 1024)) < UPLOAD_BUF_SIZE) {
            ESP_LOGI(TAG, "Upload progress: %d/%d bytes (%d%%)",
                     received_total, req->content_len,
                     (received_total * 100) / req->content_len);
        }
    }

    free(upload_buf);

    ESP_LOGI(TAG, "Upload complete: %d bytes received", received_total);

    // Finish staging and start background OTA task
    ret = node_ota_flash_finish();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA");
        return ESP_FAIL;
    }

    webserver_log("Node OTA queued for " MACSTR " - sending in background", MAC2STR(target_mac));

    // Send success response immediately (OTA continues in background)
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", true);
    cJSON_AddStringToObject(json, "message", "Firmware uploaded. OTA transfer started in background.");
    cJSON_AddStringToObject(json, "target_mac", mac_str);
    cJSON_AddNumberToObject(json, "firmware_size", received_total);
    cJSON_AddStringToObject(json, "note", "Monitor progress via /api/node/ota/status or MQTT");

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/node/ota/status - Node OTA status (async mode)
// ============================================================================
static esp_err_t api_node_ota_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    bool active = node_ota_is_active();
    bool staging = node_ota_flash_staging_active();

    cJSON_AddBoolToObject(json, "active", active || staging);
    cJSON_AddBoolToObject(json, "staging", staging);
    cJSON_AddNumberToObject(json, "state", node_ota_get_state());
    cJSON_AddNumberToObject(json, "progress", node_ota_get_progress());

    if (active) {
        uint8_t mac[6];
        if (node_ota_get_target_mac(mac) == ESP_OK) {
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(mac));
            cJSON_AddStringToObject(json, "target_mac", mac_str);
        }
    }

    // State description
    const char *state_desc;
    switch (node_ota_get_state()) {
        case NODE_OTA_STATE_IDLE:      state_desc = "idle"; break;
        case NODE_OTA_STATE_STARTING:  state_desc = "starting"; break;
        case NODE_OTA_STATE_SENDING:   state_desc = "sending"; break;
        case NODE_OTA_STATE_FINISHING: state_desc = "finishing"; break;
        case NODE_OTA_STATE_COMPLETE:  state_desc = "complete"; break;
        case NODE_OTA_STATE_FAILED:    state_desc = "failed"; break;
        case NODE_OTA_STATE_ABORTED:   state_desc = "aborted"; break;
        default:                       state_desc = "unknown"; break;
    }
    cJSON_AddStringToObject(json, "state_desc", state_desc);

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/node/ota/abort - Abort node OTA
// ============================================================================
static esp_err_t api_node_ota_abort_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    esp_err_t ret = node_ota_abort();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", ret == ESP_OK);
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(json, "message", "Node OTA aborted");
        webserver_log("Node OTA aborted");
    } else {
        cJSON_AddStringToObject(json, "error", "Failed to abort");
    }

    return send_json_response(req, json);
}

// ============================================================================
// GET /api/ota/status - OTA status
// ============================================================================
static esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    // Node OTA status
    cJSON *node_ota = cJSON_CreateObject();
    cJSON_AddBoolToObject(node_ota, "active", ota_manager_is_active());
    cJSON_AddNumberToObject(node_ota, "state", ota_manager_get_state());

    uint8_t completed, failed, total;
    ota_manager_get_progress(&completed, &failed, &total);
    cJSON_AddNumberToObject(node_ota, "completed", completed);
    cJSON_AddNumberToObject(node_ota, "failed", failed);
    cJSON_AddNumberToObject(node_ota, "total", total);

    const ota_job_t *job = ota_manager_get_job();
    if (job && job->state != OTA_STATE_IDLE) {
        cJSON_AddStringToObject(node_ota, "version", job->version);
        cJSON_AddNumberToObject(node_ota, "device_type", job->device_type);
    }
    cJSON_AddItemToObject(json, "node_ota", node_ota);

    // Gateway OTA status
    cJSON *gateway_ota = cJSON_CreateObject();
    cJSON_AddBoolToObject(gateway_ota, "active", ota_gateway_is_active());

    uint32_t written, total_bytes;
    uint8_t progress;
    ota_gateway_get_progress(&written, &total_bytes, &progress);
    cJSON_AddNumberToObject(gateway_ota, "written_bytes", written);
    cJSON_AddNumberToObject(gateway_ota, "total_bytes", total_bytes);
    cJSON_AddNumberToObject(gateway_ota, "progress", progress);
    cJSON_AddItemToObject(json, "gateway_ota", gateway_ota);

    // Current firmware version
    cJSON_AddStringToObject(json, "current_version", CONFIG_GATEWAY_FIRMWARE_VERSION);

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/reboot - Reboot gateway
// ============================================================================
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", true);
    cJSON_AddStringToObject(json, "message", "Rebooting in 2 seconds...");

    webserver_log("Gateway reboot requested via Web UI");

    esp_err_t ret = send_json_response(req, json);

    // Schedule reboot
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ret;
}

// ============================================================================
// POST /api/factory-reset - Factory reset
// ============================================================================
static esp_err_t api_factory_reset_handler(httpd_req_t *req)
{
    webserver_log("Factory reset requested via Web UI");

    // Erase NVS
    nvs_flash_erase();

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", true);
    cJSON_AddStringToObject(json, "message", "Factory reset complete, rebooting...");

    esp_err_t ret = send_json_response(req, json);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ret;
}

// ============================================================================
// GET /api/wifi/scan - Scan available WiFi networks
// ============================================================================
static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    ESP_LOGI(TAG, "WiFi scan requested");

    // Start scan (blocking)
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        cJSON *json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
        return send_json_response(req, json);
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;  // Limit results

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Out of memory");
        return send_json_response(req, json);
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", true);
    cJSON_AddNumberToObject(json, "count", ap_count);
    cJSON *networks = cJSON_AddArrayToObject(json, "networks");

    // Track unique SSIDs
    for (int i = 0; i < ap_count; i++) {
        if (strlen((char *)ap_list[i].ssid) == 0) continue;

        // Skip duplicates
        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            if (strcmp((char *)ap_list[i].ssid, (char *)ap_list[j].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (char *)ap_list[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", ap_list[i].rssi);
        cJSON_AddNumberToObject(net, "channel", ap_list[i].primary);
        cJSON_AddBoolToObject(net, "secure", ap_list[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(networks, net);
    }

    free(ap_list);
    return send_json_response(req, json);
}

// ============================================================================
// GET /api/provision/status - Get provisioning status
// ============================================================================
static esp_err_t api_provision_status_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    const config_wifi_sta_t *wifi = config_get_wifi_sta();
    const config_mqtt_t *mqtt = config_get_mqtt();
    const config_wifi_ap_t *ap = config_get_wifi_ap();

    cJSON *json = cJSON_CreateObject();

    // Provisioning state
    provision_state_t state = config_get_provision_state();
    cJSON_AddNumberToObject(json, "state", state);
    cJSON_AddStringToObject(json, "state_name",
        state == PROVISION_STATE_CONFIGURED ? "configured" :
        state == PROVISION_STATE_WIFI_ONLY ? "wifi_only" : "unconfigured");
    cJSON_AddBoolToObject(json, "fully_configured", config_is_configured());

    // Gateway identity
    cJSON_AddStringToObject(json, "gateway_id", config_get_gateway_id());
    cJSON_AddStringToObject(json, "hostname", config_get_hostname());

    // WiFi STA config
    cJSON *wifi_obj = cJSON_AddObjectToObject(json, "wifi");
    cJSON_AddStringToObject(wifi_obj, "ssid", wifi->ssid);
    cJSON_AddBoolToObject(wifi_obj, "configured", wifi->configured);

    // WiFi AP config (for setup mode)
    cJSON *ap_obj = cJSON_AddObjectToObject(json, "ap");
    cJSON_AddStringToObject(ap_obj, "ssid", ap->ssid);
    cJSON_AddNumberToObject(ap_obj, "channel", ap->channel);

    // MQTT config
    cJSON *mqtt_obj = cJSON_AddObjectToObject(json, "mqtt");
    cJSON_AddStringToObject(mqtt_obj, "broker_uri", mqtt->broker_uri);
    cJSON_AddStringToObject(mqtt_obj, "username", mqtt->username);
    cJSON_AddStringToObject(mqtt_obj, "client_id", mqtt->client_id);
    cJSON_AddBoolToObject(mqtt_obj, "configured", mqtt->configured);

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/provision/wifi - Set WiFi credentials
// Body: {"ssid": "MyNetwork", "password": "secret"}
// ============================================================================
static esp_err_t api_provision_wifi_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(body, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(body, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'ssid'");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    ESP_LOGI(TAG, "Provisioning WiFi: SSID=%s", ssid);
    webserver_log("[PROVISION] Setting WiFi: %s", ssid);

    esp_err_t err = config_set_wifi_sta(ssid, password);
    cJSON_Delete(body);

    cJSON *json = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "message", "WiFi credentials saved. Rebooting in 2 seconds...");
        cJSON_AddBoolToObject(json, "reboot_required", true);
        send_json_response(req, json);
        // Auto-reboot after provisioning WiFi
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return ESP_OK;
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
    }

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/provision/mqtt - Set MQTT configuration
// Body: {"broker_uri": "mqtt://host:port", "username": "user", "password": "pass"}
// ============================================================================
static esp_err_t api_provision_mqtt_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *uri_item = cJSON_GetObjectItem(body, "broker_uri");
    cJSON *user_item = cJSON_GetObjectItem(body, "username");
    cJSON *pass_item = cJSON_GetObjectItem(body, "password");

    if (!uri_item || !cJSON_IsString(uri_item) || strlen(uri_item->valuestring) == 0) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'broker_uri'");
        return ESP_FAIL;
    }

    const char *broker_uri = uri_item->valuestring;
    const char *username = (user_item && cJSON_IsString(user_item)) ? user_item->valuestring : "";
    const char *password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    ESP_LOGI(TAG, "Provisioning MQTT: URI=%s, User=%s", broker_uri, username);
    webserver_log("[PROVISION] Setting MQTT: %s", broker_uri);

    esp_err_t err = config_set_mqtt(broker_uri, username, password);
    cJSON_Delete(body);

    cJSON *json = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "message", "MQTT configuration saved. Reboot to apply.");
        cJSON_AddBoolToObject(json, "reboot_required", true);
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
    }

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/provision/all - Set all config at once (for app setup flow)
// Body: {"wifi": {"ssid": "...", "password": "..."}, "mqtt": {"broker_uri": "...", ...}}
// ============================================================================
static esp_err_t api_provision_all_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool wifi_ok = false;
    bool mqtt_ok = false;

    // WiFi config
    cJSON *wifi_obj = cJSON_GetObjectItem(body, "wifi");
    if (wifi_obj && cJSON_IsObject(wifi_obj)) {
        cJSON *ssid = cJSON_GetObjectItem(wifi_obj, "ssid");
        cJSON *pass = cJSON_GetObjectItem(wifi_obj, "password");
        if (ssid && cJSON_IsString(ssid) && strlen(ssid->valuestring) > 0) {
            const char *password = (pass && cJSON_IsString(pass)) ? pass->valuestring : "";
            if (config_set_wifi_sta(ssid->valuestring, password) == ESP_OK) {
                wifi_ok = true;
                ESP_LOGI(TAG, "Provisioned WiFi: %s", ssid->valuestring);
            }
        }
    }

    // MQTT config
    cJSON *mqtt_obj = cJSON_GetObjectItem(body, "mqtt");
    if (mqtt_obj && cJSON_IsObject(mqtt_obj)) {
        cJSON *uri = cJSON_GetObjectItem(mqtt_obj, "broker_uri");
        cJSON *user = cJSON_GetObjectItem(mqtt_obj, "username");
        cJSON *pass = cJSON_GetObjectItem(mqtt_obj, "password");
        if (uri && cJSON_IsString(uri) && strlen(uri->valuestring) > 0) {
            const char *username = (user && cJSON_IsString(user)) ? user->valuestring : "";
            const char *password = (pass && cJSON_IsString(pass)) ? pass->valuestring : "";
            if (config_set_mqtt(uri->valuestring, username, password) == ESP_OK) {
                mqtt_ok = true;
                ESP_LOGI(TAG, "Provisioned MQTT: %s", uri->valuestring);
            }
        }
    }

    cJSON_Delete(body);
    webserver_log("[PROVISION] Complete setup - WiFi:%s, MQTT:%s",
                  wifi_ok ? "OK" : "SKIP", mqtt_ok ? "OK" : "SKIP");

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", wifi_ok || mqtt_ok);
    cJSON_AddBoolToObject(json, "wifi_configured", wifi_ok);
    cJSON_AddBoolToObject(json, "mqtt_configured", mqtt_ok);
    cJSON_AddStringToObject(json, "message",
        (wifi_ok || mqtt_ok) ? "Configuration saved. Rebooting in 2 seconds..." : "No valid configuration provided");
    cJSON_AddBoolToObject(json, "reboot_required", wifi_ok || mqtt_ok);

    if (wifi_ok || mqtt_ok) {
        send_json_response(req, json);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return ESP_OK;
    }

    return send_json_response(req, json);
}

// ============================================================================
// POST /api/node/config - Set node configuration
// Body: {"mac": "XX:XX:XX:XX:XX:XX", "key": "relay_mode", "value": "gpio|uart"}
// ============================================================================
static esp_err_t api_node_config_handler(httpd_req_t *req)
{
    set_cors_headers(req);

    cJSON *body = parse_json_body(req);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mac_item = cJSON_GetObjectItem(body, "mac");
    cJSON *key_item = cJSON_GetObjectItem(body, "key");
    cJSON *value_item = cJSON_GetObjectItem(body, "value");

    if (!mac_item || !key_item || !value_item ||
        !cJSON_IsString(mac_item) || !cJSON_IsString(key_item) || !cJSON_IsString(value_item)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mac, key, or value");
        return ESP_FAIL;
    }

    // Parse MAC
    uint8_t target_mac[6];
    int vals[6];
    if (sscanf(mac_item->valuestring, "%02x:%02x:%02x:%02x:%02x:%02x",
               &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MAC format");
        return ESP_FAIL;
    }
    for (int i = 0; i < 6; i++) target_mac[i] = (uint8_t)vals[i];

    // Build config message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_CONFIG_SET, 0, sizeof(payload_config_set_t));

    payload_config_set_t *cfg = (payload_config_set_t *)msg.payload;
    memcpy(cfg->mac, target_mac, 6);
    memset(cfg->value, 0, sizeof(cfg->value));

    const char *key = key_item->valuestring;
    const char *value = value_item->valuestring;

    if (strcmp(key, "relay_mode") == 0) {
        cfg->config_key = CONFIG_KEY_RELAY_MODE;
        cfg->value_len = 1;
        if (strcmp(value, "gpio") == 0) {
            cfg->value[0] = RELAY_MODE_GPIO;
        } else if (strcmp(value, "uart") == 0) {
            cfg->value[0] = RELAY_MODE_UART;
        } else {
            cJSON_Delete(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid value (use 'gpio' or 'uart')");
            return ESP_FAIL;
        }
    } else {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown config key");
        return ESP_FAIL;
    }

    cJSON_Delete(body);

    // Send to node via mesh
    esp_err_t ret = mesh_network_send(target_mac, (uint8_t *)&msg,
                                       OMNIAPI_MSG_SIZE(sizeof(payload_config_set_t)));

    cJSON *json = cJSON_CreateObject();
    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "message", "Config sent to node");
        cJSON_AddStringToObject(json, "key", key);
        cJSON_AddStringToObject(json, "value", value);
        webserver_log("Config %s=%s sent to " MACSTR, key, value, MAC2STR(target_mac));
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Failed to send config");
    }

    return send_json_response(req, json);
}

// ============================================================================
// Captive Portal Detection Handlers
// These respond to OS-specific connectivity check URLs to trigger
// "Sign in to network" notification on phones/laptops
// ============================================================================
static esp_err_t api_captive_redirect_handler(httpd_req_t *req)
{
    // Redirect to our provisioning page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

// Android: expects 204, we return 302 redirect instead
static esp_err_t api_captive_generate204_handler(httpd_req_t *req)
{
    return api_captive_redirect_handler(req);
}

// Apple iOS/macOS: expects specific HTML, we return redirect
static esp_err_t api_captive_apple_handler(httpd_req_t *req)
{
    return api_captive_redirect_handler(req);
}

// Windows: expects specific text, we return redirect
static esp_err_t api_captive_windows_handler(httpd_req_t *req)
{
    return api_captive_redirect_handler(req);
}

// ============================================================================
// OPTIONS handler for CORS preflight
// ============================================================================
static esp_err_t api_options_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ============================================================================
// Register all handlers
// ============================================================================
esp_err_t web_api_register_handlers(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering API handlers");

    // Define all API endpoints
    static const struct {
        const char *uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t *);
    } endpoints[] = {
        {"/api/status",        HTTP_GET,  api_status_handler},
        {"/api/network",       HTTP_GET,  api_network_handler},
        {"/api/mesh",          HTTP_GET,  api_mesh_handler},
        {"/api/nodes",         HTTP_GET,  api_nodes_handler},
        {"/api/scan",          HTTP_POST, api_scan_handler},
        {"/api/scan/stop",     HTTP_POST, api_scan_stop_handler},
        {"/api/scan/results",  HTTP_GET,  api_scan_results_handler},
        {"/api/commission",    HTTP_POST, api_commission_handler},
        {"/api/decommission",  HTTP_POST, api_decommission_handler},
        {"/api/command",       HTTP_POST, api_command_handler},
        {"/api/logs",          HTTP_GET,  api_logs_handler},
        {"/api/ota/status",    HTTP_GET,  api_ota_status_handler},
        {"/api/ota/upload",    HTTP_POST, api_ota_upload_handler},
        {"/api/node/ota",      HTTP_POST, api_node_ota_handler},
        {"/api/node/ota/status", HTTP_GET, api_node_ota_status_handler},
        {"/api/node/ota/abort", HTTP_POST, api_node_ota_abort_handler},
        {"/api/node/config",   HTTP_POST, api_node_config_handler},
        {"/api/reboot",        HTTP_POST, api_reboot_handler},
        {"/api/factory-reset", HTTP_POST, api_factory_reset_handler},
        // WiFi scan
        {"/api/wifi/scan",        HTTP_GET,  api_wifi_scan_handler},
        // Provisioning endpoints
        {"/api/provision/status", HTTP_GET,  api_provision_status_handler},
        {"/api/provision/wifi",   HTTP_POST, api_provision_wifi_handler},
        {"/api/provision/mqtt",   HTTP_POST, api_provision_mqtt_handler},
        {"/api/provision/all",    HTTP_POST, api_provision_all_handler},
        // Captive portal detection endpoints (trigger "Sign in to network")
        {"/generate_204",         HTTP_GET,  api_captive_generate204_handler},
        {"/gen_204",              HTTP_GET,  api_captive_generate204_handler},
        {"/hotspot-detect.html",  HTTP_GET,  api_captive_apple_handler},
        {"/connecttest.txt",      HTTP_GET,  api_captive_windows_handler},
        {"/redirect",             HTTP_GET,  api_captive_redirect_handler},
        {"/canonical.html",       HTTP_GET,  api_captive_redirect_handler},
        {"/success.txt",          HTTP_GET,  api_captive_redirect_handler},
    };

    for (int i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        httpd_uri_t uri = {
            .uri = endpoints[i].uri,
            .method = endpoints[i].method,
            .handler = endpoints[i].handler
        };
        httpd_register_uri_handler(server, &uri);

        // Also register OPTIONS for CORS
        httpd_uri_t options_uri = {
            .uri = endpoints[i].uri,
            .method = HTTP_OPTIONS,
            .handler = api_options_handler
        };
        httpd_register_uri_handler(server, &options_uri);
    }

    ESP_LOGI(TAG, "Registered %d API endpoints", sizeof(endpoints) / sizeof(endpoints[0]));
    return ESP_OK;
}
