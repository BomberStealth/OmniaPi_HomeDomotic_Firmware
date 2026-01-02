#include "led_status.h"
#include "espnow_handler.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_PIN GPIO_NUM_8
#define LED_ON  0  // Active LOW
#define LED_OFF 1

#define HEARTBEAT_TIMEOUT_MS 10000

static uint32_t s_last_blink = 0;
static uint8_t s_blink_count = 0;
static bool s_led_state = false;

void led_status_init(void) {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, LED_OFF);
}

void led_blink(uint8_t count, uint32_t interval_ms) {
    for (uint8_t i = 0; i < count; i++) {
        gpio_set_level(LED_PIN, LED_ON);
        vTaskDelay(interval_ms / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, LED_OFF);
        vTaskDelay(interval_ms / portTICK_PERIOD_MS);
    }
}

void led_status_update(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t last_hb = espnow_get_last_heartbeat_time();
    bool gateway_known = espnow_is_gateway_known();

    // Determina pattern
    uint8_t pattern_blinks;
    uint32_t pattern_interval;

    if (!gateway_known) {
        // 1 blink lento - nessun gateway
        pattern_blinks = 1;
        pattern_interval = 1000;
    } else if ((now - last_hb) > HEARTBEAT_TIMEOUT_MS) {
        // 3 blinks - gateway perso
        pattern_blinks = 3;
        pattern_interval = 200;
    } else {
        // 2 blinks - operativo
        pattern_blinks = 2;
        pattern_interval = 200;
    }

    // Esegui pattern ogni 2 secondi
    if ((now - s_last_blink) > 2000) {
        led_blink(pattern_blinks, pattern_interval);
        s_last_blink = now;
    }
}
