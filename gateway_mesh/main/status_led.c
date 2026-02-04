/**
 * OmniaPi Gateway Mesh - Status LED Driver Implementation
 */

#include "status_led.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "STATUS_LED";

// Check if LED is enabled
#if CONFIG_STATUS_LED_GPIO >= 0

static TaskHandle_t s_led_task = NULL;
static volatile status_led_pattern_t s_current_pattern = STATUS_LED_OFF;
static volatile bool s_running = false;

// LED control macros
#ifdef CONFIG_STATUS_LED_ACTIVE_LOW
#define LED_ON()    gpio_set_level(CONFIG_STATUS_LED_GPIO, 0)
#define LED_OFF()   gpio_set_level(CONFIG_STATUS_LED_GPIO, 1)
#define LED_ACTIVE_STR "low"
#else
#define LED_ON()    gpio_set_level(CONFIG_STATUS_LED_GPIO, 1)
#define LED_OFF()   gpio_set_level(CONFIG_STATUS_LED_GPIO, 0)
#define LED_ACTIVE_STR "high"
#endif

/**
 * LED blink task
 */
static void led_task(void *arg)
{
    ESP_LOGI(TAG, "LED task started on GPIO %d", CONFIG_STATUS_LED_GPIO);

    int step = 0;

    while (s_running) {
        switch (s_current_pattern) {
            case STATUS_LED_OFF:
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case STATUS_LED_BOOT:
                // Fast blink: 100ms on, 100ms off
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(100));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case STATUS_LED_SEARCHING:
                // Slow blink: 500ms on, 500ms off
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(500));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case STATUS_LED_CONNECTED:
                // Solid on
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case STATUS_LED_ERROR:
                // Very fast blink: 50ms on, 50ms off
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(50));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(50));
                break;

            case STATUS_LED_OTA:
                // Double blink pattern: blink-blink-pause
                // Blink 1
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(100));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(100));
                // Blink 2
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(100));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(500));  // Pause
                break;

            case STATUS_LED_SCANNING:
                // Medium blink: 250ms on, 250ms off
                LED_ON();
                vTaskDelay(pdMS_TO_TICKS(250));
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(250));
                break;

            default:
                LED_OFF();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }

        step++;
    }

    LED_OFF();
    ESP_LOGI(TAG, "LED task stopped");
    s_led_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t status_led_init(void)
{
    ESP_LOGI(TAG, "Initializing status LED on GPIO %d (active %s)",
             CONFIG_STATUS_LED_GPIO, LED_ACTIVE_STR);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start with LED off
    LED_OFF();

    // Create LED task
    s_running = true;
    s_current_pattern = STATUS_LED_BOOT;  // Start in boot pattern

    BaseType_t xret = xTaskCreate(led_task, "status_led", 1024, NULL, 2, &s_led_task);
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Status LED initialized");
    return ESP_OK;
}

void status_led_set(status_led_pattern_t pattern)
{
    if (s_current_pattern != pattern) {
        ESP_LOGD(TAG, "LED pattern: %d -> %d", s_current_pattern, pattern);
        s_current_pattern = pattern;
    }
}

status_led_pattern_t status_led_get(void)
{
    return s_current_pattern;
}

void status_led_deinit(void)
{
    s_running = false;
    if (s_led_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));  // Give task time to exit
    }
    LED_OFF();
}

#else  // LED disabled

esp_err_t status_led_init(void)
{
    ESP_LOGI(TAG, "Status LED disabled (GPIO = -1)");
    return ESP_ERR_NOT_SUPPORTED;
}

void status_led_set(status_led_pattern_t pattern)
{
    // No-op when disabled
}

status_led_pattern_t status_led_get(void)
{
    return STATUS_LED_OFF;
}

void status_led_deinit(void)
{
    // No-op when disabled
}

#endif  // CONFIG_STATUS_LED_GPIO >= 0
