/**
 * OmniaPi Gateway - Main Entry Point
 * ESP-IDF firmware for ESP32 Gateway
 *
 * Features:
 * - Ethernet (LAN8720) with WiFi failover
 * - WiFi Station connection with AP fallback
 * - Captive Portal for WiFi configuration
 * - ESP-NOW master for node communication
 * - MQTT client for Backend integration
 * - HTTP server with REST API
 * - OTA updates (self + nodes)
 *
 * Hardware: WT32-ETH01 (ESP32 + LAN8720)
 * Build: idf.py build
 * Flash: idf.py -p COM6 flash
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"

// Module headers
#include "wifi_manager.h"
#include "eth_manager.h"
#include "captive_portal.h"
#include "node_manager.h"
#include "espnow_master.h"
#include "mqtt_handler.h"
#include "webserver.h"
#include "storage.h"
#include "ota_handler.h"
#include "backend_client.h"

static const char *TAG = "main";

#define FIRMWARE_VERSION "1.8.7-idf"

// Task intervals
#define HEARTBEAT_INTERVAL_MS   1000    // ESP-NOW heartbeat every 1s
#define MQTT_HEARTBEAT_MS       5000    // MQTT status publish every 5s
#define STATUS_PRINT_MS         30000   // Serial status every 30s

// Timeouts
#define ETH_CONNECT_TIMEOUT_MS  5000    // Wait 5s for Ethernet
#define WIFI_CONNECT_TIMEOUT_MS 30000   // Wait 30s for WiFi

// Network state
static network_mode_t s_network_mode = NETWORK_MODE_NONE;
static bool s_services_started = false;

// ============== Network Mode Helpers ==============
static const char* network_mode_to_str(network_mode_t mode)
{
    switch (mode) {
        case NETWORK_MODE_ETH:  return "ETH";
        case NETWORK_MODE_WIFI: return "WiFi";
        case NETWORK_MODE_AP:   return "AP";
        default:                return "NONE";
    }
}

static void get_current_ip(char *buffer, size_t len)
{
    if (s_network_mode == NETWORK_MODE_ETH) {
        eth_manager_get_ip(buffer, len);
    } else if (s_network_mode == NETWORK_MODE_WIFI || s_network_mode == NETWORK_MODE_AP) {
        wifi_manager_get_ip(buffer, len);
    } else {
        snprintf(buffer, len, "0.0.0.0");
    }
}

static bool is_network_connected(void)
{
    return (s_network_mode == NETWORK_MODE_ETH && eth_manager_is_connected()) ||
           (s_network_mode == NETWORK_MODE_WIFI && wifi_manager_is_connected());
}

// ============== State Change Callback ==============
// Called when a node's relay state changes (from ESP-NOW)
static void on_node_state_change(int node_index, uint8_t channel, uint8_t state)
{
    ESP_LOGI(TAG, "Node %d relay %d changed to %s", node_index, channel, state ? "ON" : "OFF");

    // Publish to MQTT
    mqtt_handler_publish_node_state(node_index);
}

// ============== Ethernet Status Callback ==============
static void on_eth_status_change(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "Ethernet connected!");

        // If we were using WiFi, switch to ETH (has priority)
        if (s_network_mode == NETWORK_MODE_WIFI) {
            ESP_LOGI(TAG, "Switching from WiFi to Ethernet (priority)");
            s_network_mode = NETWORK_MODE_ETH;
            // Note: Both connections remain active, we just prefer ETH
        } else if (s_network_mode != NETWORK_MODE_ETH) {
            s_network_mode = NETWORK_MODE_ETH;
        }
    } else {
        ESP_LOGW(TAG, "Ethernet disconnected!");

        if (s_network_mode == NETWORK_MODE_ETH) {
            // Try failover to WiFi
            if (wifi_manager_is_connected()) {
                ESP_LOGI(TAG, "Failover to WiFi");
                s_network_mode = NETWORK_MODE_WIFI;
            } else if (wifi_manager_has_credentials()) {
                ESP_LOGI(TAG, "Attempting WiFi connection for failover...");
                if (wifi_manager_connect(10000) == ESP_OK) {
                    s_network_mode = NETWORK_MODE_WIFI;
                    ESP_LOGI(TAG, "WiFi failover successful");
                } else {
                    ESP_LOGE(TAG, "WiFi failover failed - no network!");
                    s_network_mode = NETWORK_MODE_NONE;
                }
            } else {
                ESP_LOGE(TAG, "No WiFi credentials - no network!");
                s_network_mode = NETWORK_MODE_NONE;
            }
        }
    }
}

// ============== Main Task (Normal Mode) ==============
static void main_task(void *pvParameters)
{
    uint32_t last_heartbeat = 0;
    uint32_t last_mqtt_heartbeat = 0;
    uint32_t last_status_print = 0;

    ESP_LOGI(TAG, "Main task started");

    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        // ===== ESP-NOW Heartbeat (every 3s) =====
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            espnow_master_send_heartbeat();
            last_heartbeat = now;
        }

        // ===== Check node online status =====
        node_manager_check_online_status();

        // ===== MQTT Heartbeat (every 30s) =====
        if (mqtt_handler_is_connected()) {
            if (now - last_mqtt_heartbeat >= MQTT_HEARTBEAT_MS) {
                mqtt_handler_publish_status();
                mqtt_handler_publish_all_nodes();
                last_mqtt_heartbeat = now;
            }
        }

        // ===== OTA Processing =====
        ota_handler_process();

        // ===== Status Print (every 30s) =====
        if (now - last_status_print >= STATUS_PRINT_MS) {
            char ip_str[16] = "0.0.0.0";
            get_current_ip(ip_str, sizeof(ip_str));

            ESP_LOGI(TAG, "[STATUS] Mode=%s | Nodes=%d | RX=%lu | TX=%lu | ETH=%s | WiFi=%s | MQTT=%s | IP=%s",
                     network_mode_to_str(s_network_mode),
                     node_manager_get_count(),
                     (unsigned long)espnow_master_get_rx_count(),
                     (unsigned long)espnow_master_get_tx_count(),
                     eth_manager_is_connected() ? "OK" : "DISC",
                     wifi_manager_is_connected() ? "OK" : "DISC",
                     mqtt_handler_is_connected() ? "OK" : "DISC",
                     ip_str);
            last_status_print = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============== AP Mode Task ==============
// Just keeps the system alive while in AP mode
static void ap_mode_task(void *pvParameters)
{
    uint32_t last_print = 0;

    ESP_LOGI(TAG, "AP Mode task started - waiting for configuration");

    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        // Check if ETH connected while in AP mode
        if (eth_manager_is_connected()) {
            ESP_LOGI(TAG, "Ethernet connected while in AP mode! Restarting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        // Print status every 30 seconds
        if (now - last_print >= 30000) {
            char ap_ssid[32];
            wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
            ESP_LOGI(TAG, "[AP MODE] Waiting for configuration - Connect to '%s' | ETH=%s",
                     ap_ssid, eth_manager_is_link_up() ? "LINK" : "NO LINK");
            last_print = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============== Start Services ==============
static esp_err_t start_services(const char *ip_str)
{
    if (s_services_started) {
        ESP_LOGW(TAG, "Services already started");
        return ESP_OK;
    }

    esp_err_t ret;

    ESP_LOGI(TAG, "Network connected! IP: %s, Mode: %s",
             ip_str, network_mode_to_str(s_network_mode));

    // ===== Initialize ESP-NOW =====
    // Note: ESP-NOW requires WiFi to be initialized
    if (!wifi_manager_is_connected() && s_network_mode == NETWORK_MODE_ETH) {
        // If only ETH, we need to init WiFi in background for ESP-NOW
        ESP_LOGI(TAG, "Initializing WiFi for ESP-NOW (ETH-only mode)");
        // WiFi was already initialized in app_main
    }

    ret = espnow_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed!");
        return ret;
    }

    ret = espnow_master_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW start failed!");
        return ret;
    }

    // Register state change callback
    espnow_master_register_state_cb(on_node_state_change);
    ESP_LOGI(TAG, "ESP-NOW started");

    // ===== Initialize MQTT =====
    ret = mqtt_handler_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT init failed (non-critical)");
    } else {
        mqtt_handler_start();
        ESP_LOGI(TAG, "MQTT client started");
    }

    // ===== Initialize Web Server =====
    ret = webserver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web server init failed!");
        return ret;
    }
    ESP_LOGI(TAG, "Web server started");

    // ===== Register with Backend =====
    backend_client_init();
    backend_client_start_registration();
    ESP_LOGI(TAG, "Backend registration started");

    // ===== Print Ready Message =====
    printf("\n");
    printf("=============================================\n");
    printf("  GATEWAY READY!\n");
    printf("  Network: %s\n", network_mode_to_str(s_network_mode));
    printf("  Web UI:  http://%s\n", ip_str);
    printf("  MQTT:    mqtt://192.168.1.252:1883\n");
    printf("  Backend: http://192.168.1.253:3000\n");
    if (s_network_mode == NETWORK_MODE_ETH && wifi_manager_has_credentials()) {
        printf("  WiFi:    Backup ready\n");
    }
    printf("=============================================\n\n");

    // ===== Create Main Task =====
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);

    s_services_started = true;
    return ESP_OK;
}

// ============== Start AP Mode ==============
static esp_err_t start_ap_mode(void)
{
    s_network_mode = NETWORK_MODE_AP;

    // Start AP
    esp_err_t ret = wifi_manager_start_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP mode!");
        return ret;
    }

    // Start Captive Portal
    ret = captive_portal_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Captive Portal!");
        return ret;
    }

    // Create AP mode task
    xTaskCreate(ap_mode_task, "ap_mode_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

// ============== App Main ==============
void app_main(void)
{
    // Print banner
    printf("\n");
    printf("=============================================\n");
    printf("  OmniaPi Gateway v%s\n", FIRMWARE_VERSION);
    printf("  ESP-IDF %s\n", esp_get_idf_version());
    printf("  ETH + WiFi + ESP-NOW + MQTT + HTTP\n");
    printf("  Hardware: WT32-ETH01 (LAN8720)\n");
    printf("=============================================\n\n");

    esp_err_t ret;

    // ===== Initialize Node Manager =====
    ret = node_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Node Manager init failed!");
        return;
    }
    ESP_LOGI(TAG, "Node Manager initialized");

    // ===== Initialize Storage (SPIFFS) =====
    ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Storage init failed (non-critical)");
    } else {
        ESP_LOGI(TAG, "Storage initialized");
    }

    // ===== Initialize OTA Handler =====
    ret = ota_handler_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA Handler init failed");
    } else {
        ESP_LOGI(TAG, "OTA Handler initialized");
    }

    // ===== Initialize WiFi Manager =====
    // This also initializes NVS, netif, and event loop
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed!");
        return;
    }
    ESP_LOGI(TAG, "WiFi Manager initialized");

    // ===== Initialize Ethernet Manager =====
    ret = eth_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet init failed (continuing with WiFi only)");
    } else {
        ESP_LOGI(TAG, "Ethernet Manager initialized");
        eth_manager_set_callback(on_eth_status_change);

        // Start Ethernet
        ret = eth_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Ethernet start failed");
        }
    }

    // ===== Wait for Ethernet connection (5 seconds) =====
    ESP_LOGI(TAG, "Waiting for Ethernet connection (%d ms)...", ETH_CONNECT_TIMEOUT_MS);
    int eth_timeout = ETH_CONNECT_TIMEOUT_MS;
    while (!eth_manager_is_connected() && eth_timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        eth_timeout -= 100;

        // Print progress every second
        if (eth_timeout % 1000 == 0) {
            ESP_LOGI(TAG, "  ETH wait: %d ms remaining, link=%s",
                     eth_timeout, eth_manager_is_link_up() ? "UP" : "DOWN");
        }
    }

    // ===== Decision: ETH connected? =====
    if (eth_manager_is_connected()) {
        // ETH connected - use it as primary
        char ip_str[16];
        eth_manager_get_ip(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Ethernet connected! IP: %s", ip_str);
        s_network_mode = NETWORK_MODE_ETH;

        // Also try to connect WiFi as backup (non-blocking)
        if (wifi_manager_has_credentials()) {
            char ssid[33];
            wifi_manager_get_saved_ssid(ssid, sizeof(ssid));
            ESP_LOGI(TAG, "Connecting WiFi as backup: %s", ssid);

            // Try quick WiFi connect (10 sec timeout)
            if (wifi_manager_connect(10000) == ESP_OK) {
                char wifi_ip[16];
                wifi_manager_get_ip(wifi_ip, sizeof(wifi_ip));
                ESP_LOGI(TAG, "WiFi backup connected: %s", wifi_ip);
            } else {
                ESP_LOGW(TAG, "WiFi backup connection failed (ETH still primary)");
            }
        }

        // Start services
        start_services(ip_str);

    } else {
        // ETH not connected - try WiFi
        ESP_LOGW(TAG, "Ethernet not connected, trying WiFi...");

        if (wifi_manager_has_credentials()) {
            char ssid[33];
            wifi_manager_get_saved_ssid(ssid, sizeof(ssid));
            ESP_LOGI(TAG, "Found saved credentials for: %s", ssid);

            // Try to connect
            ESP_LOGI(TAG, "Attempting to connect to WiFi...");
            ret = wifi_manager_connect(WIFI_CONNECT_TIMEOUT_MS);

            if (ret == ESP_OK) {
                // Connected successfully
                char ip_str[16];
                wifi_manager_get_ip(ip_str, sizeof(ip_str));
                s_network_mode = NETWORK_MODE_WIFI;
                start_services(ip_str);
            } else {
                // Connection failed - go to AP mode
                ESP_LOGW(TAG, "WiFi connection failed! Starting AP mode...");
                start_ap_mode();
            }
        } else {
            // No saved credentials - go to AP mode
            ESP_LOGI(TAG, "No saved WiFi credentials. Starting AP mode...");
            start_ap_mode();
        }
    }
}
