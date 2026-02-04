/**
 * OmniaPi Node Mesh - Button Handler
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize button handler
 * @return ESP_OK on success
 */
esp_err_t button_handler_init(void);

/**
 * Set callback for short press
 * @param cb Callback function
 */
void button_handler_set_short_press_cb(void (*cb)(void));

/**
 * Set callback for long press (factory reset)
 * @param cb Callback function
 */
void button_handler_set_long_press_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HANDLER_H
