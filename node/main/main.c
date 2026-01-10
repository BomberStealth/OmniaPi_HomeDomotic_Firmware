#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "espnow_handler.h"
#include "relay_control.h"
#include "led_status.h"

static const char *TAG = "OMNIAPI_NODE";

void app_main(void)
{
    ESP_LOGI(TAG, "OmniaPi Node v2.6.0 (ESP-IDF)");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init componenti
    led_status_init();
    relay_control_init();

    // LED pattern: avvio (blink veloce)
    led_blink(3, 100);

    // === CHANNEL SCAN LOGIC ===
    uint8_t channel = 0;

    // 1. Try to load saved channel from NVS
    channel = espnow_load_channel();

    if (channel > 0 && channel <= 13) {
        ESP_LOGI(TAG, "Using saved channel %d from NVS", channel);
        espnow_handler_init(channel);

        // Wait a bit and check if gateway responds
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (!espnow_is_gateway_known()) {
            ESP_LOGW(TAG, "Gateway not responding on saved channel, rescanning...");
            channel = 0;  // Force rescan
        }
    }

    // 2. If no saved channel or gateway not found, scan all channels
    if (channel == 0) {
        ESP_LOGI(TAG, "Starting channel scan...");
        led_blink(10, 50);  // Fast blink during scan

        channel = espnow_channel_scan();

        if (channel == 0) {
            ESP_LOGE(TAG, "Gateway NOT FOUND on any channel!");
            ESP_LOGI(TAG, "Will retry scan every 30 seconds...");

            // Retry loop
            while (channel == 0) {
                led_blink(2, 500);  // Slow blink = searching
                vTaskDelay(pdMS_TO_TICKS(30000));
                channel = espnow_channel_scan();
            }
        }
    }

    ESP_LOGI(TAG, "Node initialized, CH=%d", channel);
    led_blink(5, 200);  // Success pattern

    // Main loop - gestito da tasks FreeRTOS
    while (1) {
        led_status_update();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
