/**
 * OmniaPi Gateway Mesh - Status LED Driver
 *
 * Visual feedback for system states via onboard LED
 */

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LED Status patterns
 */
typedef enum {
    STATUS_LED_OFF = 0,      // LED off
    STATUS_LED_BOOT,         // Fast blink - booting/initializing
    STATUS_LED_SEARCHING,    // Slow blink - searching for network/nodes
    STATUS_LED_CONNECTED,    // Solid on - connected and operational
    STATUS_LED_ERROR,        // Very fast blink - error state
    STATUS_LED_OTA,          // Double blink - OTA in progress
    STATUS_LED_SCANNING,     // Medium blink - scanning for nodes
} status_led_pattern_t;

/**
 * Initialize status LED
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if disabled
 */
esp_err_t status_led_init(void);

/**
 * Set LED pattern
 * @param pattern Pattern to display
 */
void status_led_set(status_led_pattern_t pattern);

/**
 * Get current LED pattern
 * @return Current pattern
 */
status_led_pattern_t status_led_get(void);

/**
 * Deinitialize status LED (stop task)
 */
void status_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // STATUS_LED_H
