/**
 * OmniaPi Node Mesh - Relay Device Driver
 */

#ifndef DEVICE_RELAY_H
#define DEVICE_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize relay driver
 * Loads saved mode from NVS and initializes hardware
 * @return ESP_OK on success
 */
esp_err_t device_relay_init(void);

/**
 * Set relay control mode (GPIO or UART)
 * @param mode RELAY_MODE_GPIO (0x00) or RELAY_MODE_UART (0x01)
 * @return ESP_OK on success
 */
esp_err_t device_relay_set_mode(uint8_t mode);

/**
 * Get current relay control mode
 * @return RELAY_MODE_GPIO (0x00) or RELAY_MODE_UART (0x01)
 */
uint8_t device_relay_get_mode(void);

/**
 * Set relay state
 * @param channel Relay channel (0-3)
 * @param state   true = ON, false = OFF
 * @return ESP_OK on success
 */
esp_err_t device_relay_set(uint8_t channel, bool state);

/**
 * Toggle relay state
 * @param channel Relay channel (0-3)
 * @return ESP_OK on success
 */
esp_err_t device_relay_toggle(uint8_t channel);

/**
 * Get relay state
 * @param channel Relay channel (0-3)
 * @return true if ON, false if OFF
 */
bool device_relay_get(uint8_t channel);

/**
 * Get all relay states as bitmask
 * @return Bitmask (bit 0 = ch0, bit 1 = ch1, etc.)
 */
uint8_t device_relay_get_all(void);

/**
 * Set all relays from bitmask
 * @param bitmask Bitmask (bit 0 = ch0, bit 1 = ch1, etc.)
 */
void device_relay_set_all(uint8_t bitmask);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_RELAY_H
