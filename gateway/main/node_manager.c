/**
 * OmniaPi Gateway - Node Manager
 * Tracks connected ESP-NOW nodes and their states
 */

#include "node_manager.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "node_manager";

// Node storage
static node_info_t s_nodes[MAX_NODES];
static int s_node_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

// ============== Helper Functions ==============
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ============== Public Functions ==============
esp_err_t node_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing Node Manager");

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Clear node array
    memset(s_nodes, 0, sizeof(s_nodes));
    s_node_count = 0;

    ESP_LOGI(TAG, "Node Manager initialized (max %d nodes)", MAX_NODES);
    return ESP_OK;
}

int node_manager_find_or_add(const uint8_t *mac, int8_t rssi)
{
    if (mac == NULL) return -1;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Search existing nodes
    for (int i = 0; i < s_node_count; i++) {
        if (memcmp(s_nodes[i].mac, mac, MAC_ADDR_LEN) == 0) {
            // Update existing node
            s_nodes[i].rssi = rssi;
            s_nodes[i].last_seen = get_time_ms();
            s_nodes[i].messages_received++;
            s_nodes[i].online = true;
            xSemaphoreGive(s_mutex);
            return i;
        }
    }

    // Add new node if space available
    if (s_node_count < MAX_NODES) {
        int idx = s_node_count;
        memcpy(s_nodes[idx].mac, mac, MAC_ADDR_LEN);
        s_nodes[idx].rssi = rssi;
        s_nodes[idx].last_seen = get_time_ms();
        s_nodes[idx].messages_received = 1;
        s_nodes[idx].online = true;
        s_nodes[idx].version[0] = '\0';
        s_nodes[idx].relay_states[0] = 0;
        s_nodes[idx].relay_states[1] = 0;
        s_nodes[idx].relay_count = 2;
        s_node_count++;

        char mac_str[18];
        node_manager_mac_to_string(mac, mac_str);
        ESP_LOGI(TAG, "New node registered: %s (index %d)", mac_str, idx);

        xSemaphoreGive(s_mutex);
        return idx;
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGW(TAG, "Node array full, cannot add new node");
    return -1;
}

node_info_t *node_manager_get_node(int index)
{
    if (index < 0 || index >= s_node_count) {
        return NULL;
    }
    return &s_nodes[index];
}

node_info_t *node_manager_get_by_mac(const uint8_t *mac)
{
    if (mac == NULL) return NULL;

    for (int i = 0; i < s_node_count; i++) {
        if (memcmp(s_nodes[i].mac, mac, MAC_ADDR_LEN) == 0) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

void node_manager_update_relay(int index, uint8_t channel, uint8_t state)
{
    if (index < 0 || index >= s_node_count) return;
    if (channel < 1 || channel > 2) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_nodes[index].relay_states[channel - 1] = state;
    xSemaphoreGive(s_mutex);

    char mac_str[18];
    node_manager_mac_to_string(s_nodes[index].mac, mac_str);
    ESP_LOGI(TAG, "Node %s relay %d = %s", mac_str, channel, state ? "ON" : "OFF");
}

void node_manager_update_version(int index, const char *version)
{
    if (index < 0 || index >= s_node_count || version == NULL) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_nodes[index].version, version, sizeof(s_nodes[index].version) - 1);
    s_nodes[index].version[sizeof(s_nodes[index].version) - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

int node_manager_get_count(void)
{
    return s_node_count;
}

void node_manager_check_online_status(void)
{
    uint32_t now = get_time_ms();

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_node_count; i++) {
        // Mark offline if not seen for 10 seconds
        if (now - s_nodes[i].last_seen > 10000) {
            if (s_nodes[i].online) {
                s_nodes[i].online = false;
                char mac_str[18];
                node_manager_mac_to_string(s_nodes[i].mac, mac_str);
                ESP_LOGW(TAG, "Node %s went offline", mac_str);
            }
        }
    }
    xSemaphoreGive(s_mutex);
}

void node_manager_mac_to_string(const uint8_t *mac, char *buffer)
{
    if (mac == NULL || buffer == NULL) return;
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool node_manager_mac_from_string(const char *str, uint8_t *mac)
{
    if (str == NULL || mac == NULL) return false;

    int values[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)values[i];
    }
    return true;
}

int node_manager_get_nodes_json(char *buffer, size_t len)
{
    if (buffer == NULL || len == 0) return 0;

    uint32_t now = get_time_ms();
    int written = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    written = snprintf(buffer, len, "{\"nodes\":[");

    for (int i = 0; i < s_node_count && written < (int)len - 200; i++) {
        char mac_str[18];
        node_manager_mac_to_string(s_nodes[i].mac, mac_str);

        uint32_t ago = (now - s_nodes[i].last_seen) / 1000;
        char last_seen_str[32];
        if (ago < 60) {
            snprintf(last_seen_str, sizeof(last_seen_str), "%lus ago", (unsigned long)ago);
        } else {
            snprintf(last_seen_str, sizeof(last_seen_str), "%lum ago", (unsigned long)(ago / 60));
        }

        written += snprintf(buffer + written, len - written,
            "%s{\"mac\":\"%s\",\"rssi\":%d,\"messages\":%lu,\"online\":%s,"
            "\"version\":\"%s\",\"relays\":[%d,%d],\"lastSeen\":\"%s\"}",
            i > 0 ? "," : "",
            mac_str,
            s_nodes[i].rssi,
            (unsigned long)s_nodes[i].messages_received,
            s_nodes[i].online ? "true" : "false",
            s_nodes[i].version,
            s_nodes[i].relay_states[0],
            s_nodes[i].relay_states[1],
            last_seen_str);
    }

    written += snprintf(buffer + written, len - written,
        "],\"count\":%d,\"timestamp\":%lu}",
        s_node_count, (unsigned long)now);

    xSemaphoreGive(s_mutex);

    return written;
}
