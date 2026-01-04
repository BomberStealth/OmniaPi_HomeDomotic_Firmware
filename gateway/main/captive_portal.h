/**
 * OmniaPi Gateway - Captive Portal
 * Web server for WiFi configuration in AP mode
 */

#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the captive portal web server
 * Should be called after wifi_manager_start_ap()
 * @return ESP_OK on success
 */
esp_err_t captive_portal_start(void);

/**
 * Stop the captive portal web server
 * @return ESP_OK on success
 */
esp_err_t captive_portal_stop(void);

/**
 * Check if captive portal is running
 * @return true if running
 */
bool captive_portal_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // CAPTIVE_PORTAL_H
