/**
 * OmniaPi Gateway - Backend Client
 * HTTP client for Backend registration and communication
 */

#include "backend_client.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "wifi_manager.h"

static const char *TAG = "backend_client";

// Firmware version (from main.c)
#define FIRMWARE_VERSION "1.7.0-idf"

// Registration retry interval (30 seconds)
#define REGISTRATION_RETRY_MS 30000

// Backend URL buffer
static char s_backend_url[128] = DEFAULT_BACKEND_URL;

// Registration state
static bool s_registered = false;
static bool s_task_running = false;

// Forward declaration
static void registration_task(void *pvParameters);

// ============== Initialization ==============
esp_err_t backend_client_init(void)
{
    ESP_LOGI(TAG, "Backend client initialized, URL: %s", s_backend_url);
    return ESP_OK;
}

// ============== URL Configuration ==============
void backend_client_set_url(const char *url)
{
    if (url && strlen(url) > 0) {
        strncpy(s_backend_url, url, sizeof(s_backend_url) - 1);
        s_backend_url[sizeof(s_backend_url) - 1] = '\0';
        ESP_LOGI(TAG, "Backend URL set to: %s", s_backend_url);
    }
}

void backend_client_get_url(char *buffer, size_t len)
{
    strncpy(buffer, s_backend_url, len - 1);
    buffer[len - 1] = '\0';
}

// ============== Registration ==============
esp_err_t backend_client_register(void)
{
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot register");
        return ESP_ERR_INVALID_STATE;
    }

    // Get MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    // Get IP address
    char ip_str[16] = "0.0.0.0";
    wifi_manager_get_ip(ip_str, sizeof(ip_str));

    // Create JSON payload
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"ip\":\"%s\",\"version\":\"%s\"}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        ip_str,
        FIRMWARE_VERSION);

    ESP_LOGI(TAG, "Registering with backend: %s", s_backend_url);
    ESP_LOGI(TAG, "Payload: %s", payload);

    // Build full URL
    char url[192];
    snprintf(url, sizeof(url), "%s/api/gateway/register", s_backend_url);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,  // 10 second timeout
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Set POST data
    esp_http_client_set_post_field(client, payload, strlen(payload));

    // Perform request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d", status_code, content_length);

        if (status_code == 200 || status_code == 201) {
            s_registered = true;
            ESP_LOGI(TAG, "Successfully registered with backend!");
        } else if (status_code == 409) {
            // Gateway already registered - this is fine
            s_registered = true;
            ESP_LOGI(TAG, "Gateway already registered (409)");
        } else {
            ESP_LOGW(TAG, "Registration returned status %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// ============== Registration Task ==============
static void registration_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Registration task started");

    while (!s_registered) {
        // Try to register
        esp_err_t ret = backend_client_register();

        if (ret == ESP_OK && s_registered) {
            ESP_LOGI(TAG, "Registration successful, stopping retry task");
            break;
        }

        // Wait before retry
        ESP_LOGI(TAG, "Registration failed, retrying in %d seconds...", REGISTRATION_RETRY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(REGISTRATION_RETRY_MS));
    }

    s_task_running = false;
    vTaskDelete(NULL);
}

void backend_client_start_registration(void)
{
    if (s_task_running) {
        ESP_LOGW(TAG, "Registration task already running");
        return;
    }

    if (s_registered) {
        ESP_LOGI(TAG, "Already registered, skipping");
        return;
    }

    s_task_running = true;
    xTaskCreate(registration_task, "backend_reg", 4096, NULL, 5, NULL);
}

bool backend_client_is_registered(void)
{
    return s_registered;
}
