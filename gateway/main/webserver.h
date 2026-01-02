/**
 * OmniaPi Gateway - Web Server
 * HTTP server with REST API
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and start HTTP server
 * @return ESP_OK on success
 */
esp_err_t webserver_init(void);

/**
 * Stop HTTP server
 * @return ESP_OK on success
 */
esp_err_t webserver_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
