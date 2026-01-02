/**
 * OmniaPi Gateway - Main Entry Point
 * ESP-IDF firmware for ESP32 Gateway
 *
 * Features:
 * - WiFi Station connection
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
#include "node_manager.h"
#include "espnow_master.h"
#include "mqtt_handler.h"
#include "webserver.h"
#include "storage.h"
#include "ota_handler.h"

static const char *TAG = "main";

#define FIRMWARE_VERSION "1.4.0-idf"

// Task intervals
#define HEARTBEAT_INTERVAL_MS   3000    // ESP-NOW heartbeat every 3s
#define MQTT_HEARTBEAT_MS       30000   // MQTT status publish every 30s
#define STATUS_PRINT_MS         30000   // Serial status every 30s

// ============== State Change Callback ==============
// Called when a node's relay state changes (from ESP-NOW)
static void on_node_state_change(int node_index, uint8_t channel, uint8_t state)
{
    ESP_LOGI(TAG, "Node %d relay %d changed to %s", node_index, channel, state ? "ON" : "OFF");

    // Publish to MQTT
    mqtt_handler_publish_node_state(node_index);
}

// ============== Main Task ==============
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

    // ===== Initialize WiFi =====
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed!");
        return;
    }
    ESP_LOGI(TAG, "WiFi initialized");

    // ===== Start WiFi Connection =====
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return;
    }

    char ip_str[16];
    wifi_manager_get_ip(ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "WiFi connected! IP: %s, Channel: %d",
             ip_str, wifi_manager_get_channel());

    // ===== Initialize ESP-NOW =====
    ret = espnow_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed!");
        return;
    }

    ret = espnow_master_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW start failed!");
        return;
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
        return;
    }
    ESP_LOGI(TAG, "Web server started");

    // ===== Print Ready Message =====
    printf("\n");
    printf("========================================\n");
    printf("  GATEWAY READY!\n");
    printf("  Web UI: http://%s\n", ip_str);
    printf("  MQTT:   mqtt://192.168.1.11:1883\n");
    printf("========================================\n\n");

    // ===== Create Main Task =====
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
}
