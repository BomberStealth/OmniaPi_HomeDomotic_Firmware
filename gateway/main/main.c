/**
 * OmniaPi Gateway - Main Entry Point
 * ESP-IDF firmware for ESP32 Gateway
 *
 * Features:
 * - WiFi Station connection with AP fallback
 * - Captive Portal for WiFi configuration
 * - ESP-NOW master for node communication
 * - MQTT client for Backend integration
 * - HTTP server with REST API
 * - OTA updates (self + nodes)
 *
 * Hardware: WT32-ETH01 (ESP32)
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
#include "captive_portal.h"
#include "node_manager.h"
#include "espnow_master.h"
#include "mqtt_handler.h"
#include "webserver.h"
#include "storage.h"
#include "ota_handler.h"
#include "backend_client.h"

static const char *TAG = "main";

#define FIRMWARE_VERSION "1.5.0-idf"

// Task intervals
#define HEARTBEAT_INTERVAL_MS   3000    // ESP-NOW heartbeat every 3s
#define MQTT_HEARTBEAT_MS       30000   // MQTT status publish every 30s
#define STATUS_PRINT_MS         30000   // Serial status every 30s

// WiFi connection timeout (30 seconds)
#define WIFI_CONNECT_TIMEOUT_MS 30000

// Flags
static bool s_ap_mode = false;

// ============== State Change Callback ==============
// Called when a node's relay state changes (from ESP-NOW)
static void on_node_state_change(int node_index, uint8_t channel, uint8_t state)
{
    ESP_LOGI(TAG, "Node %d relay %d changed to %s", node_index, channel, state ? "ON" : "OFF");

    // Publish to MQTT
    mqtt_handler_publish_node_state(node_index);
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
            wifi_manager_get_ip(ip_str, sizeof(ip_str));

            ESP_LOGI(TAG, "[STATUS] Nodes=%d | RX=%lu | TX=%lu | WiFi=%s | MQTT=%s | IP=%s",
                     node_manager_get_count(),
                     (unsigned long)espnow_master_get_rx_count(),
                     (unsigned long)espnow_master_get_tx_count(),
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

        // Print status every 30 seconds
        if (now - last_print >= 30000) {
            char ap_ssid[32];
            wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
            ESP_LOGI(TAG, "[AP MODE] Waiting for configuration - Connect to '%s'", ap_ssid);
            last_print = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============== Start Normal Operation ==============
static esp_err_t start_normal_operation(void)
{
    esp_err_t ret;
    char ip_str[16];
    wifi_manager_get_ip(ip_str, sizeof(ip_str));

    ESP_LOGI(TAG, "WiFi connected! IP: %s, Channel: %d",
             ip_str, wifi_manager_get_channel());

    // ===== Initialize ESP-NOW =====
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
    printf("========================================\n");
    printf("  GATEWAY READY!\n");
    printf("  Web UI:  http://%s\n", ip_str);
    printf("  MQTT:    mqtt://192.168.1.11:1883\n");
    printf("  Backend: http://192.168.1.253:3000\n");
    printf("========================================\n\n");

    // ===== Create Main Task =====
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);

    return ESP_OK;
}

// ============== Start AP Mode ==============
static esp_err_t start_ap_mode(void)
{
    s_ap_mode = true;

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
    printf("========================================\n");
    printf("  OmniaPi Gateway v%s\n", FIRMWARE_VERSION);
    printf("  ESP-IDF %s\n", esp_get_idf_version());
    printf("  ESP-NOW + WiFi + MQTT + HTTP\n");
    printf("========================================\n\n");

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
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed!");
        return;
    }
    ESP_LOGI(TAG, "WiFi Manager initialized");

    // ===== Check for saved credentials =====
    if (wifi_manager_has_credentials()) {
        char ssid[33];
        wifi_manager_get_saved_ssid(ssid, sizeof(ssid));
        ESP_LOGI(TAG, "Found saved credentials for: %s", ssid);

        // Try to connect
        ESP_LOGI(TAG, "Attempting to connect to WiFi...");
        ret = wifi_manager_connect(WIFI_CONNECT_TIMEOUT_MS);

        if (ret == ESP_OK) {
            // Connected successfully - start normal operation
            start_normal_operation();
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
