/**
 * OmniaPi Node Mesh - Mesh Node Implementation
 *
 * ESP-WIFI-MESH node (non-root) implementation
 * Supports discovery mesh (uncommissioned) and production mesh (commissioned)
 */

#include "mesh_node.h"
#include "commissioning.h"
#include "omniapi_protocol.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_netif.h"

static const char *TAG = "MESH_NODE";

// ============================================================================
// Configuration
// ============================================================================

// Discovery mesh (for uncommissioned nodes)
static const uint8_t MESH_ID_DISCOVERY_DEFAULT[6] = MESH_ID_DISCOVERY;
static const char *MESH_PASSWORD_DISCOVERY_DEFAULT = MESH_PASSWORD_DISCOVERY;

#define RX_BUFFER_SIZE      1500
#define TX_BUFFER_SIZE      1460

// ============================================================================
// State
// ============================================================================
static bool s_mesh_initialized = false;
static bool s_mesh_started = false;
static bool s_connected = false;
static int s_mesh_layer = -1;
static mesh_addr_t s_parent_addr = {0};
static mesh_addr_t s_root_addr = {0};
static int8_t s_parent_rssi = 0;
static esp_netif_t *s_netif_sta = NULL;

static uint8_t s_rx_buffer[RX_BUFFER_SIZE];

// Current mesh credentials (discovery or production)
static uint8_t s_current_mesh_id[6] = {0};
static char s_current_mesh_password[33] = {0};
static bool s_is_production_mesh = false;

// ============================================================================
// Callbacks
// ============================================================================
static void (*s_connected_cb)(void) = NULL;
static void (*s_disconnected_cb)(void) = NULL;
static mesh_node_rx_cb_t s_rx_cb = NULL;

void mesh_node_set_connected_cb(void (*cb)(void)) { s_connected_cb = cb; }
void mesh_node_set_disconnected_cb(void (*cb)(void)) { s_disconnected_cb = cb; }
void mesh_node_set_rx_cb(mesh_node_rx_cb_t cb) { s_rx_cb = cb; }

// ============================================================================
// Event Handlers
// ============================================================================

static void mesh_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case MESH_EVENT_STARTED: {
            mesh_addr_t id = {0};
            esp_mesh_get_id(&id);
            ESP_LOGI(TAG, "<MESH_EVENT_STARTED> ID:%02X:%02X:%02X:%02X:%02X:%02X (%s)",
                     id.addr[0], id.addr[1], id.addr[2], id.addr[3], id.addr[4], id.addr[5],
                     s_is_production_mesh ? "PRODUCTION" : "DISCOVERY");
            s_mesh_started = true;
            s_mesh_layer = esp_mesh_get_layer();
            break;
        }

        case MESH_EVENT_STOPPED: {
            ESP_LOGI(TAG, "<MESH_EVENT_STOPPED>");
            s_mesh_started = false;
            s_connected = false;
            s_mesh_layer = -1;
            break;
        }

        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t *child = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_CHILD_CONNECTED> aid:%d, %02X:%02X:%02X:%02X:%02X:%02X",
                     child->aid, child->mac[0], child->mac[1], child->mac[2],
                     child->mac[3], child->mac[4], child->mac[5]);
            break;
        }

        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t *child = (mesh_event_child_disconnected_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_CHILD_DISCONNECTED> aid:%d, %02X:%02X:%02X:%02X:%02X:%02X",
                     child->aid, child->mac[0], child->mac[1], child->mac[2],
                     child->mac[3], child->mac[4], child->mac[5]);
            break;
        }

        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t *rt = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGD(TAG, "<MESH_EVENT_ROUTING_TABLE_ADD> +%d nodes, total:%d",
                     rt->rt_size_change, rt->rt_size_new);
            break;
        }

        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t *rt = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGD(TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE> -%d nodes, total:%d",
                     rt->rt_size_change, rt->rt_size_new);
            break;
        }

        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
            s_mesh_layer = connected->self_layer;
            memcpy(&s_parent_addr.addr, connected->connected.bssid, 6);

            ESP_LOGI(TAG, "<MESH_EVENT_PARENT_CONNECTED> layer:%d, parent:%02X:%02X:%02X:%02X:%02X:%02X (%s)",
                     s_mesh_layer, s_parent_addr.addr[0], s_parent_addr.addr[1],
                     s_parent_addr.addr[2], s_parent_addr.addr[3], s_parent_addr.addr[4],
                     s_parent_addr.addr[5],
                     s_is_production_mesh ? "PRODUCTION" : "DISCOVERY");

            s_connected = true;
            if (s_connected_cb) s_connected_cb();
            break;
        }

        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t *disc = (mesh_event_disconnected_t *)event_data;
            ESP_LOGW(TAG, "<MESH_EVENT_PARENT_DISCONNECTED> reason:%d", disc->reason);
            s_connected = false;
            s_mesh_layer = -1;
            if (s_disconnected_cb) s_disconnected_cb();
            break;
        }

        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t *layer = (mesh_event_layer_change_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_LAYER_CHANGE> %d -> %d",
                     s_mesh_layer, layer->new_layer);
            s_mesh_layer = layer->new_layer;
            break;
        }

        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t *root = (mesh_event_root_address_t *)event_data;
            memcpy(&s_root_addr.addr, root->addr, 6);
            ESP_LOGI(TAG, "<MESH_EVENT_ROOT_ADDRESS> root:%02X:%02X:%02X:%02X:%02X:%02X",
                     root->addr[0], root->addr[1], root->addr[2],
                     root->addr[3], root->addr[4], root->addr[5]);
            break;
        }

        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t *state = (mesh_event_toDS_state_t *)event_data;
            ESP_LOGD(TAG, "<MESH_EVENT_TODS_STATE> state:%d", *state);
            break;
        }

        case MESH_EVENT_ROOT_FIXED: {
            mesh_event_root_fixed_t *fixed = (mesh_event_root_fixed_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_ROOT_FIXED> %s",
                     fixed->is_fixed ? "FIXED" : "NOT FIXED");
            break;
        }

        case MESH_EVENT_NO_PARENT_FOUND: {
            mesh_event_no_parent_found_t *np = (mesh_event_no_parent_found_t *)event_data;
            ESP_LOGW(TAG, "<MESH_EVENT_NO_PARENT_FOUND> scan:%d (%s mesh)",
                     np->scan_times,
                     s_is_production_mesh ? "PRODUCTION" : "DISCOVERY");
            break;
        }

        case MESH_EVENT_SCAN_DONE: {
            mesh_event_scan_done_t *scan = (mesh_event_scan_done_t *)event_data;
            ESP_LOGD(TAG, "<MESH_EVENT_SCAN_DONE> number:%d", scan->number);
            break;
        }

        default:
            ESP_LOGD(TAG, "Mesh event %ld", event_id);
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "<IP_EVENT_STA_GOT_IP> IP:%d.%d.%d.%d",
                 esp_ip4_addr1_16(&event->ip_info.ip), esp_ip4_addr2_16(&event->ip_info.ip),
                 esp_ip4_addr3_16(&event->ip_info.ip), esp_ip4_addr4_16(&event->ip_info.ip));
    }
}

// ============================================================================
// Initialization
// ============================================================================

esp_err_t mesh_node_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-WIFI-MESH as Node...");

    if (s_mesh_initialized) {
        ESP_LOGW(TAG, "Mesh already initialized");
        return ESP_OK;
    }

    // Create mesh netif
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&s_netif_sta, NULL));

    // Initialize WiFi
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // Register IP event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &ip_event_handler, NULL));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize mesh
    ESP_ERROR_CHECK(esp_mesh_init());

    // Register mesh event handler
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
                                               &mesh_event_handler, NULL));

    s_mesh_initialized = true;
    ESP_LOGI(TAG, "Mesh node initialized");
    return ESP_OK;
}

esp_err_t mesh_node_start(void)
{
    ESP_LOGI(TAG, "Starting mesh node...");

    if (!s_mesh_initialized) {
        ESP_LOGE(TAG, "Mesh not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    // Determine which mesh to join based on commissioning state
    if (commissioning_is_commissioned()) {
        // Use production mesh credentials
        if (commissioning_get_network_credentials(s_current_mesh_id, s_current_mesh_password) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get network credentials!");
            return ESP_FAIL;
        }
        s_is_production_mesh = true;
        ESP_LOGI(TAG, "=== JOINING PRODUCTION MESH ===");
    } else {
        // Use discovery mesh
        memcpy(s_current_mesh_id, MESH_ID_DISCOVERY_DEFAULT, 6);
        strncpy(s_current_mesh_password, MESH_PASSWORD_DISCOVERY_DEFAULT, 32);
        s_current_mesh_password[32] = '\0';
        s_is_production_mesh = false;
        ESP_LOGI(TAG, "=== JOINING DISCOVERY MESH ===");
    }

    ESP_LOGI(TAG, "  Mesh ID: %02X:%02X:%02X:%02X:%02X:%02X",
             s_current_mesh_id[0], s_current_mesh_id[1], s_current_mesh_id[2],
             s_current_mesh_id[3], s_current_mesh_id[4], s_current_mesh_id[5]);

    // Configure mesh topology (tree)
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));

    // Set max layer
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));

    // Set vote percentage
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));

    // Set XON queue size
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));

    // Disable mesh PS (Power Save) for better latency
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));

    // Configure mesh
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();

    // Set mesh ID (discovery or production)
    memcpy(&cfg.mesh_id, s_current_mesh_id, 6);

    // Channel (must match gateway)
    cfg.channel = CONFIG_MESH_CHANNEL;

    // Router configuration (required by ESP-MESH even for non-root nodes)
    memcpy((uint8_t *)&cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, strlen(CONFIG_MESH_ROUTER_SSID));
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *)&cfg.router.password, CONFIG_MESH_ROUTER_PASSWD, strlen(CONFIG_MESH_ROUTER_PASSWD));

    // Mesh softAP configuration (for child nodes to connect)
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
    cfg.mesh_ap.max_connection = 1;  // LEAF node - config requires >= 1, MESH_LEAF type blocks children
    cfg.mesh_ap.nonmesh_max_connection = 0;  // No non-mesh connections on nodes

    // Set mesh password (discovery or production)
    memcpy(&cfg.mesh_ap.password, s_current_mesh_password, strlen(s_current_mesh_password));

    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    // *** NODE CONFIGURATION - ALWAYS LEAF ***
    // This device is NEVER the root - gateway is FIXED ROOT
    // Node must ONLY connect as child, never become root
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_LEAF));   // Force LEAF - never root
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));        // There IS a fixed root (gateway)

    // Self-organized but NEVER try to become root
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, false));

    // Start mesh
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(TAG, "Mesh node started - searching for %s network...",
             s_is_production_mesh ? "PRODUCTION" : "DISCOVERY");
    ESP_LOGI(TAG, "  Channel: %d", CONFIG_MESH_CHANNEL);
    ESP_LOGI(TAG, "  Max Layer: %d", CONFIG_MESH_MAX_LAYER);

    return ESP_OK;
}

esp_err_t mesh_node_stop(void)
{
    ESP_LOGI(TAG, "Stopping mesh node...");
    s_connected = false;
    return esp_mesh_stop();
}

// ============================================================================
// Messaging
// ============================================================================

esp_err_t mesh_node_send_to_root(const uint8_t *data, size_t len)
{
    if (!s_mesh_started || !s_connected) {
        ESP_LOGW(TAG, "Not connected to mesh");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0 || len > TX_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_data_t mesh_data = {
        .data = (uint8_t *)data,
        .size = len,
        .proto = MESH_PROTO_BIN,
        .tos = MESH_TOS_P2P,
    };

    // Send to root (NULL destination = root)
    esp_err_t ret = esp_mesh_send(NULL, &mesh_data, MESH_DATA_TODS, NULL, 0);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Send to root failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

void mesh_node_process_rx(void)
{
    if (!s_mesh_started) return;

    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;

    data.data = s_rx_buffer;
    data.size = RX_BUFFER_SIZE;

    // Non-blocking receive
    esp_err_t ret = esp_mesh_recv(&from, &data, 0, &flag, NULL, 0);

    if (ret == ESP_OK && data.size > 0) {
        ESP_LOGD(TAG, "RX from %02X:%02X:%02X:%02X:%02X:%02X len=%d flag=0x%x",
                 from.addr[0], from.addr[1], from.addr[2], from.addr[3],
                 from.addr[4], from.addr[5], (int)data.size, flag);

        // Call application callback
        if (s_rx_cb) {
            s_rx_cb(from.addr, data.data, data.size);
        }
    }
}

// ============================================================================
// Status
// ============================================================================

bool mesh_node_is_connected(void)
{
    return s_connected;
}

int mesh_node_get_layer(void)
{
    return s_mesh_layer;
}

int8_t mesh_node_get_parent_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_parent_rssi = ap_info.rssi;
    }
    return s_parent_rssi;
}

void mesh_node_get_root_mac(uint8_t *mac)
{
    if (mac) {
        memcpy(mac, s_root_addr.addr, 6);
    }
}

void mesh_node_get_mesh_id(uint8_t *mesh_id)
{
    mesh_addr_t id;
    esp_mesh_get_id(&id);
    if (mesh_id) {
        memcpy(mesh_id, id.addr, 6);
    }
}

bool mesh_node_is_production_mesh(void)
{
    return s_is_production_mesh;
}
