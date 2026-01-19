#include "effects.h"
#include "led_controller.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "EFFECTS";

// Effect context
static effect_ctx_t s_ctx = {
    .type = EFFECT_TYPE_STATIC,
    .speed = 128,
    .r = 255,
    .g = 255,
    .b = 255,
    .brightness = 255,
    .step = 0,
    .last_update = 0,
    // Custom colors default (red, green, blue)
    .custom_r1 = 255, .custom_g1 = 0, .custom_b1 = 0,
    .custom_r2 = 0, .custom_g2 = 255, .custom_b2 = 0,
    .custom_r3 = 0, .custom_g3 = 0, .custom_b3 = 255
};

// Flag to force re-render when parameters change
static bool s_dirty = true;

// ============================================
// HELPER FUNCTIONS
// ============================================

// Convert HSV to RGB
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

// Get interval based on speed (255=fast=10ms, 0=slow=200ms)
static uint32_t get_interval_ms(void) {
    // Map speed 0-255 to interval 200-10ms
    return 200 - (s_ctx.speed * 190 / 255);
}

// ============================================
// EFFECT IMPLEMENTATIONS
// ============================================

// Static color - no animation
void effect_static(effect_ctx_t* ctx) {
    for (int i = 0; i < led_num_leds; i++) {
        led_set_pixel(i, ctx->r, ctx->g, ctx->b);
    }
}

// Rainbow cycle
void effect_rainbow(effect_ctx_t* ctx) {
    for (int i = 0; i < led_num_leds; i++) {
        uint16_t hue = (ctx->step + (i * 256 / led_num_leds)) % 256;
        uint8_t r, g, b;
        hsv_to_rgb(hue, 255, ctx->brightness, &r, &g, &b);
        led_set_pixel(i, r, g, b);
    }
    // Speed-based step increment: speed 0 = +1, speed 255 = +8
    uint8_t step_inc = 1 + (ctx->speed * 7 / 255);
    ctx->step = (ctx->step + step_inc) % 256;
}

// Breathing/pulse effect
void effect_breathing(effect_ctx_t* ctx) {
    // Sine wave breathing (0-255-0)
    float phase = (float)ctx->step / 128.0f * 3.14159f;
    uint8_t breath = (uint8_t)(sin(phase) * 127 + 128);

    uint8_t r = (ctx->r * breath) / 255;
    uint8_t g = (ctx->g * breath) / 255;
    uint8_t b = (ctx->b * breath) / 255;

    for (int i = 0; i < led_num_leds; i++) {
        led_set_pixel(i, r, g, b);
    }

    ctx->step = (ctx->step + 1) % 256;
}

// Chase/running light
void effect_chase(effect_ctx_t* ctx) {
    // Clear all
    for (int i = 0; i < led_num_leds; i++) {
        led_set_pixel(i, 0, 0, 0);
    }

    // Light up 3 consecutive LEDs
    int pos = ctx->step % led_num_leds;
    for (int j = 0; j < 3; j++) {
        int idx = (pos + j) % led_num_leds;
        // Fade effect for tail
        uint8_t fade = 255 - (j * 80);
        uint8_t r = (ctx->r * fade) / 255;
        uint8_t g = (ctx->g * fade) / 255;
        uint8_t b = (ctx->b * fade) / 255;
        led_set_pixel(idx, r, g, b);
    }

    ctx->step = (ctx->step + 1) % led_num_leds;
}

// Random sparkle
void effect_sparkle(effect_ctx_t* ctx) {
    // Dim all LEDs slightly
    for (int i = 0; i < led_num_leds; i++) {
        led_set_pixel(i, ctx->r / 10, ctx->g / 10, ctx->b / 10);
    }

    // Light up 2-3 random LEDs brightly
    for (int j = 0; j < 3; j++) {
        int idx = esp_random() % led_num_leds;
        led_set_pixel(idx, ctx->r, ctx->g, ctx->b);
    }
}

// Fire simulation - heat buffer (allocated dynamically)
static uint8_t *fire_heat = NULL;
static uint16_t fire_heat_size = 0;

void effect_fire(effect_ctx_t* ctx) {
    // Reallocate heat buffer if needed
    if (fire_heat == NULL || fire_heat_size != led_num_leds) {
        if (fire_heat != NULL) {
            free(fire_heat);
        }
        fire_heat = (uint8_t*)calloc(led_num_leds, sizeof(uint8_t));
        fire_heat_size = led_num_leds;
    }

    if (fire_heat == NULL) return;  // Allocation failed

    // Cool down every cell a little
    for (int i = 0; i < led_num_leds; i++) {
        uint8_t cooldown = (esp_random() % 30) + 5;
        if (fire_heat[i] > cooldown) {
            fire_heat[i] -= cooldown;
        } else {
            fire_heat[i] = 0;
        }
    }

    // Heat from bottom rises up
    for (int i = led_num_leds - 1; i >= 2; i--) {
        fire_heat[i] = (fire_heat[i - 1] + fire_heat[i - 2] + fire_heat[i - 2]) / 3;
    }

    // Randomly ignite new sparks near bottom
    if ((esp_random() % 10) < 5) {
        int y = esp_random() % 3;
        fire_heat[y] = fire_heat[y] + (esp_random() % 64) + 160;
        if (fire_heat[y] > 255) fire_heat[y] = 255;
    }

    // Map heat to LED colors
    for (int i = 0; i < led_num_leds; i++) {
        uint8_t h = fire_heat[i];
        uint8_t r, g, b;

        // Heat to color mapping (black -> red -> yellow -> white)
        if (h < 85) {
            r = h * 3;
            g = 0;
            b = 0;
        } else if (h < 170) {
            r = 255;
            g = (h - 85) * 3;
            b = 0;
        } else {
            r = 255;
            g = 255;
            b = (h - 170) * 3;
        }

        led_set_pixel(i, r, g, b);
    }
}

// Custom 3-color rainbow - smooth transitions between 3 user-selected colors
void effect_custom_rainbow(effect_ctx_t* ctx) {
    // Each LED gets a color based on position and animation step
    // Divide strip into 3 zones, smoothly transitioning between colors

    for (int i = 0; i < led_num_leds; i++) {
        // Calculate position in the color cycle (0-767 = 3*256)
        uint16_t pos = (ctx->step + (i * 768 / led_num_leds)) % 768;

        uint8_t r, g, b;

        if (pos < 256) {
            // Transition from color1 to color2
            uint8_t blend = pos;
            r = ((256 - blend) * ctx->custom_r1 + blend * ctx->custom_r2) >> 8;
            g = ((256 - blend) * ctx->custom_g1 + blend * ctx->custom_g2) >> 8;
            b = ((256 - blend) * ctx->custom_b1 + blend * ctx->custom_b2) >> 8;
        } else if (pos < 512) {
            // Transition from color2 to color3
            uint8_t blend = pos - 256;
            r = ((256 - blend) * ctx->custom_r2 + blend * ctx->custom_r3) >> 8;
            g = ((256 - blend) * ctx->custom_g2 + blend * ctx->custom_g3) >> 8;
            b = ((256 - blend) * ctx->custom_b2 + blend * ctx->custom_b3) >> 8;
        } else {
            // Transition from color3 back to color1
            uint8_t blend = pos - 512;
            r = ((256 - blend) * ctx->custom_r3 + blend * ctx->custom_r1) >> 8;
            g = ((256 - blend) * ctx->custom_g3 + blend * ctx->custom_g1) >> 8;
            b = ((256 - blend) * ctx->custom_b3 + blend * ctx->custom_b1) >> 8;
        }

        led_set_pixel(i, r, g, b);
    }

    // Speed-based step increment: speed 0 = +2, speed 255 = +16
    uint8_t step_inc = 2 + (ctx->speed * 14 / 255);
    ctx->step = (ctx->step + step_inc) % 768;
}

// ============================================
// PUBLIC FUNCTIONS
// ============================================

void effects_init(void) {
    s_ctx.step = 0;
    s_ctx.last_update = 0;
    ESP_LOGI(TAG, "Effects initialized");
}

void effects_set_type(effect_type_t type) {
    if (type >= EFFECT_TYPE_MAX) return;
    s_ctx.type = type;
    s_ctx.step = 0;  // Reset animation
    s_dirty = true;  // Force re-render
    ESP_LOGI(TAG, "Effect type set: %d", type);
}

void effects_set_speed(uint8_t speed) {
    s_ctx.speed = speed;
}

void effects_set_color(uint8_t r, uint8_t g, uint8_t b) {
    s_ctx.r = r;
    s_ctx.g = g;
    s_ctx.b = b;
    s_dirty = true;  // Force re-render for static effect
}

void effects_set_brightness(uint8_t brightness) {
    s_ctx.brightness = brightness;
    s_dirty = true;  // Force re-render for static effect
}

bool effects_update(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t interval = get_interval_ms();

    // Static effect only needs update when dirty (parameters changed)
    if (s_ctx.type == EFFECT_TYPE_STATIC) {
        if (s_dirty) {
            effect_static(&s_ctx);
            s_ctx.last_update = now;
            s_dirty = false;
            return true;
        }
        return false;
    }

    // Check if enough time has passed
    if ((now - s_ctx.last_update) < interval) {
        return false;
    }

    s_ctx.last_update = now;

    // Run the appropriate effect
    switch (s_ctx.type) {
        case EFFECT_TYPE_STATIC:
            effect_static(&s_ctx);
            break;
        case EFFECT_TYPE_RAINBOW:
            effect_rainbow(&s_ctx);
            break;
        case EFFECT_TYPE_BREATHING:
            effect_breathing(&s_ctx);
            break;
        case EFFECT_TYPE_CHASE:
            effect_chase(&s_ctx);
            break;
        case EFFECT_TYPE_SPARKLE:
            effect_sparkle(&s_ctx);
            break;
        case EFFECT_TYPE_FIRE:
            effect_fire(&s_ctx);
            break;
        case EFFECT_TYPE_CUSTOM:
            effect_custom_rainbow(&s_ctx);
            break;
        default:
            effect_static(&s_ctx);
            break;
    }

    return true;
}

effect_ctx_t* effects_get_ctx(void) {
    return &s_ctx;
}

void effects_reset(void) {
    s_ctx.step = 0;
    s_ctx.last_update = 0;
    s_dirty = true;  // Force re-render
}

void effects_set_num_leds(uint16_t num) {
    // Reset fire buffer when LED count changes
    if (fire_heat != NULL) {
        free(fire_heat);
        fire_heat = NULL;
        fire_heat_size = 0;
    }
    // Reset animation state
    effects_reset();
    ESP_LOGI(TAG, "Effects num_leds updated: %d", num);
}

void effects_set_custom_colors(uint8_t r1, uint8_t g1, uint8_t b1,
                               uint8_t r2, uint8_t g2, uint8_t b2,
                               uint8_t r3, uint8_t g3, uint8_t b3) {
    s_ctx.custom_r1 = r1; s_ctx.custom_g1 = g1; s_ctx.custom_b1 = b1;
    s_ctx.custom_r2 = r2; s_ctx.custom_g2 = g2; s_ctx.custom_b2 = b2;
    s_ctx.custom_r3 = r3; s_ctx.custom_g3 = g3; s_ctx.custom_b3 = b3;
    s_dirty = true;  // Force re-render
    ESP_LOGI(TAG, "Custom colors set: (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)",
             r1, g1, b1, r2, g2, b2, r3, g3, b3);
}
