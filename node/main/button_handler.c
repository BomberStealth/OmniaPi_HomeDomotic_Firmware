#include "button_handler.h"
#include "relay_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTON";

#define BUTTON_PIN GPIO_NUM_4
#define DEBOUNCE_TIME_MS 50

static volatile int64_t last_press_time = 0;

// ISR handler - chiamato su falling edge (pressione pulsante)
static void IRAM_ATTR button_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000;  // Convert to ms

    // Debounce: ignora se troppo vicino alla pressione precedente
    if ((now - last_press_time) > DEBOUNCE_TIME_MS) {
        last_press_time = now;
        // Toggle relay 1 direttamente (funziona anche offline)
        relay_toggle_channel(1);
    }
}

void button_handler_init(void)
{
    // Configura GPIO4 come input con pull-up interno
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Falling edge (HIGH->LOW = pressione)
    };
    gpio_config(&io_conf);

    // Installa ISR service e aggiungi handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    ESP_LOGI(TAG, "Button initialized on GPIO%d (pull-up, falling edge)", BUTTON_PIN);
}
