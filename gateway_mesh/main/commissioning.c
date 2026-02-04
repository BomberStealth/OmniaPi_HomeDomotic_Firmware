/**
 * OmniaPi Gateway Mesh - Commissioning Implementation
 *
 * Handles node discovery, commissioning, and decommissioning over ESP-WIFI-MESH
 */

#include "commissioning.h"
#include "mesh_network.h"
#include "mqtt_handler.h"
#include "node_manager.h"
#include "omniapi_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "COMMISSION";

// ============================================================================
// Configuration
// ============================================================================
#define SCAN_TIMEOUT_MS         300000  // Safety timeout: 5 minutes (user controls stop)
#define SCAN_CLEANUP_TIMEOUT_MS 60000   // Remove old results after 60 seconds
#define MESH_SWITCH_DELAY_MS    1000    // Wait time after mesh switch

// Mesh IDs
static const uint8_t MESH_ID_PROD[6] = MESH_ID_PRODUCTION;
static const uint8_t MESH_ID_DISC[6] = MESH_ID_DISCOVERY;

// ============================================================================
// State
// ============================================================================
static commission_mode_t s_current_mode = COMMISSION_MODE_PRODUCTION;
static bool s_scanning = false;
static TimerHandle_t s_scan_timer = NULL;
static scan_result_t s_scan_results[MAX_SCAN_RESULTS];
static int s_scan_count = 0;
static uint8_t s_current_seq = 0;

// Production network credentials (generated per-plant)
static uint8_t s_network_id[6] = {0};
static char s_network_key[33] = {0};
static char s_plant_id[33] = {0};
static bool s_credentials_set = false;

// ============================================================================
// Forward Declarations
// ============================================================================
static void scan_timer_callback(TimerHandle_t timer);
static int find_scan_result_by_mac(const uint8_t *mac);
static void cleanup_old_results(void);
static void scan_start_task(void *pvParameters);
static void scan_stop_task(void *pvParameters);

// Task handle for async scan operations
static TaskHandle_t s_scan_task_handle = NULL;

// ============================================================================
// Initialization
// ============================================================================

esp_err_t commissioning_init(void)
{
    ESP_LOGI(TAG, "Initializing commissioning handler");

    // Create scan timeout timer
    s_scan_timer = xTimerCreate("scan_timer",
                                 pdMS_TO_TICKS(SCAN_TIMEOUT_MS),
                                 pdFALSE,  // One-shot
                                 NULL,
                                 scan_timer_callback);

    if (s_scan_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create scan timer");
        return ESP_FAIL;
    }

    // Initialize scan results
    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_count = 0;

    // Initialize default production network credentials
    // These are used when commissioning nodes via Web UI without backend
    static const uint8_t default_network_id[6] = MESH_ID_PRODUCTION;
    memcpy(s_network_id, default_network_id, 6);
    strncpy(s_network_key, MESH_PASSWORD_PRODUCTION, 32);
    s_network_key[32] = '\0';
    strncpy(s_plant_id, "default_plant", 32);
    s_plant_id[32] = '\0';
    s_credentials_set = true;

    ESP_LOGI(TAG, "Default production credentials set:");
    ESP_LOGI(TAG, "  Network ID: %02X:%02X:%02X:%02X:%02X:%02X (OMNIAP)",
             s_network_id[0], s_network_id[1], s_network_id[2],
             s_network_id[3], s_network_id[4], s_network_id[5]);
    ESP_LOGI(TAG, "  Plant ID: %s", s_plant_id);

    ESP_LOGI(TAG, "Commissioning handler initialized");
    return ESP_OK;
}

commission_mode_t commissioning_get_mode(void)
{
    return s_current_mode;
}

// ============================================================================
// Credentials Management
// ============================================================================

esp_err_t commissioning_set_credentials(const uint8_t *network_id,
                                         const char *network_key,
                                         const char *plant_id)
{
    if (network_id == NULL || network_key == NULL || plant_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_network_id, network_id, 6);
    strncpy(s_network_key, network_key, 32);
    s_network_key[32] = '\0';
    strncpy(s_plant_id, plant_id, 32);
    s_plant_id[32] = '\0';

    s_credentials_set = true;

    ESP_LOGI(TAG, "Network credentials set:");
    ESP_LOGI(TAG, "  Network ID: %02X:%02X:%02X:%02X:%02X:%02X",
             s_network_id[0], s_network_id[1], s_network_id[2],
             s_network_id[3], s_network_id[4], s_network_id[5]);
    ESP_LOGI(TAG, "  Plant ID: %s", s_plant_id);

    return ESP_OK;
}

esp_err_t commissioning_get_credentials(uint8_t *network_id, char *network_key)
{
    if (!s_credentials_set) {
        return ESP_ERR_INVALID_STATE;
    }

    if (network_id) memcpy(network_id, s_network_id, 6);
    if (network_key) {
        strncpy(network_key, s_network_key, 32);
        network_key[32] = '\0';  // Ensure null termination
    }

    return ESP_OK;
}

// ============================================================================
// Scanning
// ============================================================================

// Async task to perform the actual mesh switch for scanning
static void scan_start_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Scan task started - switching to discovery mesh...");

    // Small delay to allow HTTP response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));

    // Step 1: Stop production mesh
    ESP_LOGI(TAG, "Step 1: Stopping production mesh...");
    esp_err_t ret = mesh_network_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop production mesh: %s", esp_err_to_name(ret));
        s_scanning = false;
        s_scan_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Wait for mesh to fully stop
    vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));

    // Step 2: Start discovery mesh as ROOT
    ESP_LOGI(TAG, "Step 2: Starting discovery mesh (OMNIDS) as ROOT...");
    ret = mesh_network_start_with_id(MESH_ID_DISC, MESH_PASSWORD_DISCOVERY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start discovery mesh: %s", esp_err_to_name(ret));
        // Try to restart production mesh
        mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
        s_scanning = false;
        s_scan_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_current_mode = COMMISSION_MODE_DISCOVERY;

    // Wait for discovery mesh to stabilize
    vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS * 2));

    // Step 3: Broadcast scan request
    ESP_LOGI(TAG, "Step 3: Broadcasting scan request...");
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_SCAN_REQUEST, s_current_seq, 0);

    ret = mesh_network_broadcast((uint8_t *)&msg, OMNIAPI_MSG_SIZE(0));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Scan request broadcast sent (seq=%d)", s_current_seq);
    } else {
        ESP_LOGW(TAG, "Broadcast failed (no children yet): %s", esp_err_to_name(ret));
        // This is OK - nodes may connect later and respond
    }

    // Start scan timeout timer
    xTimerStart(s_scan_timer, 0);

    ESP_LOGI(TAG, "=== DISCOVERY MODE ACTIVE - Waiting for nodes (timeout: %d sec) ===",
             SCAN_TIMEOUT_MS / 1000);

    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t commissioning_start_scan(void)
{
    ESP_LOGI(TAG, "=== STARTING NODE SCAN ===");
    ESP_LOGI(TAG, "  Current state: scanning=%d, mode=%s, task_handle=%p",
             s_scanning,
             s_current_mode == COMMISSION_MODE_DISCOVERY ? "DISCOVERY" : "PRODUCTION",
             (void*)s_scan_task_handle);

    // If we think we're scanning but task handle is NULL, the state is stale - reset it
    if (s_scanning && s_scan_task_handle == NULL) {
        ESP_LOGW(TAG, "Scan state was stale (s_scanning=true but no task), resetting...");
        s_scanning = false;
        // Also force return to production if we were stuck in discovery
        if (s_current_mode == COMMISSION_MODE_DISCOVERY) {
            ESP_LOGW(TAG, "Also stuck in DISCOVERY mode, forcing switch to PRODUCTION...");
            xTimerStop(s_scan_timer, 0);
            mesh_network_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
            s_current_mode = COMMISSION_MODE_PRODUCTION;
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for mesh to stabilize
        }
    }

    if (s_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_scan_task_handle != NULL) {
        ESP_LOGW(TAG, "Scan task already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Cleanup old results first
    cleanup_old_results();

    s_scanning = true;
    s_current_seq++;

    // Create task to do the actual mesh switch (async)
    // This allows the HTTP response to be sent before the network changes
    BaseType_t ret = xTaskCreate(scan_start_task, "scan_start", 4096, NULL, 5, &s_scan_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scan task");
        s_scanning = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Scan task created - returning success immediately");
    return ESP_OK;
}

// Async task to stop scan and return to production mesh
static void scan_stop_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Stop scan task started - returning to production mesh...");

    // Small delay to allow HTTP response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));

    xTimerStop(s_scan_timer, 0);

    // Switch back to production mesh
    ESP_LOGI(TAG, "Step 1: Stopping discovery mesh...");
    esp_err_t ret = mesh_network_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop discovery mesh: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));

    ESP_LOGI(TAG, "Step 2: Restarting production mesh (OMNIAP)...");
    ret = mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restart production mesh: %s", esp_err_to_name(ret));
    }

    s_current_mode = COMMISSION_MODE_PRODUCTION;

    ESP_LOGI(TAG, "=== BACK TO PRODUCTION MODE - Found %d nodes ===", s_scan_count);

    // Publish scan results to MQTT
    mqtt_publish_scan_results(s_scan_results, s_scan_count);

    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t commissioning_stop_scan(void)
{
    ESP_LOGI(TAG, "=== STOPPING NODE SCAN ===");
    ESP_LOGI(TAG, "  s_scanning=%d, current_mode=%s, task_handle=%p",
             s_scanning,
             s_current_mode == COMMISSION_MODE_DISCOVERY ? "DISCOVERY" : "PRODUCTION",
             (void*)s_scan_task_handle);

    // Always reset scanning flag
    s_scanning = false;

    // Stop the timer if running
    xTimerStop(s_scan_timer, 0);

    // If we're already in production mode, nothing to do
    if (s_current_mode == COMMISSION_MODE_PRODUCTION) {
        ESP_LOGI(TAG, "Already in production mode, nothing to do");
        return ESP_OK;
    }

    // We're in discovery mode - need to switch back to production
    ESP_LOGI(TAG, "Switching from discovery back to production mode...");

    // If a task is already running (start or previous stop), wait for it briefly
    if (s_scan_task_handle != NULL) {
        ESP_LOGW(TAG, "Previous task still running, waiting up to 3 seconds...");
        for (int i = 0; i < 30 && s_scan_task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (s_scan_task_handle != NULL) {
            ESP_LOGW(TAG, "Task still running, proceeding anyway (will overwrite handle)");
        }
    }

    // Create task to do the actual mesh switch (async)
    BaseType_t ret = xTaskCreate(scan_stop_task, "scan_stop", 4096, NULL, 5, &s_scan_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stop scan task, doing it synchronously");
        // Fallback: do it synchronously
        mesh_network_stop();
        vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));
        mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
        s_current_mode = COMMISSION_MODE_PRODUCTION;
        ESP_LOGI(TAG, "=== BACK TO PRODUCTION MODE (synchronous) ===");
    }

    return ESP_OK;
}

bool commissioning_is_scanning(void)
{
    return s_scanning;
}

static void scan_timer_callback(TimerHandle_t timer)
{
    // Safety timeout after 5 minutes - auto-stop to prevent getting stuck
    ESP_LOGW(TAG, "=== SCAN SAFETY TIMEOUT (5 min) - Auto-stopping ===");
    ESP_LOGI(TAG, "Found %d nodes during scan", s_scan_count);

    s_scanning = false;

    // Stop discovery mesh and restart production
    mesh_network_stop();
    vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));
    mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
    s_current_mode = COMMISSION_MODE_PRODUCTION;

    ESP_LOGI(TAG, "=== Back to PRODUCTION mode ===");

    // Publish scan results to MQTT
    mqtt_publish_scan_results(s_scan_results, s_scan_count);
}

// ============================================================================
// Scan Response Handling
// ============================================================================

void commissioning_handle_scan_response(const uint8_t *src_mac, const omniapi_message_t *msg)
{
    if (msg == NULL) return;

    const payload_scan_response_t *resp = (const payload_scan_response_t *)msg->payload;

    ESP_LOGI(TAG, "Scan response from %02X:%02X:%02X:%02X:%02X:%02X",
             resp->mac[0], resp->mac[1], resp->mac[2],
             resp->mac[3], resp->mac[4], resp->mac[5]);
    ESP_LOGI(TAG, "  Device type: 0x%02X, FW: %lu.%lu.%lu, Commissioned: %d, RSSI: %d",
             resp->device_type,
             (unsigned long)((resp->firmware_version >> 16) & 0xFF),
             (unsigned long)((resp->firmware_version >> 8) & 0xFF),
             (unsigned long)(resp->firmware_version & 0xFF),
             resp->commissioned, resp->rssi);

    // Check if we already have this node
    int idx = find_scan_result_by_mac(resp->mac);

    if (idx < 0) {
        // New node - add to list
        if (s_scan_count < MAX_SCAN_RESULTS) {
            idx = s_scan_count++;
        } else {
            ESP_LOGW(TAG, "Scan results full, ignoring new node");
            return;
        }
    }

    // Update node info
    memcpy(s_scan_results[idx].mac, resp->mac, 6);
    s_scan_results[idx].device_type = resp->device_type;
    s_scan_results[idx].rssi = resp->rssi;
    s_scan_results[idx].commissioned = resp->commissioned;
    s_scan_results[idx].last_seen = esp_timer_get_time() / 1000;  // Convert to ms

    // Format firmware version
    snprintf(s_scan_results[idx].firmware_version,
             sizeof(s_scan_results[idx].firmware_version),
             "%lu.%lu.%lu",
             (unsigned long)((resp->firmware_version >> 16) & 0xFF),
             (unsigned long)((resp->firmware_version >> 8) & 0xFF),
             (unsigned long)(resp->firmware_version & 0xFF));
}

int commissioning_get_scan_results(scan_result_t *results, int max_results)
{
    if (results == NULL || max_results <= 0) {
        return 0;
    }

    int count = (s_scan_count < max_results) ? s_scan_count : max_results;
    memcpy(results, s_scan_results, count * sizeof(scan_result_t));

    return count;
}

// ============================================================================
// Add discovered node from NODE_ANNOUNCE (when SCAN_RESPONSE not received)
// ============================================================================

void commissioning_add_discovered_node(const uint8_t *mac, uint8_t device_type,
                                        uint32_t firmware_version, bool commissioned)
{
    if (mac == NULL) return;

    // Only add uncommissioned nodes
    if (commissioned) {
        ESP_LOGD(TAG, "Node already commissioned, skipping discovered list");
        return;
    }

    // Check if already in list
    int idx = find_scan_result_by_mac(mac);

    if (idx < 0) {
        // New node - add to list
        if (s_scan_count < MAX_SCAN_RESULTS) {
            idx = s_scan_count++;
            ESP_LOGI(TAG, "=== NODE ADDED TO DISCOVERED (from announce) ===");
        } else {
            ESP_LOGW(TAG, "Scan results full, ignoring new node");
            return;
        }
    } else {
        ESP_LOGI(TAG, "=== NODE UPDATED IN DISCOVERED ===");
    }

    // Update node info
    memcpy(s_scan_results[idx].mac, mac, 6);
    s_scan_results[idx].device_type = device_type;
    s_scan_results[idx].rssi = 0;  // Not available from announce
    s_scan_results[idx].commissioned = 0;
    s_scan_results[idx].last_seen = esp_timer_get_time() / 1000;

    // Format firmware version
    snprintf(s_scan_results[idx].firmware_version,
             sizeof(s_scan_results[idx].firmware_version),
             "%lu.%lu.%lu",
             (unsigned long)((firmware_version >> 16) & 0xFF),
             (unsigned long)((firmware_version >> 8) & 0xFF),
             (unsigned long)(firmware_version & 0xFF));

    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "  Type: 0x%02X, FW: %s",
             device_type, s_scan_results[idx].firmware_version);
    ESP_LOGI(TAG, "  Total discovered: %d", s_scan_count);
}

static int find_scan_result_by_mac(const uint8_t *mac)
{
    for (int i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan_results[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void cleanup_old_results(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    int write_idx = 0;

    for (int i = 0; i < s_scan_count; i++) {
        if ((now - s_scan_results[i].last_seen) < SCAN_CLEANUP_TIMEOUT_MS) {
            if (write_idx != i) {
                s_scan_results[write_idx] = s_scan_results[i];
            }
            write_idx++;
        }
    }

    if (write_idx < s_scan_count) {
        ESP_LOGD(TAG, "Cleaned up %d old scan results", s_scan_count - write_idx);
        s_scan_count = write_idx;
    }
}

// ============================================================================
// Node Commissioning
// ============================================================================

esp_err_t commissioning_add_node(const uint8_t *mac, const char *node_name)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "=== COMMISSIONING NODE: %02X:%02X:%02X:%02X:%02X:%02X ===",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "  credentials_set=%d, current_mode=%s",
             s_credentials_set,
             s_current_mode == COMMISSION_MODE_DISCOVERY ? "DISCOVERY" : "PRODUCTION");

    if (!s_credentials_set) {
        ESP_LOGE(TAG, "Network credentials not set!");
        return ESP_ERR_INVALID_STATE;
    }

    bool need_mesh_switch = (s_current_mode == COMMISSION_MODE_PRODUCTION);
    ESP_LOGI(TAG, "  need_mesh_switch=%d", need_mesh_switch);
    esp_err_t ret = ESP_OK;

    // If we're in production mode, temporarily switch to discovery
    if (need_mesh_switch) {
        ESP_LOGI(TAG, "Switching to discovery mesh to reach uncommissioned node...");

        ret = mesh_network_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop mesh: %s", esp_err_to_name(ret));
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));

        ret = mesh_network_start_with_id(MESH_ID_DISC, MESH_PASSWORD_DISCOVERY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start discovery mesh: %s", esp_err_to_name(ret));
            mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
            return ret;
        }

        s_current_mode = COMMISSION_MODE_DISCOVERY;
        vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS * 3));  // Wait for node to reconnect
    }

    // Build commission message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_COMMISSION, ++s_current_seq, sizeof(payload_commission_t));

    payload_commission_t *cmd = (payload_commission_t *)msg.payload;
    memcpy(cmd->mac, mac, 6);
    memcpy(cmd->network_id, s_network_id, 6);
    strncpy(cmd->network_key, s_network_key, 32);
    strncpy(cmd->plant_id, s_plant_id, 32);

    if (node_name != NULL) {
        strncpy(cmd->node_name, node_name, 32);
    } else {
        snprintf(cmd->node_name, 32, "Node_%02X%02X%02X", mac[3], mac[4], mac[5]);
    }

    ESP_LOGI(TAG, "Sending commission command:");
    ESP_LOGI(TAG, "  Production Network ID: %02X:%02X:%02X:%02X:%02X:%02X",
             cmd->network_id[0], cmd->network_id[1], cmd->network_id[2],
             cmd->network_id[3], cmd->network_id[4], cmd->network_id[5]);
    ESP_LOGI(TAG, "  Plant ID: %s", cmd->plant_id);
    ESP_LOGI(TAG, "  Node Name: %s", cmd->node_name);

    // Send to specific node
    mesh_addr_t addr;
    memcpy(addr.addr, mac, 6);

    ret = mesh_network_send(addr.addr, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_commission_t)));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send commission command: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Commission command sent - waiting for ACK...");
    }

    // Wait a bit for ACK, then switch back if needed
    if (need_mesh_switch) {
        vTaskDelay(pdMS_TO_TICKS(3000));  // Wait for ACK

        ESP_LOGI(TAG, "Switching back to production mesh...");
        mesh_network_stop();
        vTaskDelay(pdMS_TO_TICKS(MESH_SWITCH_DELAY_MS));
        mesh_network_start_with_id(MESH_ID_PROD, MESH_PASSWORD_PRODUCTION);
        s_current_mode = COMMISSION_MODE_PRODUCTION;
    }

    return ret;
}

esp_err_t commissioning_remove_node(const uint8_t *mac)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Decommissioning node: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Build decommission message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_DECOMMISSION, ++s_current_seq, sizeof(payload_decommission_t));

    payload_decommission_t *cmd = (payload_decommission_t *)msg.payload;
    memcpy(cmd->mac, mac, 6);

    // Send to specific node
    mesh_addr_t addr;
    memcpy(addr.addr, mac, 6);

    return mesh_network_send(addr.addr, (uint8_t *)&msg, OMNIAPI_MSG_SIZE(sizeof(payload_decommission_t)));
}

// ============================================================================
// ACK Handling
// ============================================================================

void commissioning_handle_commission_ack(const uint8_t *src_mac, const omniapi_message_t *msg)
{
    if (msg == NULL) return;

    const payload_commission_ack_t *ack = (const payload_commission_ack_t *)msg->payload;

    if (ack->status == 0) {
        ESP_LOGI(TAG, "Commission ACK (SUCCESS) from %02X:%02X:%02X:%02X:%02X:%02X",
                 ack->mac[0], ack->mac[1], ack->mac[2],
                 ack->mac[3], ack->mac[4], ack->mac[5]);

        // Update scan result
        int idx = find_scan_result_by_mac(ack->mac);
        if (idx >= 0) {
            s_scan_results[idx].commissioned = 1;
        }

        // Add to node manager immediately
        // The node will connect to production mesh shortly, but we add it now
        // so it appears in the UI right away
        ESP_LOGI(TAG, "Adding commissioned node to node_manager");
        node_manager_add_node(ack->mac);

        // Publish to MQTT
        mqtt_publish_commission_result(ack->mac, true, "Node commissioned successfully");
    } else {
        ESP_LOGW(TAG, "Commission ACK (FAILED) from %02X:%02X:%02X:%02X:%02X:%02X status=%d",
                 ack->mac[0], ack->mac[1], ack->mac[2],
                 ack->mac[3], ack->mac[4], ack->mac[5], ack->status);

        mqtt_publish_commission_result(ack->mac, false, "Commissioning failed");
    }
}

void commissioning_handle_decommission_ack(const uint8_t *src_mac, const omniapi_message_t *msg)
{
    if (msg == NULL) return;

    const payload_decommission_ack_t *ack = (const payload_decommission_ack_t *)msg->payload;

    if (ack->status == 0) {
        ESP_LOGI(TAG, "Decommission ACK (SUCCESS) from %02X:%02X:%02X:%02X:%02X:%02X",
                 ack->mac[0], ack->mac[1], ack->mac[2],
                 ack->mac[3], ack->mac[4], ack->mac[5]);

        // Remove from scan results
        int idx = find_scan_result_by_mac(ack->mac);
        if (idx >= 0) {
            // Shift remaining results
            for (int i = idx; i < s_scan_count - 1; i++) {
                s_scan_results[i] = s_scan_results[i + 1];
            }
            s_scan_count--;
        }

        mqtt_publish_decommission_result(ack->mac, true, "Node decommissioned successfully");
    } else {
        ESP_LOGW(TAG, "Decommission ACK (FAILED) from %02X:%02X:%02X:%02X:%02X:%02X status=%d",
                 ack->mac[0], ack->mac[1], ack->mac[2],
                 ack->mac[3], ack->mac[4], ack->mac[5], ack->status);

        mqtt_publish_decommission_result(ack->mac, false, "Decommissioning failed");
    }
}

// ============================================================================
// Identify
// ============================================================================

esp_err_t commissioning_identify_node(const uint8_t *mac)
{
    if (mac == NULL) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Identifying node: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Build identify message
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_IDENTIFY, ++s_current_seq, 6);
    memcpy(msg.payload, mac, 6);  // Target MAC in payload

    // Broadcast identify (node will check if MAC matches)
    return mesh_network_broadcast((uint8_t *)&msg, OMNIAPI_MSG_SIZE(6));
}
