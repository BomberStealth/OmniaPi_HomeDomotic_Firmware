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

#define WIFI_CHANNEL 10

void app_main(void)
{
    ESP_LOGI(TAG, "OmniaPi Node v2.5.0 (ESP-IDF)");

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
    espnow_handler_init(WIFI_CHANNEL);

    // LED pattern: avvio
    led_blink(3, 100);

    ESP_LOGI(TAG, "Node inizializzato, CH=%d", WIFI_CHANNEL);

    // Main loop - gestito da tasks FreeRTOS
    while (1) {
        led_status_update();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
