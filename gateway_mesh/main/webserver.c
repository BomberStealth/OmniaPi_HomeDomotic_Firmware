/**
 * OmniaPi Gateway Mesh - Web Server Implementation
 */

#include "webserver.h"
#include "web_api.h"
#include "web_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "WEBSERVER";

// WebSocket ping interval (in milliseconds)
#define WS_PING_INTERVAL_MS     15000

// ============================================================================
// State
// ============================================================================
static httpd_handle_t s_server = NULL;
static bool s_running = false;
static TaskHandle_t s_ws_ping_task = NULL;

// Log buffer (circular)
static log_entry_t s_log_buffer[LOG_BUFFER_SIZE];
static int s_log_head = 0;
static int s_log_count = 0;
static SemaphoreHandle_t s_log_mutex = NULL;

// WebSocket clients
static int s_ws_fds[WS_MAX_CLIENTS];
static int s_ws_count = 0;
static SemaphoreHandle_t s_ws_mutex = NULL;

// ============================================================================
// WebSocket Handler
// ============================================================================

static void ws_add_client(int fd)
{
    if (s_ws_mutex && xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100))) {
        bool found = false;
        for (int i = 0; i < s_ws_count; i++) {
            if (s_ws_fds[i] == fd) {
                found = true;
                break;
            }
        }
        if (!found && s_ws_count < WS_MAX_CLIENTS) {
            s_ws_fds[s_ws_count++] = fd;
            ESP_LOGI(TAG, "WebSocket client connected (fd=%d, total=%d)", fd, s_ws_count);
        }
        xSemaphoreGive(s_ws_mutex);
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake (fd=%d)", fd);
        // Register client immediately on handshake
        ws_add_client(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
        ESP_LOGD(TAG, "WS received: %s", buf);
    }

    // Ensure client is in list (may have been removed on error)
    ws_add_client(fd);

    free(buf);
    return ESP_OK;
}

// ============================================================================
// Log Management
// ============================================================================

void webserver_log(const char *format, ...)
{
    if (s_log_mutex == NULL) return;

    char message[LOG_LINE_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100))) {
        log_entry_t *entry = &s_log_buffer[s_log_head];
        entry->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        strncpy(entry->message, message, LOG_LINE_MAX - 1);
        entry->message[LOG_LINE_MAX - 1] = '\0';

        s_log_head = (s_log_head + 1) % LOG_BUFFER_SIZE;
        if (s_log_count < LOG_BUFFER_SIZE) {
            s_log_count++;
        }

        xSemaphoreGive(s_log_mutex);

        // Broadcast to WebSocket clients
        char json[256];
        snprintf(json, sizeof(json), "{\"type\":\"log\",\"ts\":%lu,\"msg\":\"%s\"}",
                 (unsigned long)entry->timestamp, message);
        webserver_ws_broadcast(json);
    }
}

int webserver_get_logs(log_entry_t *entries, int max_entries)
{
    if (s_log_mutex == NULL || entries == NULL) return 0;

    int count = 0;
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100))) {
        count = (s_log_count < max_entries) ? s_log_count : max_entries;
        int start = (s_log_head - count + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;

        for (int i = 0; i < count; i++) {
            int idx = (start + i) % LOG_BUFFER_SIZE;
            memcpy(&entries[i], &s_log_buffer[idx], sizeof(log_entry_t));
        }

        xSemaphoreGive(s_log_mutex);
    }
    return count;
}

void webserver_ws_broadcast(const char *message)
{
    if (s_server == NULL || s_ws_mutex == NULL) return;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)message;
    ws_pkt.len = strlen(message);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100))) {
        for (int i = 0; i < s_ws_count; i++) {
            esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                // Remove disconnected client
                ESP_LOGD(TAG, "WS client %d disconnected", s_ws_fds[i]);
                for (int j = i; j < s_ws_count - 1; j++) {
                    s_ws_fds[j] = s_ws_fds[j + 1];
                }
                s_ws_count--;
                i--;
            }
        }
        xSemaphoreGive(s_ws_mutex);
    }
}

// ============================================================================
// WebSocket Ping Task (keep-alive)
// ============================================================================

static void ws_ping_task(void *arg)
{
    ESP_LOGI(TAG, "WebSocket ping task started");

    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(WS_PING_INTERVAL_MS));

        if (!s_running || s_server == NULL) {
            break;
        }

        // Send ping to all WebSocket clients
        if (s_ws_mutex && xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100))) {
            httpd_ws_frame_t ping_pkt = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_PING,
                .payload = NULL,
                .len = 0
            };

            for (int i = 0; i < s_ws_count; i++) {
                esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &ping_pkt);
                if (ret != ESP_OK) {
                    ESP_LOGD(TAG, "WS ping failed for fd=%d, removing", s_ws_fds[i]);
                    // Remove disconnected client
                    for (int j = i; j < s_ws_count - 1; j++) {
                        s_ws_fds[j] = s_ws_fds[j + 1];
                    }
                    s_ws_count--;
                    i--;
                }
            }
            xSemaphoreGive(s_ws_mutex);
        }
    }

    ESP_LOGI(TAG, "WebSocket ping task stopped");
    s_ws_ping_task = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// HTTP Handlers
// ============================================================================

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    return httpd_resp_send(req, web_ui_get_html(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, web_ui_get_css(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, web_ui_get_js(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ============================================================================
// Server Start/Stop
// ============================================================================

esp_err_t webserver_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    // Create mutexes with error checking
    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
        if (s_log_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create log mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_ws_mutex == NULL) {
        s_ws_mutex = xSemaphoreCreateMutex();
        if (s_ws_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WebSocket mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEBSERVER_PORT;
    config.stack_size = WEBSERVER_STACK_SIZE;
    config.max_uri_handlers = 70;  // 31 API + 31 OPTIONS + 5 static + headroom
    config.max_open_sockets = 7;   // Increased for WebSocket + API calls (max 7 on ESP32)
    config.lru_purge_enable = false;  // Disabled to prevent WebSocket disconnection

    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register static handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = css_handler
    };
    httpd_register_uri_handler(s_server, &css_uri);

    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = js_handler
    };
    httpd_register_uri_handler(s_server, &js_uri);

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler
    };
    httpd_register_uri_handler(s_server, &favicon_uri);

    // Register WebSocket handler
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    // Register API handlers
    web_api_register_handlers(s_server);

    s_running = true;
    ESP_LOGI(TAG, "Web server started successfully");
    webserver_log("Web server started on port %d", WEBSERVER_PORT);

    // Start WebSocket ping task for keep-alive
    if (s_ws_ping_task == NULL) {
        xTaskCreate(ws_ping_task, "ws_ping", 2048, NULL, 3, &s_ws_ping_task);
    }

    return ESP_OK;
}

esp_err_t webserver_stop(void)
{
    if (!s_running || s_server == NULL) {
        return ESP_OK;
    }

    // Stop ping task first
    s_running = false;
    if (s_ws_ping_task != NULL) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_err_t ret = httpd_stop(s_server);
    if (ret == ESP_OK) {
        s_server = NULL;
        s_ws_count = 0;
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ret;
}

bool webserver_is_running(void)
{
    return s_running;
}

httpd_handle_t webserver_get_handle(void)
{
    return s_server;
}
