#include "led_controller.h"
#include "effects.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"

static const char *TAG = "LED_CTRL";

// NVS keys
#define NVS_NAMESPACE "led_state"
#define NVS_KEY_POWER "power"
#define NVS_KEY_R "r"
#define NVS_KEY_G "g"
#define NVS_KEY_B "b"
#define NVS_KEY_BRIGHTNESS "bright"
#define NVS_KEY_EFFECT "effect"
#define NVS_KEY_SPEED "speed"
#define NVS_KEY_NUM_LEDS "num_leds"

// LED strip handle
static led_strip_handle_t s_led_strip = NULL;

// Number of LEDs (dynamic, default 30)
uint16_t led_num_leds = LED_STRIP_DEFAULT_LEDS;

// Current state
static led_state_t s_state = {
    .power = false,
    .r = 255,
    .g = 255,
    .b = 255,
    .brightness = 255,
    .effect_id = 0,  // EFFECT_STATIC
    .effect_speed = 128
};

// ============================================
// LED STRIP INIT (RMT driver via ESP-IDF component)
// ============================================

// Internal function to create LED strip with current led_num_leds
static esp_err_t led_strip_create(void) {
    if (s_led_strip != NULL) {
        // Deinitialize existing strip
        led_strip_clear(s_led_strip);
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }

    ESP_LOGI(TAG, "Creating LED strip: GPIO=%d, LEDs=%d", LED_STRIP_GPIO, led_num_leds);

    // Configure LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = led_num_leds,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,  // WS2812B uses GRB
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // Configure RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES,
        .flags.with_dma = false,
    };

    // Create LED strip
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }

    // Clear all LEDs
    led_strip_clear(s_led_strip);
    return ESP_OK;
}

void led_controller_init(void) {
    // Load num_leds from NVS first (before creating strip)
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint16_t saved_num = 0;
        if (nvs_get_u16(handle, NVS_KEY_NUM_LEDS, &saved_num) == ESP_OK) {
            if (saved_num > 0 && saved_num <= LED_STRIP_MAX_LEDS) {
                led_num_leds = saved_num;
                ESP_LOGI(TAG, "Loaded num_leds from NVS: %d", led_num_leds);
            }
        }
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Initializing LED strip: GPIO=%d, LEDs=%d", LED_STRIP_GPIO, led_num_leds);

    // Create LED strip
    ESP_ERROR_CHECK(led_strip_create());

    // Initialize effects system
    effects_init();

    // Load saved state from NVS
    led_load_state();

    ESP_LOGI(TAG, "LED strip initialized. Power=%d, RGB=%d,%d,%d, Bright=%d, Effect=%d, NumLEDs=%d",
             s_state.power, s_state.r, s_state.g, s_state.b,
             s_state.brightness, s_state.effect_id, led_num_leds);

    // Apply loaded state
    if (s_state.power) {
        effects_set_color(s_state.r, s_state.g, s_state.b);
        effects_set_brightness(s_state.brightness);
        effects_set_type((effect_type_t)s_state.effect_id);
        effects_set_speed(s_state.effect_speed);
    }
}

// ============================================
// POWER CONTROL
// ============================================

void led_set_power_on(void) {
    s_state.power = true;
    effects_set_color(s_state.r, s_state.g, s_state.b);
    effects_set_brightness(s_state.brightness);
    effects_set_type((effect_type_t)s_state.effect_id);
    ESP_LOGI(TAG, "LED Power ON");
}

void led_set_power_off(void) {
    s_state.power = false;
    led_clear();
    led_refresh();
    ESP_LOGI(TAG, "LED Power OFF");
}

// ============================================
// COLOR & BRIGHTNESS
// ============================================

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    s_state.r = r;
    s_state.g = g;
    s_state.b = b;
    s_state.power = true;  // Auto power on

    effects_set_color(r, g, b);
    // Set to static effect when color is set directly
    effects_set_type(EFFECT_TYPE_STATIC);
    s_state.effect_id = 0;  // EFFECT_STATIC

    ESP_LOGI(TAG, "Color set: R=%d G=%d B=%d", r, g, b);
}

void led_set_brightness(uint8_t brightness) {
    s_state.brightness = brightness;
    effects_set_brightness(brightness);
    ESP_LOGI(TAG, "Brightness set: %d", brightness);
}

// ============================================
// EFFECTS
// ============================================

void led_set_effect(uint8_t effect_id) {
    if (effect_id >= EFFECT_TYPE_MAX) {
        ESP_LOGW(TAG, "Invalid effect ID: %d", effect_id);
        return;
    }

    s_state.effect_id = effect_id;
    s_state.power = true;  // Auto power on
    effects_set_type((effect_type_t)effect_id);
    effects_reset();  // Reset animation state
    ESP_LOGI(TAG, "Effect set: %d", effect_id);
}

void led_set_effect_speed(uint8_t speed) {
    s_state.effect_speed = speed;
    effects_set_speed(speed);
    ESP_LOGI(TAG, "Effect speed set: %d", speed);
}

void led_set_custom_effect(uint8_t r1, uint8_t g1, uint8_t b1,
                           uint8_t r2, uint8_t g2, uint8_t b2,
                           uint8_t r3, uint8_t g3, uint8_t b3) {
    s_state.effect_id = EFFECT_TYPE_CUSTOM;
    s_state.power = true;  // Auto power on

    // Set the custom colors
    effects_set_custom_colors(r1, g1, b1, r2, g2, b2, r3, g3, b3);

    // Switch to custom effect
    effects_set_type(EFFECT_TYPE_CUSTOM);
    effects_reset();

    ESP_LOGI(TAG, "Custom effect set: (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)",
             r1, g1, b1, r2, g2, b2, r3, g3, b3);
}

// ============================================
// STATE ACCESS
// ============================================

led_state_t* led_get_state(void) {
    return &s_state;
}

// ============================================
// UPDATE (call in main loop)
// ============================================

void led_update(void) {
    if (!s_state.power) return;

    if (effects_update()) {
        led_refresh();
    }
}

// ============================================
// LOW-LEVEL LED FUNCTIONS
// ============================================

void led_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= led_num_leds || s_led_strip == NULL) return;

    // Apply brightness
    uint8_t bright = s_state.brightness;
    r = (r * bright) / 255;
    g = (g * bright) / 255;
    b = (b * bright) / 255;

    led_strip_set_pixel(s_led_strip, index, r, g, b);
}

void led_refresh(void) {
    if (s_led_strip == NULL) return;
    led_strip_refresh(s_led_strip);
}

void led_clear(void) {
    if (s_led_strip == NULL) return;
    led_strip_clear(s_led_strip);
}

// ============================================
// NVS PERSISTENCE
// ============================================

void led_save_state(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_u8(handle, NVS_KEY_POWER, s_state.power ? 1 : 0);
    nvs_set_u8(handle, NVS_KEY_R, s_state.r);
    nvs_set_u8(handle, NVS_KEY_G, s_state.g);
    nvs_set_u8(handle, NVS_KEY_B, s_state.b);
    nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, s_state.brightness);
    nvs_set_u8(handle, NVS_KEY_EFFECT, s_state.effect_id);
    nvs_set_u8(handle, NVS_KEY_SPEED, s_state.effect_speed);

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGD(TAG, "State saved to NVS");
}

void led_load_state(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved state in NVS, using defaults");
        return;
    }

    uint8_t val;
    if (nvs_get_u8(handle, NVS_KEY_POWER, &val) == ESP_OK) s_state.power = (val != 0);
    if (nvs_get_u8(handle, NVS_KEY_R, &val) == ESP_OK) s_state.r = val;
    if (nvs_get_u8(handle, NVS_KEY_G, &val) == ESP_OK) s_state.g = val;
    if (nvs_get_u8(handle, NVS_KEY_B, &val) == ESP_OK) s_state.b = val;
    if (nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &val) == ESP_OK) s_state.brightness = val;
    if (nvs_get_u8(handle, NVS_KEY_EFFECT, &val) == ESP_OK) s_state.effect_id = val;
    if (nvs_get_u8(handle, NVS_KEY_SPEED, &val) == ESP_OK) s_state.effect_speed = val;

    nvs_close(handle);

    ESP_LOGI(TAG, "State loaded from NVS");
}

// ============================================
// NUMBER OF LEDS - DYNAMIC CONFIGURATION
// ============================================

bool led_set_num_leds(uint16_t num) {
    if (num < 1 || num > LED_STRIP_MAX_LEDS) {
        ESP_LOGW(TAG, "Invalid num_leds: %d (must be 1-%d)", num, LED_STRIP_MAX_LEDS);
        return false;
    }

    if (num == led_num_leds) {
        ESP_LOGI(TAG, "num_leds unchanged: %d", num);
        return true;
    }

    ESP_LOGI(TAG, "Setting num_leds: %d -> %d", led_num_leds, num);

    // Save current power state
    bool was_on = s_state.power;

    // Turn off before reinitializing
    if (was_on) {
        led_set_power_off();
    }

    // Update value
    led_num_leds = num;

    // Reinitialize strip with new number
    esp_err_t err = led_strip_create();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reinitialize strip with %d LEDs", num);
        return false;
    }

    // Update effects system with new LED count
    effects_set_num_leds(num);

    // Save to NVS
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u16(handle, NVS_KEY_NUM_LEDS, num);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "num_leds saved to NVS: %d", num);
    }

    // Restore power state
    if (was_on) {
        led_set_power_on();
    }

    ESP_LOGI(TAG, "LED strip reconfigured: %d LEDs", num);
    return true;
}

uint16_t led_get_num_leds(void) {
    return led_num_leds;
}
