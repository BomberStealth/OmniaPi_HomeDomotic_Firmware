#include "relay_control.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RELAY";

#define RELAY1_PIN GPIO_NUM_1
#define RELAY2_PIN GPIO_NUM_2
#define RELAY_COUNT 2

static const gpio_num_t relay_pins[RELAY_COUNT] = {RELAY1_PIN, RELAY2_PIN};
static bool relay_states[RELAY_COUNT] = {false, false};

void relay_control_init(void) {
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_reset_pin(relay_pins[i]);
        gpio_set_direction(relay_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(relay_pins[i], 1);  // HIGH = OFF (active-low relay)
        relay_states[i] = false;
    }
    ESP_LOGI(TAG, "Relays initialized (active-low): GPIO%d, GPIO%d", RELAY1_PIN, RELAY2_PIN);
}

void relay_set_state(bool on) {
    // Default: set relay 1
    relay_set_channel(1, on);
}

void relay_set_channel(uint8_t channel, bool on) {
    if (channel < 1 || channel > RELAY_COUNT) {
        ESP_LOGW(TAG, "Invalid channel: %d", channel);
        return;
    }
    int idx = channel - 1;
    relay_states[idx] = on;
    gpio_set_level(relay_pins[idx], on ? 0 : 1);  // Active-low: ON=0, OFF=1
    ESP_LOGI(TAG, "Relay %d -> %s", channel, on ? "ON" : "OFF");
}

bool relay_get_state(void) {
    return relay_states[0];  // Default: relay 1
}

bool relay_get_channel_state(uint8_t channel) {
    if (channel < 1 || channel > RELAY_COUNT) {
        return false;
    }
    return relay_states[channel - 1];
}

void relay_toggle(void) {
    relay_set_channel(1, !relay_states[0]);
}

void relay_toggle_channel(uint8_t channel) {
    if (channel < 1 || channel > RELAY_COUNT) {
        return;
    }
    relay_set_channel(channel, !relay_states[channel - 1]);
}
