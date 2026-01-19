#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "espnow_handler.h"
#include "led_controller.h"
#include "effects.h"

static const char *TAG = "OMNIAPI_LED";

// ============================================
// MAIN APPLICATION
// ============================================

void app_main(void)
{
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "OmniaPi LED Strip v1.0.0 (ESP32-S2)");
    ESP_LOGI(TAG, "=========================================");

    // ========== INIT NVS ==========
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // ========== INIT LED STRIP ==========
    led_controller_init();
    ESP_LOGI(TAG, "LED controller initialized");

    // TEST LED AL BOOT - Effetto rainbow per 5 secondi
    ESP_LOGI(TAG, "=== TEST LED BOOT ===");
    led_set_effect(EFFECT_RAINBOW);
    led_set_brightness(128);  // 50% per non accecare
    led_set_power_on();
    vTaskDelay(5000 / portTICK_PERIOD_MS);  // 5 secondi rainbow
    led_set_power_off();
    ESP_LOGI(TAG, "=== TEST LED COMPLETATO ===");

    // ========== CHANNEL SCAN LOGIC ==========
    uint8_t channel = 0;

    // 1. Try to load saved channel from NVS
    channel = espnow_load_channel();

    if (channel > 0 && channel <= 13) {
        ESP_LOGI(TAG, "Using saved channel %d from NVS", channel);
        espnow_handler_init(channel);

        // Wait a bit and check if gateway responds
        // Blue breathing effect while waiting
        led_set_color(0, 0, 255);
        led_set_effect(EFFECT_BREATHING);

        for (int i = 0; i < 100; i++) {  // 2 seconds
            led_update();
            vTaskDelay(pdMS_TO_TICKS(20));

            if (espnow_is_gateway_known()) {
                break;
            }
        }

        if (!espnow_is_gateway_known()) {
            ESP_LOGW(TAG, "Gateway not responding on saved channel, rescanning...");
            channel = 0;  // Force rescan
        }
    }

    // 2. If no saved channel or gateway not found, scan all channels
    if (channel == 0) {
        ESP_LOGI(TAG, "Starting channel scan...");

        // Yellow chase effect during scan
        led_set_color(255, 200, 0);
        led_set_effect(EFFECT_CHASE);

        // Scan with visual feedback
        while (channel == 0) {
            for (int scan_attempt = 0; scan_attempt < 2; scan_attempt++) {
                channel = espnow_channel_scan();

                if (channel > 0) break;

                ESP_LOGW(TAG, "Gateway not found, retrying in 5 seconds...");

                // Red breathing while waiting to retry
                led_set_color(255, 0, 0);
                led_set_effect(EFFECT_BREATHING);

                for (int i = 0; i < 250; i++) {  // 5 seconds
                    led_update();
                    vTaskDelay(pdMS_TO_TICKS(20));
                }

                // Back to yellow chase for next scan
                led_set_color(255, 200, 0);
                led_set_effect(EFFECT_CHASE);
            }
        }
    }

    // ========== SUCCESS ==========
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "LED Strip connected! Channel=%d", channel);
    ESP_LOGI(TAG, "=========================================");

    // Green success flash
    led_set_color(0, 255, 0);
    led_set_effect(EFFECT_STATIC);
    led_set_power_on();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Load saved state or turn off
    led_load_state();
    led_state_t* state = led_get_state();

    if (state->power) {
        // Restore saved effect/color
        led_set_color(state->r, state->g, state->b);
        led_set_brightness(state->brightness);
        led_set_effect(state->effect_id);
        led_set_effect_speed(state->effect_speed);
        ESP_LOGI(TAG, "Restored saved state: RGB=%d,%d,%d Effect=%d",
                 state->r, state->g, state->b, state->effect_id);
    } else {
        led_set_power_off();
        ESP_LOGI(TAG, "Starting with LEDs off (saved state)");
    }

    // ========== MAIN LOOP ==========
    ESP_LOGI(TAG, "Entering main loop...");

    uint32_t loop_count = 0;

    while (1) {
        // Update LED effects (~50Hz for smooth animations)
        led_update();

        // Log heartbeat status periodically
        if (++loop_count >= 1000) {  // Every ~20 seconds
            loop_count = 0;
            uint32_t last_hb = espnow_get_last_heartbeat_time();
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ESP_LOGI(TAG, "Status: Gateway=%s, LastHB=%lums ago",
                     espnow_is_gateway_known() ? "OK" : "LOST",
                     (unsigned long)(now - last_hb));
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // ~50Hz update rate
    }
}
