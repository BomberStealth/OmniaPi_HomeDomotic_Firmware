/**
 * OmniaPi Gateway Mesh - WiFi Manager Implementation
 * Note: WiFi is mainly managed by mesh_network.c
 * This module provides fallback/utility functions
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "WIFI_MGR";
static bool s_initialized = false;

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "WiFi manager init (mesh handles WiFi)");
    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    // WiFi is started by mesh_network
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    return esp_wifi_stop();
}

bool wifi_manager_is_connected(void)
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}
