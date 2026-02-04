/**
 * OmniaPi Gateway Mesh - Mesh Network Implementation
 *
 * ESP-WIFI-MESH Fixed Root implementation
 */

#include "mesh_network.h"
#include "omniapi_protocol.h"
#include "config_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "esp_netif.h"

static const char *TAG = "MESH_NET";

// ============================================================================
// Configuration
// ============================================================================
static const uint8_t MESH_ID[6] = MESH_ID_DEFAULT;

#define RX_BUFFER_SIZE      1500
#define TX_BUFFER_SIZE      1460
#define RX_QUEUE_SIZE       16

// ============================================================================
// State
// ============================================================================
static bool s_mesh_initialized = false;
static bool s_mesh_started = false;
static bool s_is_root = false;
static int s_mesh_layer = -1;
static mesh_addr_t s_parent_addr = {0};
static esp_netif_t *s_netif_sta = NULL;

static uint8_t s_rx_buffer[RX_BUFFER_SIZE];
static uint8_t s_tx_buffer[TX_BUFFER_SIZE];

// Statistics
static mesh_stats_t s_stats = {0};

// Sequence number for messages
static uint8_t s_seq_num = 0;

// ============================================================================
// Callbacks
// ============================================================================
static void (*s_started_cb)(void) = NULL;
static void (*s_root_cb)(bool is_root) = NULL;
static void (*s_child_connected_cb)(const uint8_t *mac) = NULL;
static void (*s_child_disconnected_cb)(const uint8_t *mac) = NULL;
static mesh_rx_cb_t s_rx_cb = NULL;
static void (*s_router_cb)(bool connected) = NULL;
static bool s_router_connected = false;

void mesh_network_set_started_cb(void (*cb)(void)) { s_started_cb = cb; }
void mesh_network_set_root_cb(void (*cb)(bool)) { s_root_cb = cb; }
void mesh_network_set_child_connected_cb(void (*cb)(const uint8_t *)) { s_child_connected_cb = cb; }
void mesh_network_set_child_disconnected_cb(void (*cb)(const uint8_t *)) { s_child_disconnected_cb = cb; }
void mesh_network_set_rx_cb(mesh_rx_cb_t cb) { s_rx_cb = cb; }
void mesh_network_set_router_cb(void (*cb)(bool connected)) { s_router_cb = cb; }

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
            ESP_LOGI(TAG, "<MESH_EVENT_STARTED> ID:%02X:%02X:%02X:%02X:%02X:%02X",
                     id.addr[0], id.addr[1], id.addr[2], id.addr[3], id.addr[4], id.addr[5]);
            s_mesh_started = true;
            s_mesh_layer = esp_mesh_get_layer();
            if (s_started_cb) s_started_cb();
            break;
        }

        case MESH_EVENT_STOPPED: {
            ESP_LOGI(TAG, "<MESH_EVENT_STOPPED>");
            s_mesh_started = false;
            s_mesh_layer = -1;
            break;
        }

        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t *child = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_CHILD_CONNECTED> aid:%d, %02X:%02X:%02X:%02X:%02X:%02X",
                     child->aid, child->mac[0], child->mac[1], child->mac[2],
                     child->mac[3], child->mac[4], child->mac[5]);
            if (s_child_connected_cb) s_child_connected_cb(child->mac);
            break;
        }

        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t *child = (mesh_event_child_disconnected_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_CHILD_DISCONNECTED> aid:%d, %02X:%02X:%02X:%02X:%02X:%02X",
                     child->aid, child->mac[0], child->mac[1], child->mac[2],
                     child->mac[3], child->mac[4], child->mac[5]);
            if (s_child_disconnected_cb) s_child_disconnected_cb(child->mac);
            break;
        }

        case MESH_EVENT_ROUTING_TABLE_ADD: {
            mesh_event_routing_table_change_t *rt = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_ROUTING_TABLE_ADD> +%d nodes, total:%d",
                     rt->rt_size_change, rt->rt_size_new);
            s_stats.routing_table_size = rt->rt_size_new;
            break;
        }

        case MESH_EVENT_ROUTING_TABLE_REMOVE: {
            mesh_event_routing_table_change_t *rt = (mesh_event_routing_table_change_t *)event_data;
            ESP_LOGW(TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE> -%d nodes, total:%d",
                     rt->rt_size_change, rt->rt_size_new);
            s_stats.routing_table_size = rt->rt_size_new;
            break;
        }

        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
            s_mesh_layer = connected->self_layer;
            memcpy(&s_parent_addr.addr, connected->connected.bssid, 6);

            s_is_root = esp_mesh_is_root();
            ESP_LOGI(TAG, "<MESH_EVENT_PARENT_CONNECTED> layer:%d, parent:%02X:%02X:%02X:%02X:%02X:%02X %s",
                     s_mesh_layer, s_parent_addr.addr[0], s_parent_addr.addr[1],
                     s_parent_addr.addr[2], s_parent_addr.addr[3], s_parent_addr.addr[4],
                     s_parent_addr.addr[5], s_is_root ? "<ROOT>" : "");

            // If we're root, restart DHCP to get IP from router
            if (s_is_root && s_netif_sta) {
                esp_netif_dhcpc_stop(s_netif_sta);
                esp_netif_dhcpc_start(s_netif_sta);
                ESP_LOGI(TAG, "Root reconnected to router - DHCP restarted");
            }

            if (s_root_cb) s_root_cb(s_is_root);
            break;
        }

        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t *disc = (mesh_event_disconnected_t *)event_data;
            ESP_LOGW(TAG, "<MESH_EVENT_PARENT_DISCONNECTED> reason:%d", disc->reason);
            s_mesh_layer = esp_mesh_get_layer();

            // Notify mesh nodes that external network is unreachable
            esp_mesh_post_toDS_state(false);

            // Track router state and notify application
            if (s_router_connected) {
                s_router_connected = false;
                if (s_router_cb) s_router_cb(false);
            }

            ESP_LOGW(TAG, "Router disconnected - mesh will auto-reconnect");
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
            ESP_LOGI(TAG, "<MESH_EVENT_ROOT_ADDRESS> root:%02X:%02X:%02X:%02X:%02X:%02X",
                     root->addr[0], root->addr[1], root->addr[2],
                     root->addr[3], root->addr[4], root->addr[5]);
            break;
        }

        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t *state = (mesh_event_toDS_state_t *)event_data;
            ESP_LOGI(TAG, "<MESH_EVENT_TODS_STATE> state:%d", *state);
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
            ESP_LOGW(TAG, "<MESH_EVENT_NO_PARENT_FOUND> scan:%d", np->scan_times);
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

        // Notify mesh that we can reach external network
        esp_mesh_post_toDS_state(true);

        // Track router state and notify application
        s_router_connected = true;
        if (s_router_cb) s_router_cb(true);
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "<IP_EVENT_STA_LOST_IP> Lost IP address from router");

        // Notify mesh that external network is unreachable
        esp_mesh_post_toDS_state(false);

        // Track router state and notify application
        if (s_router_connected) {
            s_router_connected = false;
            if (s_router_cb) s_router_cb(false);
        }
    }
}

// ============================================================================
// Initialization
// ============================================================================

esp_err_t mesh_network_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-WIFI-MESH as Fixed Root...");

    if (s_mesh_initialized) {
        ESP_LOGW(TAG, "Mesh already initialized");
        return ESP_OK;
    }

    // Create mesh netif
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&s_netif_sta, NULL));

    // Initialize WiFi
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // Register IP event handler (all IP events for reconnect handling)
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
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
    ESP_LOGI(TAG, "Mesh initialized");
    return ESP_OK;
}

esp_err_t mesh_network_start(void)
{
    ESP_LOGI(TAG, "Starting mesh network...");

    if (!s_mesh_initialized) {
        ESP_LOGE(TAG, "Mesh not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    // Configure mesh topology (tree)
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));

    // Set max layer
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));

    // Set vote percentage (we're fixed root, but just in case)
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));

    // Set XON queue size
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));

    // Disable mesh PS (Power Save) for better latency
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));

    // Configure mesh
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();

    // Mesh ID
    memcpy(&cfg.mesh_id, MESH_ID, 6);

    // Router configuration (for external network access)
    cfg.channel = CONFIG_MESH_CHANNEL;

    // Use config_manager for WiFi credentials (NVS or defaults)
    const config_wifi_sta_t *wifi_sta = config_get_wifi_sta();
    if (wifi_sta && strlen(wifi_sta->ssid) > 0) {
        cfg.router.ssid_len = strlen(wifi_sta->ssid);
        memcpy(&cfg.router.ssid, wifi_sta->ssid, cfg.router.ssid_len);
        memcpy(&cfg.router.password, wifi_sta->password, strlen(wifi_sta->password));
        ESP_LOGI(TAG, "Router configured: SSID=%s", wifi_sta->ssid);
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured - mesh will not connect to router");
    }

    // Mesh softAP configuration (for child nodes)
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
#ifdef CONFIG_MESH_AP_PASSWD
    memcpy(&cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD, strlen(CONFIG_MESH_AP_PASSWD));
#endif

    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    // *** FIXED ROOT CONFIGURATION ***
    // This device is ALWAYS the root
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));

    // Start mesh
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(TAG, "Mesh started as FIXED ROOT");
    ESP_LOGI(TAG, "  Mesh ID: %02X:%02X:%02X:%02X:%02X:%02X",
             MESH_ID[0], MESH_ID[1], MESH_ID[2], MESH_ID[3], MESH_ID[4], MESH_ID[5]);
    ESP_LOGI(TAG, "  Channel: %d", CONFIG_MESH_CHANNEL);
    ESP_LOGI(TAG, "  Max Layer: %d", CONFIG_MESH_MAX_LAYER);
    ESP_LOGI(TAG, "  Max Connections: %d", CONFIG_MESH_AP_CONNECTIONS);

    return ESP_OK;
}

esp_err_t mesh_network_stop(void)
{
    ESP_LOGI(TAG, "Stopping mesh network...");

    if (s_mesh_started) {
        esp_mesh_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Unregister event handler before deinit
    esp_event_handler_unregister(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler);

    // DEINIT mesh completely
    if (s_mesh_initialized) {
        ESP_LOGI(TAG, "Deinitializing mesh...");
        esp_mesh_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));
        s_mesh_initialized = false;
    }

    s_mesh_started = false;
    s_is_root = false;

    ESP_LOGI(TAG, "Mesh stopped and deinitialized");
    return ESP_OK;
}

// Forward declaration for event handler
static void mesh_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

esp_err_t mesh_network_start_with_id(const uint8_t mesh_id[6], const char *password)
{
    ESP_LOGI(TAG, "=== Starting mesh with ID: %02X:%02X:%02X:%02X:%02X:%02X ===",
             mesh_id[0], mesh_id[1], mesh_id[2], mesh_id[3], mesh_id[4], mesh_id[5]);

    esp_err_t ret;

    // 1. Stop and deinit if already running
    if (s_mesh_started || s_mesh_initialized) {
        ESP_LOGI(TAG, "Step 1: Stopping current mesh...");
        if (s_mesh_started) {
            esp_mesh_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            s_mesh_started = false;
        }
        if (s_mesh_initialized) {
            esp_mesh_deinit();
            vTaskDelay(pdMS_TO_TICKS(200));
            s_mesh_initialized = false;
        }
    }

    // 2. RE-INIT mesh
    ESP_LOGI(TAG, "Step 2: Initializing mesh...");
    ret = esp_mesh_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mesh_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mesh_initialized = true;

    // 3. Register event handler
    ESP_LOGI(TAG, "Step 3: Registering event handler...");
    ret = esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Configure mesh topology
    ESP_LOGI(TAG, "Step 4: Configuring mesh topology...");
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));

    // 5. Configure mesh with custom ID
    ESP_LOGI(TAG, "Step 5: Configuring mesh parameters...");
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy(&cfg.mesh_id, mesh_id, 6);

    // Router configuration - use config_manager
    cfg.channel = CONFIG_MESH_CHANNEL;
    const config_wifi_sta_t *wifi_cfg = config_get_wifi_sta();
    if (wifi_cfg && strlen(wifi_cfg->ssid) > 0) {
        cfg.router.ssid_len = strlen(wifi_cfg->ssid);
        memcpy(&cfg.router.ssid, wifi_cfg->ssid, cfg.router.ssid_len);
        memcpy(&cfg.router.password, wifi_cfg->password, strlen(wifi_cfg->password));
        ESP_LOGI(TAG, "Custom mesh: Router SSID=%s", wifi_cfg->ssid);
    }

    // Mesh softAP with custom password
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = 0;
    if (password) {
        memcpy(&cfg.mesh_ap.password, password, strlen(password));
    }

    ret = esp_mesh_set_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mesh_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Fixed root configuration
    ESP_LOGI(TAG, "Step 6: Setting as FIXED ROOT...");
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));

    // 7. Start mesh
    ESP_LOGI(TAG, "Step 7: Starting mesh...");
    ret = esp_mesh_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mesh_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "=== Mesh started with custom ID as FIXED ROOT ===");
    return ESP_OK;
}

// ============================================================================
// Messaging
// ============================================================================

esp_err_t mesh_network_send(const uint8_t *dest_mac, const uint8_t *data, size_t len)
{
    if (!s_mesh_started || !s_is_root) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dest_mac == NULL || data == NULL || len == 0 || len > TX_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_addr_t dest;
    memcpy(dest.addr, dest_mac, 6);

    mesh_data_t mesh_data = {
        .data = (uint8_t *)data,
        .size = len,
        .proto = MESH_PROTO_BIN,
        .tos = MESH_TOS_P2P,
    };

    ESP_LOGD(TAG, "Sending %u bytes to %02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)len, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);

    esp_err_t ret = esp_mesh_send(&dest, &mesh_data, MESH_DATA_P2P | MESH_DATA_FROMDS, NULL, 0);

    if (ret == ESP_OK) {
        s_stats.tx_count++;
        ESP_LOGD(TAG, "Send OK to %02X:%02X:%02X:%02X:%02X:%02X (%u bytes)",
                 dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], (unsigned)len);
    } else {
        s_stats.tx_errors++;
        ESP_LOGE(TAG, "Send FAILED to %02X:%02X:%02X:%02X:%02X:%02X: %s (len=%u)",
                 dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5],
                 esp_err_to_name(ret), (unsigned)len);
    }

    return ret;
}

esp_err_t mesh_network_broadcast(const uint8_t *data, size_t len)
{
    if (!s_mesh_started || !s_is_root) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get routing table
    mesh_addr_t route_table[MESH_MAX_ROUTING_TABLE];
    int table_size = 0;

    esp_err_t ret = esp_mesh_get_routing_table(route_table, MESH_MAX_ROUTING_TABLE * 6, &table_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get routing table: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "Broadcasting to %d nodes", table_size);

    mesh_data_t mesh_data = {
        .data = (uint8_t *)data,
        .size = len,
        .proto = MESH_PROTO_BIN,
        .tos = MESH_TOS_P2P,
    };

    int success = 0;
    for (int i = 0; i < table_size; i++) {
        ret = esp_mesh_send(&route_table[i], &mesh_data, MESH_DATA_P2P | MESH_DATA_FROMDS, NULL, 0);
        if (ret == ESP_OK) {
            success++;
            s_stats.tx_count++;
        } else {
            s_stats.tx_errors++;
        }
    }

    ESP_LOGD(TAG, "Broadcast complete: %d/%d success", success, table_size);
    return (success > 0) ? ESP_OK : ESP_FAIL;
}

void mesh_network_broadcast_heartbeat(void)
{
    omniapi_message_t msg;
    OMNIAPI_INIT_HEADER(&msg.header, MSG_HEARTBEAT, s_seq_num++, 0);

    mesh_network_broadcast((uint8_t *)&msg, OMNIAPI_MSG_SIZE(0));
}

void mesh_network_process_rx(void)
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
        s_stats.rx_count++;

        ESP_LOGD(TAG, "RX from %02X:%02X:%02X:%02X:%02X:%02X len=%d",
                 from.addr[0], from.addr[1], from.addr[2], from.addr[3],
                 from.addr[4], from.addr[5], (int)data.size);

        // Call application callback
        if (s_rx_cb) {
            s_rx_cb(from.addr, data.data, data.size);
        }
    } else if (ret != ESP_ERR_MESH_TIMEOUT && ret != ESP_OK) {
        s_stats.rx_errors++;
    }
}

// ============================================================================
// Status
// ============================================================================

bool mesh_network_is_started(void)
{
    return s_mesh_started;
}

bool mesh_network_is_node_reachable(const uint8_t *mac)
{
    if (!s_mesh_started || mac == NULL) {
        return false;
    }

    mesh_addr_t route_table[MESH_MAX_ROUTING_TABLE];
    int table_size = 0;

    esp_err_t ret = esp_mesh_get_routing_table(route_table, MESH_MAX_ROUTING_TABLE * 6, &table_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get routing table: %s", esp_err_to_name(ret));
        return false;
    }

    for (int i = 0; i < table_size; i++) {
        if (memcmp(route_table[i].addr, mac, 6) == 0) {
            return true;
        }
    }

    return false;
}

bool mesh_network_is_root(void)
{
    return s_is_root;
}

int mesh_network_get_layer(void)
{
    return s_mesh_layer;
}

int mesh_network_get_node_count(void)
{
    return esp_mesh_get_routing_table_size();
}

int mesh_network_get_routing_table(mesh_addr_t *table, int max_nodes)
{
    int size = 0;
    esp_mesh_get_routing_table(table, max_nodes * 6, &size);
    return size;
}

void mesh_network_get_id(uint8_t *mesh_id)
{
    mesh_addr_t id;
    esp_mesh_get_id(&id);
    memcpy(mesh_id, id.addr, 6);
}

void mesh_network_get_stats(mesh_stats_t *stats)
{
    if (stats) {
        memcpy(stats, &s_stats, sizeof(mesh_stats_t));
        stats->routing_table_size = esp_mesh_get_routing_table_size();
    }
}

esp_netif_t *mesh_network_get_sta_netif(void)
{
    return s_netif_sta;
}
