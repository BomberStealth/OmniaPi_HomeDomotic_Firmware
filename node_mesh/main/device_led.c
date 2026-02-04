/**
 * OmniaPi Node Mesh - LED Strip Device Implementation (WS2812B)
 */

#include "device_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_NODE_DEVICE_TYPE_LED
#include "led_strip.h"

static const char *TAG = "LED_STRIP";

// LED strip handle
static led_strip_handle_t s_led_strip = NULL;

// Current state
static bool s_on = false;
static uint8_t s_r = 255, s_g = 255, s_b = 255;
static uint8_t s_brightness = 255;
static led_effect_t s_effect = LED_EFFECT_NONE;
static uint16_t s_effect_speed = 50;

// Effect task handle
static TaskHandle_t s_effect_task = NULL;

// Apply brightness to color
static uint8_t apply_brightness(uint8_t color)
{
    return (uint8_t)(((uint16_t)color * s_brightness) / 255);
}

// Effect task
static void effect_task(void *arg)
{
    uint16_t step = 0;

    while (1) {
        if (!s_on || s_effect == LED_EFFECT_NONE) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        switch (s_effect) {
            case LED_EFFECT_RAINBOW: {
                // Rainbow cycle effect
                for (int i = 0; i < CONFIG_LED_STRIP_COUNT; i++) {
                    uint16_t hue = (step + i * 360 / CONFIG_LED_STRIP_COUNT) % 360;

                    // HSV to RGB conversion (simplified)
                    uint8_t r, g, b;
                    uint8_t region = hue / 60;
                    uint8_t remainder = (hue - (region * 60)) * 255 / 60;

                    switch (region) {
                        case 0:  r = 255; g = remainder; b = 0; break;
                        case 1:  r = 255 - remainder; g = 255; b = 0; break;
                        case 2:  r = 0; g = 255; b = remainder; break;
                        case 3:  r = 0; g = 255 - remainder; b = 255; break;
                        case 4:  r = remainder; g = 0; b = 255; break;
                        default: r = 255; g = 0; b = 255 - remainder; break;
                    }

                    led_strip_set_pixel(s_led_strip, i,
                                        apply_brightness(r),
                                        apply_brightness(g),
                                        apply_brightness(b));
                }
                led_strip_refresh(s_led_strip);
                step = (step + 5) % 360;
                break;
            }

            case LED_EFFECT_BREATHE: {
                // Breathing effect
                uint8_t br = (step < 128) ? step * 2 : (255 - step) * 2;
                for (int i = 0; i < CONFIG_LED_STRIP_COUNT; i++) {
                    led_strip_set_pixel(s_led_strip, i,
                                        (s_r * br) / 255,
                                        (s_g * br) / 255,
                                        (s_b * br) / 255);
                }
                led_strip_refresh(s_led_strip);
                step = (step + 2) % 256;
                break;
            }

            case LED_EFFECT_FLASH: {
                // Flash effect
                bool on = (step % 2) == 0;
                for (int i = 0; i < CONFIG_LED_STRIP_COUNT; i++) {
                    if (on) {
                        led_strip_set_pixel(s_led_strip, i,
                                            apply_brightness(s_r),
                                            apply_brightness(s_g),
                                            apply_brightness(s_b));
                    } else {
                        led_strip_set_pixel(s_led_strip, i, 0, 0, 0);
                    }
                }
                led_strip_refresh(s_led_strip);
                step++;
                break;
            }

            case LED_EFFECT_CHASE: {
                // Chase effect
                for (int i = 0; i < CONFIG_LED_STRIP_COUNT; i++) {
                    if (i == step % CONFIG_LED_STRIP_COUNT) {
                        led_strip_set_pixel(s_led_strip, i,
                                            apply_brightness(s_r),
                                            apply_brightness(s_g),
                                            apply_brightness(s_b));
                    } else {
                        led_strip_set_pixel(s_led_strip, i, 0, 0, 0);
                    }
                }
                led_strip_refresh(s_led_strip);
                step++;
                break;
            }

            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(s_effect_speed));
    }
}

esp_err_t device_led_init(void)
{
    ESP_LOGI(TAG, "Initializing LED strip: %d LEDs on GPIO%d",
             CONFIG_LED_STRIP_COUNT, CONFIG_LED_STRIP_GPIO);

    // LED strip configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_STRIP_GPIO,
        .max_leds = CONFIG_LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    // RMT configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));

    // Clear all LEDs
    led_strip_clear(s_led_strip);

    // Create effect task
    xTaskCreate(effect_task, "led_effect", 2048, NULL, 5, &s_effect_task);

    ESP_LOGI(TAG, "LED strip initialized");
    return ESP_OK;
}

void device_led_on(void)
{
    s_on = true;

    if (s_effect == LED_EFFECT_NONE) {
        // Static color
        for (int i = 0; i < CONFIG_LED_STRIP_COUNT; i++) {
            led_strip_set_pixel(s_led_strip, i,
                                apply_brightness(s_r),
                                apply_brightness(s_g),
                                apply_brightness(s_b));
        }
        led_strip_refresh(s_led_strip);
    }

    ESP_LOGI(TAG, "LED ON (R=%d G=%d B=%d BR=%d)", s_r, s_g, s_b, s_brightness);
}

void device_led_off(void)
{
    s_on = false;
    led_strip_clear(s_led_strip);
    ESP_LOGI(TAG, "LED OFF");
}

void device_led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    s_r = r;
    s_g = g;
    s_b = b;

    if (s_on && s_effect == LED_EFFECT_NONE) {
        device_led_on();  // Refresh
    }

    ESP_LOGI(TAG, "Color set: R=%d G=%d B=%d", r, g, b);
}

void device_led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;

    if (s_on && s_effect == LED_EFFECT_NONE) {
        device_led_on();  // Refresh
    }

    ESP_LOGI(TAG, "Brightness set: %d", brightness);
}

void device_led_set_effect(led_effect_t effect, uint16_t speed)
{
    s_effect = effect;
    s_effect_speed = speed > 0 ? speed : 50;

    ESP_LOGI(TAG, "Effect set: %d (speed=%d ms)", effect, s_effect_speed);

    if (effect == LED_EFFECT_NONE && s_on) {
        device_led_on();  // Restore static color
    }
}

void device_led_get_state(bool *on, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *brightness)
{
    if (on) *on = s_on;
    if (r) *r = s_r;
    if (g) *g = s_g;
    if (b) *b = s_b;
    if (brightness) *brightness = s_brightness;
}

void device_led_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < CONFIG_LED_STRIP_COUNT && s_led_strip) {
        led_strip_set_pixel(s_led_strip, index,
                            apply_brightness(r),
                            apply_brightness(g),
                            apply_brightness(b));
    }
}

void device_led_refresh(void)
{
    if (s_led_strip) {
        led_strip_refresh(s_led_strip);
    }
}

#else
// Stub implementations when LED is not enabled
esp_err_t device_led_init(void) { return ESP_OK; }
void device_led_on(void) {}
void device_led_off(void) {}
void device_led_set_color(uint8_t r, uint8_t g, uint8_t b) {}
void device_led_set_brightness(uint8_t brightness) {}
void device_led_set_effect(led_effect_t effect, uint16_t speed) {}
void device_led_get_state(bool *on, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *brightness) {
    if (on) *on = false;
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    if (brightness) *brightness = 0;
}
void device_led_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {}
void device_led_refresh(void) {}
#endif
