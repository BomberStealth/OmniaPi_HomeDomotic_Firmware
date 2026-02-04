/**
 * OmniaPi Gateway Mesh - Main Application
 *
 * ESP32 Gateway for OmniaPi Home Domotic System
 * Features:
 * - ESP-WIFI-MESH as Fixed Root
 * - Ethernet connectivity (WT32-ETH01)
 * - MQTT communication with backend
 * - Node management and commissioning
 * - OTA updates distribution
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"

#include "lwip/sockets.h"

#include "omniapi_protocol.h"
#include "mesh_network.h"
#include "eth_manager.h"
#include "wifi_manager.h"
#include "mqtt_handler.h"
#include "node_manager.h"
#include "nvs_storage.h"
#include "config_manager.h"
#include "commissioning.h"
#include "ota_manager.h"
#include "node_ota.h"
#include "webserver.h"
#include "web_api.h"
#include "status_led.h"

static const char *TAG = "GATEWAY_MAIN";

// ============================================================================
// Version Info
// ============================================================================
#define GATEWAY_VERSION_STRING  CONFIG_GATEWAY_FIRMWARE_VERSION

// ============================================================================
// Event Groups
// ============================================================================
static EventGroupHandle_t s_gateway_events = NULL;

#define EVENT_ETH_CONNECTED     BIT0
#define EVENT_WIFI_CONNECTED    BIT1
#define EVENT_MQTT_CONNECTED    BIT2
#define EVENT_MESH_STARTED      BIT3
#define EVENT_MESH_ROOT         BIT4

// ============================================================================
// Global State
// ============================================================================
typedef struct {
    bool eth_connected;
    bool wifi_connected;
    bool mqtt_connected;
    bool mesh_started;
    bool is_mesh_root;
    uint8_t gateway_mac[6];
    uint32_t uptime_sec;
    int mesh_nodes_count;
} gateway_state_t;

static gateway_state_t s_state = {0};
static bool s_eth_init_ok = false;  // Track if Ethernet init succeeded
static const char *s_eth_fail_reason = NULL;  // Store ETH init failure reason

// ============================================================================
// Forward Declarations
// ============================================================================
static void gateway_task(void *pvParameters);
static void heartbeat_task(void *pvParameters);
static void status_task(void *pvParameters);
static esp_err_t init_nvs(void);
static esp_err_t init_network(void);
static esp_err_t start_provisioning_ap(void);
static void captive_dns_task(void *pvParameters);
static void print_banner(void);
static void mesh_rx_handler(const uint8_t *src_mac, const uint8_t *data, size_t len);

// ============================================================================
// Mesh Message Router
// ============================================================================

/**
 * Handle incoming mesh messages and route to appropriate handlers
 */
static void mesh_rx_handler(const uint8_t *src_mac, const uint8_t *data, size_t len)
{
    if (len < sizeof(omniapi_header_t)) {
        ESP_LOGW(TAG, "Message too short: %d bytes", (int)len);
        return;
    }

    const omniapi_message_t *msg = (const omniapi_message_t *)data;

    // Validate magic
    if (msg->header.magic != OMNIAPI_MAGIC) {
        ESP_LOGW(TAG, "Invalid magic: 0x%04X", msg->header.magic);
        return;
    }

    // Validate length
    if (len < OMNIAPI_MSG_SIZE(msg->header.payload_len)) {
        ESP_LOGW(TAG, "Payload truncated");
        return;
    }

    ESP_LOGD(TAG, "RX from %02X:%02X:%02X:%02X:%02X:%02X msg_type=0x%02X",
             src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
             msg->header.msg_type);

    // Route message based on type
    switch (msg->header.msg_type) {
        // Ignore messages that gateway sends (mesh echo)
        case MSG_HEARTBEAT:
            // Gateway sends these, ignore if echoed back
            break;

        // Node status messages
        case MSG_HEARTBEAT_ACK:
            node_manager_update_info(src_mac, (const payload_heartbeat_ack_t *)msg->payload);
            break;

        case MSG_NODE_ANNOUNCE: {
            const payload_node_announce_t *announce = (const payload_node_announce_t *)msg->payload;
            ESP_LOGI(TAG, "Node announce: type=%d, commissioned=%d, FW=0x%08lX",
                     announce->device_type, announce->commissioned,
                     (unsigned long)announce->firmware_version);

            if (announce->commissioned) {
                // Commissioned node: add to node_manager for normal operation
                node_manager_add_node(src_mac);
                // Publish to MQTT
                if (s_state.mqtt_connected) {
                    mqtt_publish_node_connected(src_mac);
                }
            } else {
                // Uncommissioned node: add to discovered list (scan results)
                // This handles the case where node connects AFTER scan_request was sent
                ESP_LOGI(TAG, "Uncommissioned node detected - adding to discovered list");
                commissioning_add_discovered_node(
                    announce->mac,
                    announce->device_type,
                    announce->firmware_version,
                    announce->commissioned
                );
            }
            break;
        }

        // Commissioning messages
        case MSG_SCAN_REQUEST:
            // Ignore - gateway sends these, shouldn't receive them
            ESP_LOGD(TAG, "Ignoring scan request (gateway is sender)");
            break;

        case MSG_SCAN_RESPONSE:
            ESP_LOGI(TAG, "=== SCAN RESPONSE RECEIVED ===");
            commissioning_handle_scan_response(src_mac, msg);
            break;

        case MSG_COMMISSION_ACK:
            commissioning_handle_commission_ack(src_mac, msg);
            break;

        case MSG_DECOMMISSION_ACK:
            commissioning_handle_decommission_ack(src_mac, msg);
            break;

        // OTA messages from nodes (pull-mode - node requests chunks)
        case MSG_OTA_REQUEST:
            ota_manager_handle_request(src_mac, (const payload_ota_request_t *)msg->payload);
            break;

        case MSG_OTA_COMPLETE:
            // Handle both pull-mode and push-mode OTA complete
            ota_manager_handle_complete(src_mac, (const payload_ota_complete_t *)msg->payload);
            node_ota_handle_complete(src_mac, (const payload_ota_complete_t *)msg->payload);
            break;

        case MSG_OTA_FAILED:
            // Handle both pull-mode and push-mode OTA failed
            ota_manager_handle_failed(src_mac, (const payload_ota_failed_t *)msg->payload);
            node_ota_handle_failed(src_mac, (const payload_ota_failed_t *)msg->payload);
            break;

        // Push-mode OTA messages (gateway pushes to specific node)
        case MSG_OTA_ACK:
            node_ota_handle_ack(src_mac, (const payload_ota_ack_t *)msg->payload);
            break;

        // Relay/LED status updates
        case MSG_RELAY_STATUS: {
            const payload_relay_status_t *status = (const payload_relay_status_t *)msg->payload;
            ESP_LOGI(TAG, "Relay status: ch=%d state=%d", status->channel, status->state);
            // TODO: Forward to MQTT
            break;
        }

        case MSG_LED_STATUS: {
            const payload_led_status_t *status = (const payload_led_status_t *)msg->payload;
            ESP_LOGI(TAG, "LED status: on=%d r=%d g=%d b=%d brightness=%d",
                     status->on, status->r, status->g, status->b, status->brightness);
            // TODO: Forward to MQTT
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", msg->header.msg_type);
            break;
    }
}

// ============================================================================
// Route Priority Management
// ============================================================================

/**
 * Update default network route based on available connections.
 * Priority: Ethernet > WiFi
 */
static void update_default_route(void)
{
    esp_netif_t *eth_netif = eth_manager_get_netif();
    esp_netif_t *sta_netif = mesh_network_get_sta_netif();

    if (s_state.eth_connected && eth_netif) {
        esp_netif_set_default_netif(eth_netif);
        ESP_LOGI(TAG, "Default route -> ETHERNET");
    } else if (s_state.wifi_connected && sta_netif) {
        esp_netif_set_default_netif(sta_netif);
        ESP_LOGI(TAG, "Default route -> WiFi");
    } else {
        ESP_LOGW(TAG, "No external network available");
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

/**
 * Network connectivity callback (from eth_manager)
 */
void on_network_connected(bool is_ethernet)
{
    if (is_ethernet) {
        s_state.eth_connected = true;
        xEventGroupSetBits(s_gateway_events, EVENT_ETH_CONNECTED);
        ESP_LOGI(TAG, "Ethernet connected");
    } else {
        s_state.wifi_connected = true;
        xEventGroupSetBits(s_gateway_events, EVENT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "WiFi connected");
    }
    update_default_route();
}

void on_network_disconnected(bool is_ethernet)
{
    if (is_ethernet) {
        s_state.eth_connected = false;
        xEventGroupClearBits(s_gateway_events, EVENT_ETH_CONNECTED);
        ESP_LOGW(TAG, "Ethernet disconnected");
    } else {
        s_state.wifi_connected = false;
        xEventGroupClearBits(s_gateway_events, EVENT_WIFI_CONNECTED);
        ESP_LOGW(TAG, "WiFi disconnected");
    }
    update_default_route();
}

/**
 * MQTT connectivity callback
 */
void on_mqtt_connected(void)
{
    s_state.mqtt_connected = true;
    xEventGroupSetBits(s_gateway_events, EVENT_MQTT_CONNECTED);
    ESP_LOGI(TAG, "MQTT connected");

    // Publish gateway online status
    mqtt_publish_gateway_status(true);
}

void on_mqtt_disconnected(void)
{
    s_state.mqtt_connected = false;
    xEventGroupClearBits(s_gateway_events, EVENT_MQTT_CONNECTED);
    ESP_LOGW(TAG, "MQTT disconnected");
}

/**
 * Mesh event callback
 */
void on_mesh_started(void)
{
    s_state.mesh_started = true;
    xEventGroupSetBits(s_gateway_events, EVENT_MESH_STARTED);
    ESP_LOGI(TAG, "Mesh network started");
}

void on_mesh_root_set(bool is_root)
{
    s_state.is_mesh_root = is_root;
    if (is_root) {
        xEventGroupSetBits(s_gateway_events, EVENT_MESH_ROOT);
        ESP_LOGI(TAG, "This device is the MESH ROOT");
    } else {
        xEventGroupClearBits(s_gateway_events, EVENT_MESH_ROOT);
        ESP_LOGW(TAG, "This device is NOT the mesh root!");
    }
}

void on_router_state_changed(bool connected)
{
    if (connected) {
        s_state.wifi_connected = true;
        xEventGroupSetBits(s_gateway_events, EVENT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "WiFi router connected - external network available via WiFi");
    } else {
        s_state.wifi_connected = false;
        xEventGroupClearBits(s_gateway_events, EVENT_WIFI_CONNECTED);
        ESP_LOGW(TAG, "WiFi router disconnected - waiting for auto-reconnect...");
    }
    update_default_route();
}

void on_mesh_child_connected(const uint8_t *mac)
{
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Mesh child connected: %s", mac_str);

    // In discovery mode, don't add nodes to node_manager automatically
    // They will be added via MSG_NODE_ANNOUNCE if commissioned=1
    // or via scan response if they're uncommissioned
    if (commissioning_get_mode() == COMMISSION_MODE_DISCOVERY) {
        ESP_LOGI(TAG, "Discovery mode - node will be handled via protocol messages");
        return;
    }

    // Production mode: add to node manager
    node_manager_add_node(mac);
    s_state.mesh_nodes_count = node_manager_get_count();

    // Notify MQTT
    if (s_state.mqtt_connected) {
        mqtt_publish_node_connected(mac);
    }
}

void on_mesh_child_disconnected(const uint8_t *mac)
{
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGW(TAG, "Mesh child disconnected: %s", mac_str);

    // Update node manager
    node_manager_set_offline(mac);
    s_state.mesh_nodes_count = node_manager_get_count();

    // Notify MQTT
    if (s_state.mqtt_connected) {
        mqtt_publish_node_disconnected(mac);
    }
}

// ============================================================================
// Initialization
// ============================================================================

static void print_banner(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║       OmniaPi Gateway Mesh v%s                 ║", GATEWAY_VERSION_STRING);
    ESP_LOGI(TAG, "║       ESP-WIFI-MESH Fixed Root Gateway            ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

static esp_err_t init_nvs(void)
{
    ESP_LOGI(TAG, "Initializing NVS...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
        nvs_storage_init();

        // Initialize configuration manager (loads from NVS with Kconfig fallback)
        config_manager_init();
        config_print_current();  // Log current configuration
    }

    return ret;
}

static esp_err_t init_network(void)
{
    ESP_LOGI(TAG, "Initializing network (dual: Ethernet + WiFi)...");

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Always initialize Ethernet (non-blocking, connects in background)
    esp_err_t eth_ret = eth_manager_init();
    if (eth_ret == ESP_OK) {
        eth_ret = eth_manager_start();
        if (eth_ret == ESP_OK) {
            s_eth_init_ok = true;
            ESP_LOGI(TAG, "Ethernet initialized and started OK");
        } else {
            s_eth_fail_reason = esp_err_to_name(eth_ret);
            ESP_LOGE(TAG, "Ethernet start FAILED: %s", s_eth_fail_reason);
        }
    } else {
        // Use detailed error from eth_manager (shows which step failed)
        const char *detail = eth_manager_get_init_error();
        s_eth_fail_reason = detail ? detail : esp_err_to_name(eth_ret);
        ESP_LOGE(TAG, "Ethernet init FAILED: %s - WiFi only mode", s_eth_fail_reason);
    }

    // WiFi/mesh will be initialized by mesh_network_init() after this
    return ESP_OK;
}

// ============================================================================
// Provisioning SoftAP Mode
// ============================================================================

/**
 * Start WiFi SoftAP for provisioning when gateway is unconfigured.
 * Creates an AP the user can connect to and configure via HTTP API.
 * Default AP IP: 192.168.4.1
 */
static esp_err_t start_provisioning_ap(void)
{
    const config_wifi_ap_t *ap_cfg = config_get_wifi_ap();

    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║       PROVISIONING MODE - SoftAP                  ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  SSID: %s", ap_cfg->ssid);
    ESP_LOGI(TAG, "  Password: omniapi123");
    ESP_LOGI(TAG, "  Connect and go to http://192.168.4.1");

    // Init TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default AP and STA netifs (APSTA mode needed for WiFi scan)
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // Init WiFi
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    // Configure AP
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, ap_cfg->ssid, sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password, ap_cfg->password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(ap_cfg->ssid);
    wifi_config.ap.channel = ap_cfg->channel;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    // If password is empty, use open auth
    if (strlen(ap_cfg->password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Use APSTA mode so WiFi scan works while serving as AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Reduce TX power to avoid brownout with small PSU (HLK)
    // 8 = 2dBm (minimum), range is enough for provisioning at close range
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(8));
    int8_t actual_power = 0;
    esp_wifi_get_max_tx_power(&actual_power);
    ESP_LOGI(TAG, "WiFi TX power set to %d (x0.25 dBm)", actual_power);

    ESP_LOGI(TAG, "SoftAP started: %s", ap_cfg->ssid);
    return ESP_OK;
}

// ============================================================================
// Captive Portal DNS Server
// Responds to ALL DNS queries with 192.168.4.1 (SoftAP IP)
// This triggers "Sign in to network" notification on phones/laptops
// ============================================================================
static void captive_dns_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Captive DNS server starting on port 53");

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive DNS server running - all domains -> 192.168.4.1");

    uint8_t rx_buf[128];
    uint8_t tx_buf[256];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) continue;  // Too short for DNS header

        // Build DNS response
        memcpy(tx_buf, rx_buf, len);

        // Set response flags: QR=1 (response), AA=1 (authoritative), RD=1
        tx_buf[2] = 0x85;
        tx_buf[3] = 0x80;  // RA=1

        // Set answer count = 1
        tx_buf[6] = 0x00;
        tx_buf[7] = 0x01;

        // Append answer record after the query
        int pos = len;
        tx_buf[pos++] = 0xC0;  // Name pointer to offset 12 (query name)
        tx_buf[pos++] = 0x0C;
        tx_buf[pos++] = 0x00; tx_buf[pos++] = 0x01;  // Type A
        tx_buf[pos++] = 0x00; tx_buf[pos++] = 0x01;  // Class IN
        tx_buf[pos++] = 0x00; tx_buf[pos++] = 0x00;  // TTL = 60s
        tx_buf[pos++] = 0x00; tx_buf[pos++] = 0x3C;
        tx_buf[pos++] = 0x00; tx_buf[pos++] = 0x04;  // RDLENGTH = 4
        tx_buf[pos++] = 192;  // 192.168.4.1
        tx_buf[pos++] = 168;
        tx_buf[pos++] = 4;
        tx_buf[pos++] = 1;

        sendto(sock, tx_buf, pos, 0,
               (struct sockaddr *)&client_addr, client_len);
    }
}

// ============================================================================
// Main Application Entry
// ============================================================================

void app_main(void)
{
    print_banner();

    // Initialize status LED (starts with BOOT pattern)
    esp_err_t led_err = status_led_init();
    if (led_err != ESP_OK && led_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Status LED init failed: %s", esp_err_to_name(led_err));
    }

    // Create event group
    s_gateway_events = xEventGroupCreate();
    if (s_gateway_events == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        status_led_set(STATUS_LED_ERROR);
        esp_restart();
    }

    // Initialize NVS
    ESP_ERROR_CHECK(init_nvs());

    // Mark OTA partition as valid early to prevent rollback during complex init
    // If we got this far (banner + NVS OK), the firmware image is valid
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "OTA partition marked as valid");

    // ================================================================
    // Check provisioning state - SoftAP if unconfigured
    // ================================================================
    provision_state_t prov_state = config_get_provision_state();

    if (prov_state == PROVISION_STATE_UNCONFIGURED) {
        ESP_LOGW(TAG, "Gateway NOT configured - starting provisioning SoftAP");
        status_led_set(STATUS_LED_SEARCHING);

        // Start SoftAP for provisioning
        ESP_ERROR_CHECK(start_provisioning_ap());

        // Start Web UI server (for provisioning API)
        esp_err_t web_err = webserver_start();
        if (web_err != ESP_OK) {
            ESP_LOGE(TAG, "Webserver failed to start: %s", esp_err_to_name(web_err));
        } else {
            ESP_LOGI(TAG, "Provisioning API available at http://192.168.4.1");
        }

        // Start captive portal DNS server (redirects all domains to 192.168.4.1)
        // This triggers "Sign in to network" notification on phones/laptops
        xTaskCreate(captive_dns_task, "captive_dns", 3072, NULL, 5, NULL);

        ESP_LOGI(TAG, "Waiting for configuration via API...");
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

        // Stay in provisioning mode - gateway will reboot after config
        return;
    }

    // ================================================================
    // Normal operation - gateway is configured
    // ================================================================
    ESP_LOGI(TAG, "Gateway configured (state=%d) - starting normal operation", prov_state);
    status_led_set(STATUS_LED_SEARCHING);

    // Initialize network (Ethernet + WiFi dual connectivity)
    ESP_ERROR_CHECK(init_network());

    // Initialize node manager
    ESP_ERROR_CHECK(node_manager_init());

    // Initialize mesh network as Fixed Root (also initializes WiFi)
    ESP_ERROR_CHECK(mesh_network_init());

    // Get MAC address (after WiFi init)
    esp_wifi_get_mac(WIFI_IF_STA, s_state.gateway_mac);
    ESP_LOGI(TAG, "Gateway MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_state.gateway_mac[0], s_state.gateway_mac[1], s_state.gateway_mac[2],
             s_state.gateway_mac[3], s_state.gateway_mac[4], s_state.gateway_mac[5]);

    // Set mesh callbacks
    mesh_network_set_rx_cb(mesh_rx_handler);
    mesh_network_set_child_connected_cb(on_mesh_child_connected);
    mesh_network_set_child_disconnected_cb(on_mesh_child_disconnected);
    mesh_network_set_router_cb(on_router_state_changed);

    // Start mesh network
    ESP_ERROR_CHECK(mesh_network_start());

    // Since we're Fixed Root, set state directly after successful start
    s_state.mesh_started = true;
    s_state.is_mesh_root = true;
    ESP_LOGI(TAG, "Mesh state set: started=true, is_root=true");

    // Initialize and start MQTT (after network is ready)
    ESP_ERROR_CHECK(mqtt_handler_init());
    ESP_ERROR_CHECK(mqtt_handler_start());

    // Initialize commissioning handler
    ESP_ERROR_CHECK(commissioning_init());

    // Initialize OTA manager
    ESP_ERROR_CHECK(ota_manager_init());

    // Initialize node OTA manager (push-mode OTA to mesh nodes)
    ESP_ERROR_CHECK(node_ota_init());

    // Start Web UI server
    esp_err_t web_err = webserver_start();
    if (web_err != ESP_OK) {
        ESP_LOGE(TAG, "Webserver failed to start: %s", esp_err_to_name(web_err));
    } else {
        ESP_LOGI(TAG, "Web UI available at http://omniapi-gateway/ or via IP");
    }

    // Create main tasks
    xTaskCreate(gateway_task, "gateway_task", 4096, NULL, 5, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 4, NULL);
    xTaskCreate(status_task, "status_task", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Gateway initialization complete");
    ESP_LOGI(TAG, "  Ethernet: %s (netif=%p)", s_eth_init_ok ? "INIT OK" : "INIT FAILED", eth_manager_get_netif());
    ESP_LOGI(TAG, "  WiFi/Mesh: started, STA netif=%p", mesh_network_get_sta_netif());
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // All initialized - set LED to connected
    status_led_set(STATUS_LED_CONNECTED);
}

// ============================================================================
// Tasks
// ============================================================================

/**
 * Main gateway task - handles mesh messages
 */
static void gateway_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Gateway task started");

    while (1) {
        // Process incoming mesh messages
        mesh_network_process_rx();

        // Check for MQTT commands
        mqtt_handler_process();

        // Check OTA timeout
        ota_manager_check_timeout();

        // Check node OTA timeout
        node_ota_check_timeout();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * Heartbeat task - periodic node health check
 */
static void heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heartbeat task started");

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(CONFIG_GATEWAY_HEARTBEAT_INTERVAL_MS);

    while (1) {
        // Send heartbeat to all mesh nodes
        if (s_state.mesh_started && s_state.is_mesh_root) {
            mesh_network_broadcast_heartbeat();
        }

        // Check for offline nodes
        node_manager_check_timeouts();

        vTaskDelayUntil(&last_wake, interval);
    }
}

/**
 * Status task - periodic status logging
 */
static void status_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Status task started");

    while (1) {
        s_state.uptime_sec += 30;

        ESP_LOGI(TAG, "=== Gateway Status ===");
        ESP_LOGI(TAG, "  Uptime: %lu sec", s_state.uptime_sec);
        const char *route = s_state.eth_connected ? "ETH" : (s_state.wifi_connected ? "WiFi" : "NONE");
        if (!s_eth_init_ok) {
            ESP_LOGE(TAG, "  ETH: INIT FAIL (%s), WiFi: %s, MQTT: %s, Route: %s",
                     s_eth_fail_reason ? s_eth_fail_reason : "unknown",
                     s_state.wifi_connected ? "OK" : "--",
                     s_state.mqtt_connected ? "OK" : "--",
                     route);
        } else {
            ESP_LOGI(TAG, "  ETH: %s, WiFi: %s, MQTT: %s, Route: %s",
                     s_state.eth_connected ? "OK" : "NO LINK",
                     s_state.wifi_connected ? "OK" : "--",
                     s_state.mqtt_connected ? "OK" : "--",
                     route);
        }
        ESP_LOGI(TAG, "  Mesh: %s, Root: %s, Nodes: %d",
                 s_state.mesh_started ? "OK" : "--",
                 s_state.is_mesh_root ? "YES" : "NO",
                 s_state.mesh_nodes_count);
        ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());

        // Publish status to MQTT
        if (s_state.mqtt_connected) {
            mqtt_publish_gateway_status(true);
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
