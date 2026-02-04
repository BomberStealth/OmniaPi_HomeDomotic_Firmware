/**
 * OmniaPi Node Mesh - Button Handler Implementation
 */

#include "button_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BUTTON";

// Button state
static volatile bool s_button_pressed = false;
static volatile int64_t s_press_start_time = 0;

// Callbacks
static void (*s_short_press_cb)(void) = NULL;
static void (*s_long_press_cb)(void) = NULL;

// Button task handle
static TaskHandle_t s_button_task = NULL;

// Debounce time (ms)
#define DEBOUNCE_TIME_MS    50

// Short press max duration (ms)
#define SHORT_PRESS_MAX_MS  1000

// Active level from config
#ifdef CONFIG_BUTTON_ACTIVE_LOW
#define BUTTON_PRESSED_LEVEL  0
#else
#define BUTTON_PRESSED_LEVEL  1
#endif

static void IRAM_ATTR button_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the button task
    if (s_button_task) {
        vTaskNotifyGiveFromISR(s_button_task, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void button_task(void *arg)
{
    int64_t last_change_time = 0;
    bool last_state = false;

    while (1) {
        // Wait for interrupt or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // Read button state
        bool current_state = (gpio_get_level(CONFIG_BUTTON_GPIO) == BUTTON_PRESSED_LEVEL);
        int64_t now = esp_timer_get_time() / 1000;  // ms

        // Debounce
        if (current_state != last_state) {
            if ((now - last_change_time) > DEBOUNCE_TIME_MS) {
                last_state = current_state;
                last_change_time = now;

                if (current_state) {
                    // Button pressed
                    s_button_pressed = true;
                    s_press_start_time = now;
                    ESP_LOGD(TAG, "Button pressed");
                } else {
                    // Button released
                    if (s_button_pressed) {
                        int64_t press_duration = now - s_press_start_time;
                        ESP_LOGI(TAG, "Button released after %lld ms", press_duration);

                        if (press_duration >= CONFIG_BUTTON_LONG_PRESS_MS) {
                            // Long press - factory reset
                            ESP_LOGW(TAG, "Long press detected - triggering callback");
                            if (s_long_press_cb) {
                                s_long_press_cb();
                            }
                        } else if (press_duration < SHORT_PRESS_MAX_MS) {
                            // Short press - toggle
                            ESP_LOGI(TAG, "Short press detected - triggering callback");
                            if (s_short_press_cb) {
                                s_short_press_cb();
                            }
                        }
                        // Medium press (1-10 sec) is ignored

                        s_button_pressed = false;
                    }
                }
            }
        }

        // Check for long press while button is still held
        if (s_button_pressed) {
            int64_t press_duration = now - s_press_start_time;
            if (press_duration >= CONFIG_BUTTON_LONG_PRESS_MS) {
                // Long press reached while still holding
                ESP_LOGW(TAG, "Long press threshold reached!");
                s_button_pressed = false;  // Prevent re-triggering

                if (s_long_press_cb) {
                    s_long_press_cb();
                }
            }
        }
    }
}

esp_err_t button_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing button on GPIO%d (active %s)",
             CONFIG_BUTTON_GPIO, BUTTON_PRESSED_LEVEL ? "HIGH" : "LOW");

    // Configure button GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_BUTTON_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = CONFIG_BUTTON_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Create button task
    xTaskCreate(button_task, "button", 2048, NULL, 10, &s_button_task);

    // Install GPIO ISR service
    gpio_install_isr_service(0);

    // Add ISR handler
    gpio_isr_handler_add(CONFIG_BUTTON_GPIO, button_isr_handler, NULL);

    ESP_LOGI(TAG, "Button handler initialized (long press = %d ms)",
             CONFIG_BUTTON_LONG_PRESS_MS);

    return ESP_OK;
}

void button_handler_set_short_press_cb(void (*cb)(void))
{
    s_short_press_cb = cb;
}

void button_handler_set_long_press_cb(void (*cb)(void))
{
    s_long_press_cb = cb;
}
