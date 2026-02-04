/**
 * OmniaPi Gateway Mesh - Web API
 *
 * REST API endpoints for gateway management
 */

#ifndef WEB_API_H
#define WEB_API_H

#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register all API handlers with the HTTP server
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t web_api_register_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // WEB_API_H
