/**
 * OmniaPi Gateway - BLE WiFi Provisioning Implementation
 *
 * Uses ESP-IDF wifi_provisioning component with BLE transport (protocomm_ble).
 * When provisioning completes, saves WiFi credentials to NVS and reboots.
 */

#include "ble_prov.h"
#include "config_manager.h"
#include "nvs_storage.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_netif.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "BLE_PROV";

#define WIFI_MAX_RETRY   3
#define WIFI_CONNECT_TIMEOUT_MS  10000

// ============================================================================
// Event handler for provisioning
// ============================================================================

static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started - waiting for BLE client...");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received WiFi credentials: SSID=%s", (char *)cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Auth error" : "AP not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful!");
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended - cleaning up");
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // WiFi connected via provisioning - save and reboot
        ESP_LOGI(TAG, "WiFi connected after provisioning - saving and rebooting...");
    }
}

// ============================================================================
// Custom BLE Endpoint: MQTT Configuration
// ============================================================================

/**
 * Handler for "custom-mqtt" BLE endpoint.
 * Receives JSON: {"broker":"mqtt://192.168.1.252:1883"}
 * Saves MQTT broker URI to NVS via config_manager.
 */
static esp_err_t custom_mqtt_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                      uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf == NULL || inlen <= 0) {
        ESP_LOGE(TAG, "custom-mqtt: empty input");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "custom-mqtt: received %d bytes", (int)inlen);

    cJSON *json = cJSON_ParseWithLength((const char *)inbuf, inlen);
    if (json == NULL) {
        ESP_LOGE(TAG, "custom-mqtt: invalid JSON");
        const char *resp = "{\"status\":\"error\",\"message\":\"invalid JSON\"}";
        *outlen = strlen(resp);
        *outbuf = malloc(*outlen);
        memcpy(*outbuf, resp, *outlen);
        return ESP_OK;
    }

    cJSON *broker = cJSON_GetObjectItem(json, "broker");
    if (!broker || !cJSON_IsString(broker) || strlen(broker->valuestring) == 0) {
        ESP_LOGE(TAG, "custom-mqtt: missing 'broker' field");
        cJSON_Delete(json);
        const char *resp = "{\"status\":\"error\",\"message\":\"missing broker\"}";
        *outlen = strlen(resp);
        *outbuf = malloc(*outlen);
        memcpy(*outbuf, resp, *outlen);
        return ESP_OK;
    }

    esp_err_t err = config_set_mqtt(broker->valuestring, NULL, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT broker URI saved: %s", broker->valuestring);
        const char *resp = "{\"status\":\"ok\"}";
        *outlen = strlen(resp);
        *outbuf = malloc(*outlen);
        memcpy(*outbuf, resp, *outlen);
    } else {
        ESP_LOGE(TAG, "Failed to save MQTT config: %s", esp_err_to_name(err));
        const char *resp = "{\"status\":\"error\",\"message\":\"save failed\"}";
        *outlen = strlen(resp);
        *outbuf = malloc(*outlen);
        memcpy(*outbuf, resp, *outlen);
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// ============================================================================
// Public API
// ============================================================================

bool ble_prov_needed(bool eth_connected)
{
    if (eth_connected) {
        ESP_LOGI(TAG, "Ethernet connected - BLE provisioning NOT needed");
        return false;
    }

    // Check if WiFi credentials exist in NVS
    const config_wifi_sta_t *wifi_cfg = config_get_wifi_sta();
    if (wifi_cfg->configured && strlen(wifi_cfg->ssid) > 0) {
        ESP_LOGI(TAG, "WiFi credentials found in NVS (SSID=%s) - will try WiFi first", wifi_cfg->ssid);
        return false;
    }

    ESP_LOGW(TAG, "No Ethernet, no WiFi credentials - BLE provisioning NEEDED");
    return true;
}

esp_err_t ble_prov_start(void)
{
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║       BLE WiFi Provisioning Mode                  ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════╝");

    // Register provisioning event handler
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &prov_event_handler, NULL));

    // Create default WiFi STA interface (needed for WiFi scan during provisioning)
    esp_netif_create_default_wifi_sta();

    // Initialize and start WiFi in STA mode (required for prov-scan endpoint)
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize provisioning manager with BLE transport
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // Check if already provisioned
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned via BLE - reading stored WiFi credentials...");

        // WiFi is already initialized and running (STA mode from above)
        // Read credentials stored by wifi_prov_mgr in WiFi's NVS namespace
        wifi_config_t wifi_cfg;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

        if (strlen((char *)wifi_cfg.sta.ssid) > 0) {
            // Save to config_manager so next boot has prov_state = WIFI_ONLY
            config_set_wifi_sta((char *)wifi_cfg.sta.ssid, (char *)wifi_cfg.sta.password);
            ESP_LOGI(TAG, "WiFi credentials saved to config: SSID=%s", wifi_cfg.sta.ssid);
        } else {
            // Edge case: provisioned flag set but no creds - reset so BLE prov can re-run
            ESP_LOGW(TAG, "No WiFi creds found despite provisioned flag - resetting provisioning");
            wifi_prov_mgr_reset_provisioning();
        }

        wifi_prov_mgr_deinit();

        // Reboot to restart app_main with correct provision state
        ESP_LOGI(TAG, "Rebooting to apply WiFi credentials...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return ESP_OK;  // Never reached
    }

    // Generate BLE device name: "OmniaPi-XXXX"
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ble_name[20];
    snprintf(ble_name, sizeof(ble_name), "OmniaPi-%02X%02X", mac[4], mac[5]);

    ESP_LOGI(TAG, "BLE device name: %s", ble_name);
    ESP_LOGI(TAG, "Security: sec0 (no encryption)");

    // Set BLE service UUID (custom for OmniaPi)
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    // Create custom endpoint BEFORE starting provisioning
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create("custom-mqtt"));

    // Start provisioning without security (sec0 - no encryption)
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_0,
        NULL,
        ble_name,
        NULL
    ));

    // Register custom endpoint handler AFTER starting provisioning
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_register(
        "custom-mqtt", custom_mqtt_handler, NULL));

    ESP_LOGI(TAG, "BLE provisioning active (with WiFi scan + MQTT config)");
    ESP_LOGI(TAG, "Endpoints: prov-session, prov-config, prov-scan, proto-ver, custom-mqtt");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Wait for provisioning to complete (blocks)
    wifi_prov_mgr_wait();

    // Provisioning complete - credentials are auto-saved by wifi_prov_mgr
    // Also save to our config_manager for consistency
    wifi_config_t wifi_cfg;
    esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    if (strlen((char *)wifi_cfg.sta.ssid) > 0) {
        config_set_wifi_sta((char *)wifi_cfg.sta.ssid, (char *)wifi_cfg.sta.password);
        ESP_LOGI(TAG, "WiFi credentials saved to config: SSID=%s", wifi_cfg.sta.ssid);
    }

    ESP_LOGI(TAG, "Rebooting to apply WiFi credentials...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;  // Never reached
}
