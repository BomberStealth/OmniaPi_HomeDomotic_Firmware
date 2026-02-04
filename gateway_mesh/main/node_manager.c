/**
 * OmniaPi Gateway Mesh - Node Manager Implementation
 */

#include "node_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "NODE_MGR";

static node_info_t s_nodes[MAX_NODES];
static int s_node_count = 0;

static int find_node_index(const uint8_t *mac)
{
    for (int i = 0; i < s_node_count; i++) {
        if (memcmp(s_nodes[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t node_manager_init(void)
{
    ESP_LOGI(TAG, "Node manager initialized");
    memset(s_nodes, 0, sizeof(s_nodes));
    s_node_count = 0;
    return ESP_OK;
}

esp_err_t node_manager_add_node(const uint8_t *mac)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    int idx = find_node_index(mac);
    if (idx >= 0) {
        // Node exists, update last_seen
        s_nodes[idx].last_seen = esp_timer_get_time() / 1000;
        s_nodes[idx].status = NODE_STATUS_ONLINE;
        return ESP_OK;
    }

    if (s_node_count >= MAX_NODES) {
        ESP_LOGE(TAG, "Max nodes reached!");
        return ESP_ERR_NO_MEM;
    }

    node_info_t *node = &s_nodes[s_node_count];
    memcpy(node->mac, mac, 6);
    node->status = NODE_STATUS_ONLINE;
    node->last_seen = esp_timer_get_time() / 1000;
    node->device_type = DEVICE_TYPE_UNKNOWN;
    s_node_count++;

    ESP_LOGI(TAG, "Node added: %02X:%02X:%02X:%02X:%02X:%02X (total: %d)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], s_node_count);

    return ESP_OK;
}

esp_err_t node_manager_remove_node(const uint8_t *mac)
{
    int idx = find_node_index(mac);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    // Shift remaining nodes
    for (int i = idx; i < s_node_count - 1; i++) {
        s_nodes[i] = s_nodes[i + 1];
    }
    s_node_count--;

    ESP_LOGI(TAG, "Node removed (total: %d)", s_node_count);
    return ESP_OK;
}

esp_err_t node_manager_set_offline(const uint8_t *mac)
{
    int idx = find_node_index(mac);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    s_nodes[idx].status = NODE_STATUS_OFFLINE;
    return ESP_OK;
}

void node_manager_check_timeouts(void)
{
    uint32_t now = esp_timer_get_time() / 1000;
    uint32_t timeout = CONFIG_GATEWAY_NODE_TIMEOUT_MS;

    for (int i = 0; i < s_node_count; i++) {
        if (s_nodes[i].status == NODE_STATUS_ONLINE) {
            if ((now - s_nodes[i].last_seen) > timeout) {
                ESP_LOGW(TAG, "Node timeout: %02X:%02X:%02X:%02X:%02X:%02X",
                         s_nodes[i].mac[0], s_nodes[i].mac[1], s_nodes[i].mac[2],
                         s_nodes[i].mac[3], s_nodes[i].mac[4], s_nodes[i].mac[5]);
                s_nodes[i].status = NODE_STATUS_OFFLINE;
            }
        }
    }
}

int node_manager_get_count(void)
{
    return s_node_count;
}

node_info_t* node_manager_get_node(const uint8_t *mac)
{
    int idx = find_node_index(mac);
    return (idx >= 0) ? &s_nodes[idx] : NULL;
}

node_info_t* node_manager_get_all(int *count)
{
    if (count) *count = s_node_count;
    return s_nodes;
}

esp_err_t node_manager_update_info(const uint8_t *mac, const payload_heartbeat_ack_t *info)
{
    if (mac == NULL || info == NULL) return ESP_ERR_INVALID_ARG;

    int idx = find_node_index(mac);
    if (idx < 0) return ESP_ERR_NOT_FOUND;

    node_info_t *node = &s_nodes[idx];
    node->device_type = info->device_type;
    node->status = info->status;
    node->mesh_layer = info->mesh_layer;
    node->rssi = info->rssi;
    node->last_seen = esp_timer_get_time() / 1000;

    // Parse firmware version from packed uint32_t (major<<16 | minor<<8 | patch)
    snprintf(node->firmware_version, sizeof(node->firmware_version),
             "%lu.%lu.%lu",
             (unsigned long)((info->firmware_version >> 16) & 0xFF),
             (unsigned long)((info->firmware_version >> 8) & 0xFF),
             (unsigned long)(info->firmware_version & 0xFF));

    return ESP_OK;
}
