/**
 * OmniaPi Node Mesh - Commissioning Implementation
 *
 * Handles node commissioning, network credentials storage,
 * and discovery/production mesh switching.
 */

#include "commissioning.h"
#include "nvs_storage.h"
#include "mesh_node.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "COMMISSION";

// ============================================================================
// NVS Keys
// ============================================================================
#define NVS_KEY_COMMISSIONED    "commissioned"
#define NVS_KEY_NETWORK_ID      "network_id"
#define NVS_KEY_NETWORK_KEY     "network_key"
#define NVS_KEY_PLANT_ID        "plant_id"
#define NVS_KEY_NODE_NAME       "node_name"

// ============================================================================
// Commissioning State
// ============================================================================
static bool s_commissioned = false;
static uint8_t s_network_id[6] = {0};
static char s_network_key[33] = {0};
static char s_plant_id[33] = {0};
static char s_node_name[33] = {0};

// ============================================================================
// Initialization
// ============================================================================

esp_err_t commissioning_init(void)
{
    ESP_LOGI(TAG, "Initializing commissioning handler");

    // Load commissioning state from NVS
    uint8_t commissioned = 0;
    size_t len = sizeof(commissioned);
    if (nvs_storage_load_blob(NVS_KEY_COMMISSIONED, &commissioned, &len) == ESP_OK) {
        s_commissioned = (commissioned == 1);
    }

    if (s_commissioned) {
        // Load network credentials
        len = sizeof(s_network_id);
        nvs_storage_load_blob(NVS_KEY_NETWORK_ID, s_network_id, &len);
        nvs_storage_load_string(NVS_KEY_NETWORK_KEY, s_network_key, sizeof(s_network_key));

        // Load plant ID and node name
        nvs_storage_load_string(NVS_KEY_PLANT_ID, s_plant_id, sizeof(s_plant_id));
        nvs_storage_load_string(NVS_KEY_NODE_NAME, s_node_name, sizeof(s_node_name));

        ESP_LOGI(TAG, "Node is COMMISSIONED");
        ESP_LOGI(TAG, "  Network ID: %02X:%02X:%02X:%02X:%02X:%02X",
                 s_network_id[0], s_network_id[1], s_network_id[2],
                 s_network_id[3], s_network_id[4], s_network_id[5]);
        ESP_LOGI(TAG, "  Plant ID: %s", s_plant_id);
        ESP_LOGI(TAG, "  Name: %s", s_node_name);
    } else {
        ESP_LOGI(TAG, "Node is NOT commissioned (discovery mode)");
    }

    return ESP_OK;
}

// ============================================================================
// Status Getters
// ============================================================================

bool commissioning_is_commissioned(void)
{
    return s_commissioned;
}

esp_err_t commissioning_get_network_credentials(uint8_t *network_id, char *network_key)
{
    if (!s_commissioned) {
        return ESP_ERR_INVALID_STATE;
    }

    if (network_id) {
        memcpy(network_id, s_network_id, 6);
    }
    if (network_key) {
        strncpy(network_key, s_network_key, 32);
        network_key[32] = '\0';
    }

    return ESP_OK;
}

esp_err_t commissioning_get_plant_id(char *plant_id)
{
    if (plant_id == NULL) return ESP_ERR_INVALID_ARG;
    strncpy(plant_id, s_plant_id, 32);
    plant_id[32] = '\0';
    return ESP_OK;
}

esp_err_t commissioning_get_node_name(char *name)
{
    if (name == NULL) return ESP_ERR_INVALID_ARG;
    strncpy(name, s_node_name, 32);
    name[32] = '\0';
    return ESP_OK;
}

// ============================================================================
// Message Handlers
// ============================================================================

void commissioning_handle_scan_request(const omniapi_message_t *msg)
{
    ESP_LOGI(TAG, "Received scan request from gateway");

    // Get our MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Respond with scan response
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_SCAN_RESPONSE, msg->header.seq, sizeof(payload_scan_response_t));

    payload_scan_response_t *resp = (payload_scan_response_t *)response.payload;
    memcpy(resp->mac, mac, 6);

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY
    resp->device_type = DEVICE_TYPE_RELAY;
#elif defined(CONFIG_NODE_DEVICE_TYPE_LED)
    resp->device_type = DEVICE_TYPE_LED_STRIP;
#else
    resp->device_type = DEVICE_TYPE_SENSOR;
#endif

    resp->firmware_version = (1 << 16) | (1 << 8) | 2;  // v1.1.2
    resp->commissioned = s_commissioned ? 1 : 0;
    resp->rssi = mesh_node_get_parent_rssi();

    // Send response to gateway
    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_scan_response_t)));

    ESP_LOGI(TAG, "Sent scan response (commissioned=%d)", s_commissioned);
}

void commissioning_handle_commission(const omniapi_message_t *msg)
{
    ESP_LOGI(TAG, "Received commission command from gateway");

    const payload_commission_t *cmd = (const payload_commission_t *)msg->payload;

    // Get our MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Verify this commission is for us
    if (memcmp(cmd->mac, mac, 6) != 0) {
        ESP_LOGW(TAG, "Commission command not for this node, ignoring");
        return;
    }

    ESP_LOGI(TAG, "Commission command is for us!");
    ESP_LOGI(TAG, "  Network ID: %02X:%02X:%02X:%02X:%02X:%02X",
             cmd->network_id[0], cmd->network_id[1], cmd->network_id[2],
             cmd->network_id[3], cmd->network_id[4], cmd->network_id[5]);
    ESP_LOGI(TAG, "  Plant ID: %s", cmd->plant_id);
    ESP_LOGI(TAG, "  Node Name: %s", cmd->node_name);

    // Save network credentials
    memcpy(s_network_id, cmd->network_id, 6);
    strncpy(s_network_key, cmd->network_key, 32);
    s_network_key[32] = '\0';

    nvs_storage_save_blob(NVS_KEY_NETWORK_ID, s_network_id, 6);
    nvs_storage_save_string(NVS_KEY_NETWORK_KEY, s_network_key);

    // Save plant ID and node name
    strncpy(s_plant_id, cmd->plant_id, 32);
    s_plant_id[32] = '\0';
    strncpy(s_node_name, cmd->node_name, 32);
    s_node_name[32] = '\0';

    nvs_storage_save_string(NVS_KEY_PLANT_ID, s_plant_id);
    nvs_storage_save_string(NVS_KEY_NODE_NAME, s_node_name);

    // Mark as commissioned
    uint8_t commissioned = 1;
    nvs_storage_save_blob(NVS_KEY_COMMISSIONED, &commissioned, sizeof(commissioned));
    s_commissioned = true;

    ESP_LOGI(TAG, "Node commissioned successfully!");

    // Send commission ACK
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_COMMISSION_ACK, msg->header.seq, sizeof(payload_commission_ack_t));

    payload_commission_ack_t *ack = (payload_commission_ack_t *)response.payload;
    memcpy(ack->mac, mac, 6);
    ack->status = 0;  // Success

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_commission_ack_t)));

    ESP_LOGI(TAG, "Commission ACK sent, restarting to join production mesh...");

    // Wait for ACK to be sent, then restart to join production mesh
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void commissioning_handle_decommission(const omniapi_message_t *msg)
{
    ESP_LOGW(TAG, "Received decommission command from gateway");

    const payload_decommission_t *cmd = (const payload_decommission_t *)msg->payload;

    // Get our MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Verify this decommission is for us
    if (memcmp(cmd->mac, mac, 6) != 0) {
        ESP_LOGW(TAG, "Decommission command not for this node, ignoring");
        return;
    }

    // Send ACK before reset
    omniapi_message_t response;
    OMNIAPI_INIT_HEADER(&response.header, MSG_DECOMMISSION_ACK, msg->header.seq, sizeof(payload_decommission_ack_t));

    payload_decommission_ack_t *ack = (payload_decommission_ack_t *)response.payload;
    memcpy(ack->mac, mac, 6);
    ack->status = 0;  // Success

    mesh_node_send_to_root((uint8_t *)&response, OMNIAPI_MSG_SIZE(sizeof(payload_decommission_ack_t)));

    // Wait for message to be sent
    vTaskDelay(pdMS_TO_TICKS(500));

    // Perform factory reset
    commissioning_factory_reset();
}

// ============================================================================
// Factory Reset
// ============================================================================

void commissioning_factory_reset(void)
{
    ESP_LOGW(TAG, "=== FACTORY RESET ===");

    // Clear all commissioning data
    nvs_storage_erase(NVS_KEY_COMMISSIONED);
    nvs_storage_erase(NVS_KEY_NETWORK_ID);
    nvs_storage_erase(NVS_KEY_NETWORK_KEY);
    nvs_storage_erase(NVS_KEY_PLANT_ID);
    nvs_storage_erase(NVS_KEY_NODE_NAME);

    // Clear state
    s_commissioned = false;
    memset(s_network_id, 0, sizeof(s_network_id));
    memset(s_network_key, 0, sizeof(s_network_key));
    memset(s_plant_id, 0, sizeof(s_plant_id));
    memset(s_node_name, 0, sizeof(s_node_name));

    ESP_LOGW(TAG, "Commissioning data cleared, restarting in 2 seconds...");

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}
