/**
 * OmniaPi Node Mesh - LED Strip Device Driver (WS2812B)
 */

#ifndef DEVICE_LED_H
#define DEVICE_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// LED Effects
typedef enum {
    LED_EFFECT_NONE = 0,
    LED_EFFECT_RAINBOW,
    LED_EFFECT_BREATHE,
    LED_EFFECT_FLASH,
    LED_EFFECT_CHASE,
} led_effect_t;

/**
 * Initialize LED strip
 * @return ESP_OK on success
 */
esp_err_t device_led_init(void);

/**
 * Turn LED strip on (with last color/brightness)
 */
void device_led_on(void);

/**
 * Turn LED strip off
 */
void device_led_off(void);

/**
 * Set LED strip color (all LEDs same color)
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 */
void device_led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set LED strip brightness
 * @param brightness Brightness level (0-255)
 */
void device_led_set_brightness(uint8_t brightness);

/**
 * Set LED effect
 * @param effect    Effect type
 * @param speed     Effect speed (ms per step)
 */
void device_led_set_effect(led_effect_t effect, uint16_t speed);

/**
 * Get current LED state
 */
void device_led_get_state(bool *on, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *brightness);

/**
 * Set individual pixel color
 * @param index Pixel index
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 */
void device_led_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * Refresh/update the LED strip
 */
void device_led_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_LED_H
