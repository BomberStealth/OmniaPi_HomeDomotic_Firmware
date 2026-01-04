/**
 * OmniaPi Gateway - WiFi Manager
 * Handles WiFi Station + AP mode with NVS credential storage
 */

#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_manager";

// NVS namespace and keys
#define NVS_NAMESPACE       "wifi_config"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "password"
#define NVS_KEY_CONFIGURED  "configured"

// AP Configuration
#define AP_CHANNEL          1
#define AP_MAX_CONNECTIONS  4
#define AP_PASSWORD         "omniapi123"  // Can be empty for open network

// Connection retry
#define MAX_RETRY           5

// Event group bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// State
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_connected = false;
static esp_netif_ip_info_t s_ip_info;
static wifi_manager_mode_t s_current_mode = WIFI_MODE_NOT_INITIALIZED;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static bool s_initialized = false;

// ============== Event Handler ==============
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                s_connected = false;
                if (s_retry_num < MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retry connection (%d/%d)", s_retry_num, MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "Connection failed after %d retries", MAX_RETRY);
                }
                break;

            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                s_current_mode = WIFI_MODE_AP_ACTIVE;
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Client connected: " MACSTR, MAC2STR(event->mac));
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Client disconnected: " MACSTR, MAC2STR(event->mac));
                break;
            }
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            memcpy(&s_ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            s_connected = true;
            s_current_mode = WIFI_MODE_STA_CONNECTED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

// ============== NVS Functions ==============
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_PASSWORD, password ? password : "");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_CONFIGURED, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving configured flag: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Credentials saved for SSID: %s", ssid);
    }

    return err;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_all(handle);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Credentials cleared");
    return err;
}

bool wifi_manager_has_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    uint8_t configured = 0;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_u8(handle, NVS_KEY_CONFIGURED, &configured);
    nvs_close(handle);

    return (err == ESP_OK && configured == 1);
}

esp_err_t wifi_manager_get_saved_ssid(char *buffer, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_SSID, buffer, &len);
    nvs_close(handle);

    return err;
}

static esp_err_t get_saved_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, NVS_KEY_PASSWORD, password, &pass_len);
    nvs_close(handle);

    return err;
}

// ============== Initialization ==============
esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi Manager");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_wifi_event_group = xEventGroupCreate();

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create netif for both STA and AP
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, NULL));

    // Disable power saving
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi Manager initialized");

    return ESP_OK;
}

// ============== Station Mode ==============
esp_err_t wifi_manager_connect(uint32_t timeout_ms)
{
    if (!wifi_manager_has_credentials()) {
        ESP_LOGE(TAG, "No saved credentials");
        return ESP_ERR_NOT_FOUND;
    }

    char ssid[33] = {0};
    char password[65] = {0};

    esp_err_t err = get_saved_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load credentials");
        return err;
    }

    return wifi_manager_connect_to(ssid, password, timeout_ms);
}

esp_err_t wifi_manager_connect_to(const char *ssid, const char *password, uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Connecting to: %s", ssid);

    s_retry_num = 0;
    s_connected = false;
    s_current_mode = WIFI_MODE_STA_CONNECTING;

    // Clear event bits
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    TickType_t wait_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        wait_ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to: %s", ssid);
        esp_wifi_stop();
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Connection timeout");
    esp_wifi_stop();
    return ESP_ERR_TIMEOUT;
}

// ============== Access Point Mode ==============
void wifi_manager_get_ap_ssid(char *buffer, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(buffer, len, "OmniaPi-%02X%02X", mac[4], mac[5]);
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "Starting Access Point mode");

    char ap_ssid[32];
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    // Configure AP
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.channel = AP_CHANNEL;
    wifi_config.ap.max_connection = AP_MAX_CONNECTIONS;

#ifdef AP_PASSWORD
    if (strlen(AP_PASSWORD) >= 8) {
        strncpy((char *)wifi_config.ap.password, AP_PASSWORD, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
#else
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
#endif

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set AP IP to 192.168.4.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    s_current_mode = WIFI_MODE_AP_ACTIVE;

    ESP_LOGI(TAG, "AP started - SSID: %s, Password: %s, IP: 192.168.4.1",
             ap_ssid, strlen(AP_PASSWORD) >= 8 ? AP_PASSWORD : "(open)");

    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    esp_wifi_stop();
    s_connected = false;
    s_current_mode = WIFI_MODE_NOT_INITIALIZED;
    return ESP_OK;
}

// ============== Status Functions ==============
bool wifi_manager_is_connected(void)
{
    return s_connected;
}

wifi_manager_mode_t wifi_manager_get_mode(void)
{
    return s_current_mode;
}

esp_err_t wifi_manager_get_ip(char *buffer, size_t len)
{
    if (s_current_mode == WIFI_MODE_AP_ACTIVE) {
        snprintf(buffer, len, "192.168.4.1");
        return ESP_OK;
    }

    if (!s_connected || buffer == NULL) {
        snprintf(buffer, len, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(buffer, len, IPSTR, IP2STR(&s_ip_info.ip));
    return ESP_OK;
}

int8_t wifi_manager_get_rssi(void)
{
    if (s_current_mode != WIFI_MODE_STA_CONNECTED) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

uint8_t wifi_manager_get_channel(void)
{
    if (s_current_mode == WIFI_MODE_AP_ACTIVE) {
        return AP_CHANNEL;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.primary;
    }
    return 0;
}

// ============== WiFi Scan ==============
uint16_t wifi_manager_scan(void *ap_records, uint16_t max_records)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI(TAG, "Starting WiFi scan...");

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count > max_records) {
        ap_count = max_records;
    }

    esp_wifi_scan_get_ap_records(&ap_count, (wifi_ap_record_t *)ap_records);

    ESP_LOGI(TAG, "Scan complete, found %d networks", ap_count);
    return ap_count;
}

// ============== Legacy compatibility ==============
// These are for backward compatibility with old code that uses wifi_manager_start()
esp_err_t wifi_manager_start(void)
{
    // Try to connect with saved credentials, fallback to hardcoded if none
    if (wifi_manager_has_credentials()) {
        return wifi_manager_connect(30000);  // 30 second timeout
    }

    // No saved credentials - this would normally go to AP mode
    // but for backward compatibility, try hardcoded credentials
    ESP_LOGW(TAG, "No saved credentials, using defaults");
    return wifi_manager_connect_to("Porte Di Durin", "Mellon!!!", 30000);
}
