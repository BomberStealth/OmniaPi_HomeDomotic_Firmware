/**
 * OmniaPi Gateway Mesh - Web Server
 *
 * HTTP server with REST API and WebSocket support
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================
#define WEBSERVER_PORT              80
#define WEBSERVER_MAX_CLIENTS       4
#define WEBSERVER_STACK_SIZE        8192
#define WS_MAX_CLIENTS              4
#define LOG_BUFFER_SIZE             100
#define LOG_LINE_MAX                128

// ============================================================================
// Log Entry Structure
// ============================================================================
typedef struct {
    uint32_t timestamp;
    char message[LOG_LINE_MAX];
} log_entry_t;

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Start the web server
 * @return ESP_OK on success
 */
esp_err_t webserver_start(void);

/**
 * Stop the web server
 * @return ESP_OK on success
 */
esp_err_t webserver_stop(void);

/**
 * Check if webserver is running
 * @return true if running
 */
bool webserver_is_running(void);

/**
 * Get server handle (for registering additional handlers)
 * @return HTTP server handle or NULL
 */
httpd_handle_t webserver_get_handle(void);

/**
 * Add a log entry (will be sent to WebSocket clients)
 * @param format Printf-style format string
 */
void webserver_log(const char *format, ...);

/**
 * Send WebSocket message to all connected clients
 * @param message JSON message to send
 */
void webserver_ws_broadcast(const char *message);

/**
 * Get log buffer for API
 * @param entries Output array of log entries
 * @param max_entries Maximum entries to return
 * @return Number of entries returned
 */
int webserver_get_logs(log_entry_t *entries, int max_entries);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
