#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================
// LED STRIP CONFIGURATION
// ============================================

#define LED_STRIP_GPIO          16      // Data pin (GPIO16)
#define LED_STRIP_DEFAULT_LEDS  5       // Default number of LEDs (safe for first boot)
#define LED_STRIP_MAX_LEDS      300     // Maximum supported LEDs
#define LED_STRIP_RMT_RES       10000000 // RMT resolution (10MHz)

// Current number of LEDs (dynamic, loaded from NVS)
extern uint16_t led_num_leds;

// ============================================
// LED STATE STRUCTURE
// ============================================

typedef struct {
    bool power;             // On/Off state
    uint8_t r, g, b;        // Current color
    uint8_t brightness;     // Brightness (0-255)
    uint8_t effect_id;      // Current effect
    uint8_t effect_speed;   // Effect speed (0-255)
} led_state_t;

// ============================================
// FUNCTION PROTOTYPES
// ============================================

/**
 * Initialize LED strip (RMT driver)
 */
void led_controller_init(void);

/**
 * Turn LED strip on (restore last color/effect)
 */
void led_set_power_on(void);

/**
 * Turn LED strip off
 */
void led_set_power_off(void);

/**
 * Set RGB color (also sets effect to STATIC)
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set brightness
 * @param brightness 0-255
 */
void led_set_brightness(uint8_t brightness);

/**
 * Set effect
 * @param effect_id Effect ID (see EFFECT_* defines)
 */
void led_set_effect(uint8_t effect_id);

/**
 * Set effect speed
 * @param speed 0-255 (0=slow, 255=fast)
 */
void led_set_effect_speed(uint8_t speed);

/**
 * Set custom effect with 3 RGB colors
 */
void led_set_custom_effect(uint8_t r1, uint8_t g1, uint8_t b1,
                           uint8_t r2, uint8_t g2, uint8_t b2,
                           uint8_t r3, uint8_t g3, uint8_t b3);

/**
 * Get current LED state
 */
led_state_t* led_get_state(void);

/**
 * Update LED strip (call in main loop for effects)
 * Should be called every ~20ms for smooth animations
 */
void led_update(void);

/**
 * Save current state to NVS
 */
void led_save_state(void);

/**
 * Load state from NVS
 */
void led_load_state(void);

/**
 * Set a single LED color (internal use)
 */
void led_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * Refresh LED strip (push buffer to hardware)
 */
void led_refresh(void);

/**
 * Clear all LEDs (set to black)
 */
void led_clear(void);

/**
 * Set number of LEDs and reinitialize strip
 * @param num Number of LEDs (1-300)
 * @return true if successful
 */
bool led_set_num_leds(uint16_t num);

/**
 * Get current number of LEDs
 */
uint16_t led_get_num_leds(void);

#endif // LED_CONTROLLER_H
