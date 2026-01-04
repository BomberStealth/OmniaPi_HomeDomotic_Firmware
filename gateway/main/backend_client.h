/**
 * OmniaPi Gateway - Backend Client
 * HTTP client for Backend registration and communication
 */

#ifndef BACKEND_CLIENT_H
#define BACKEND_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default Backend URL (PC Windows on local network)
#define DEFAULT_BACKEND_URL "http://192.168.1.253:3000"

/**
 * Initialize the backend client
 * @return ESP_OK on success
 */
esp_err_t backend_client_init(void);

/**
 * Register gateway with the backend
 * Sends MAC address, IP, and firmware version
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t backend_client_register(void);

/**
 * Start registration retry task
 * Will retry registration every 30 seconds until successful
 */
void backend_client_start_registration(void);

/**
 * Check if registration was successful
 * @return true if registered
 */
bool backend_client_is_registered(void);

/**
 * Set custom backend URL
 * @param url Backend URL (will be copied)
 */
void backend_client_set_url(const char *url);

/**
 * Get current backend URL
 * @param buffer Buffer to store URL
 * @param len Buffer length
 */
void backend_client_get_url(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // BACKEND_CLIENT_H
