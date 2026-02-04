/**
 * OmniaPi Gateway - Configuration Manager Implementation
 */

#include "config_manager.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CONFIG_MGR";

// ============================================================================
// NVS Keys
// ============================================================================
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_MQTT_URI        "mqtt_uri"
#define NVS_KEY_MQTT_USER       "mqtt_user"
#define NVS_KEY_MQTT_PASS       "mqtt_pass"
#define NVS_KEY_MQTT_CLIENT     "mqtt_client"
#define NVS_KEY_MESH_PASS       "mesh_pass"
#define NVS_KEY_MESH_CHANNEL    "mesh_chan"
#define NVS_KEY_PROVISIONED     "provisioned"

// ============================================================================
// Runtime Configuration (loaded at init)
// ============================================================================
static config_wifi_sta_t s_wifi_sta = {0};
static config_wifi_ap_t s_wifi_ap = {0};
static config_mqtt_t s_mqtt = {0};
static config_mesh_t s_mesh = {0};
static provision_state_t s_provision_state = PROVISION_STATE_UNCONFIGURED;

// Gateway identifiers
static char s_gateway_id[13] = {0};     // 12 hex chars + null
static char s_hostname[20] = {0};       // "omniapi-XXXX" + null
static uint8_t s_mac[6] = {0};

// ============================================================================
// Private Functions
// ============================================================================

static void generate_gateway_identifiers(void)
{
    // Get MAC address
    esp_read_mac(s_mac, ESP_MAC_WIFI_STA);

    // Full gateway ID (12 hex chars)
    snprintf(s_gateway_id, sizeof(s_gateway_id), "%02X%02X%02X%02X%02X%02X",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);

    // Hostname for mDNS (last 4 hex)
    snprintf(s_hostname, sizeof(s_hostname), "omniapi-%02X%02X",
             s_mac[4], s_mac[5]);

    ESP_LOGI(TAG, "Gateway ID: %s, Hostname: %s", s_gateway_id, s_hostname);
}

static void load_wifi_sta_config(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    // Try to load from NVS
    esp_err_t err_ssid = nvs_storage_load_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid));
    esp_err_t err_pass = nvs_storage_load_string(NVS_KEY_WIFI_PASS, pass, sizeof(pass));

    if (err_ssid == ESP_OK && strlen(ssid) > 0) {
        // Use NVS values
        strncpy(s_wifi_sta.ssid, ssid, sizeof(s_wifi_sta.ssid) - 1);
        s_wifi_sta.ssid[sizeof(s_wifi_sta.ssid) - 1] = '\0';

        if (err_pass == ESP_OK) {
            strncpy(s_wifi_sta.password, pass, sizeof(s_wifi_sta.password) - 1);
            s_wifi_sta.password[sizeof(s_wifi_sta.password) - 1] = '\0';
        }
        s_wifi_sta.configured = true;
        ESP_LOGI(TAG, "WiFi STA loaded from NVS: SSID=%s", s_wifi_sta.ssid);
    } else {
        // Fall back to Kconfig defaults
#ifdef CONFIG_MESH_ROUTER_SSID
        strncpy(s_wifi_sta.ssid, CONFIG_MESH_ROUTER_SSID, sizeof(s_wifi_sta.ssid) - 1);
        s_wifi_sta.ssid[sizeof(s_wifi_sta.ssid) - 1] = '\0';
#endif
#ifdef CONFIG_MESH_ROUTER_PASSWD
        strncpy(s_wifi_sta.password, CONFIG_MESH_ROUTER_PASSWD, sizeof(s_wifi_sta.password) - 1);
        s_wifi_sta.password[sizeof(s_wifi_sta.password) - 1] = '\0';
#endif
        s_wifi_sta.configured = false;  // Using defaults, not provisioned
        ESP_LOGI(TAG, "WiFi STA using Kconfig defaults: SSID=%s", s_wifi_sta.ssid);
    }
}

static void load_wifi_ap_config(void)
{
    // Generate AP SSID with MAC suffix
    snprintf(s_wifi_ap.ssid, sizeof(s_wifi_ap.ssid), "OmniaPi_Gateway_%02X%02X",
             s_mac[4], s_mac[5]);

    // Default AP password
    strncpy(s_wifi_ap.password, "omniapi123", sizeof(s_wifi_ap.password) - 1);
    s_wifi_ap.password[sizeof(s_wifi_ap.password) - 1] = '\0';

#ifdef CONFIG_MESH_CHANNEL
    s_wifi_ap.channel = CONFIG_MESH_CHANNEL;
#else
    s_wifi_ap.channel = 6;
#endif

    ESP_LOGI(TAG, "WiFi AP config: SSID=%s, Channel=%d", s_wifi_ap.ssid, s_wifi_ap.channel);
}

static void load_mqtt_config(void)
{
    char uri[128] = {0};
    char user[33] = {0};
    char pass[65] = {0};
    char client[33] = {0};

    // Try to load from NVS
    esp_err_t err_uri = nvs_storage_load_string(NVS_KEY_MQTT_URI, uri, sizeof(uri));

    if (err_uri == ESP_OK && strlen(uri) > 0) {
        // Use NVS values
        strncpy(s_mqtt.broker_uri, uri, sizeof(s_mqtt.broker_uri) - 1);
        s_mqtt.broker_uri[sizeof(s_mqtt.broker_uri) - 1] = '\0';

        if (nvs_storage_load_string(NVS_KEY_MQTT_USER, user, sizeof(user)) == ESP_OK) {
            strncpy(s_mqtt.username, user, sizeof(s_mqtt.username) - 1);
            s_mqtt.username[sizeof(s_mqtt.username) - 1] = '\0';
        }
        if (nvs_storage_load_string(NVS_KEY_MQTT_PASS, pass, sizeof(pass)) == ESP_OK) {
            strncpy(s_mqtt.password, pass, sizeof(s_mqtt.password) - 1);
            s_mqtt.password[sizeof(s_mqtt.password) - 1] = '\0';
        }
        if (nvs_storage_load_string(NVS_KEY_MQTT_CLIENT, client, sizeof(client)) == ESP_OK) {
            strncpy(s_mqtt.client_id, client, sizeof(s_mqtt.client_id) - 1);
            s_mqtt.client_id[sizeof(s_mqtt.client_id) - 1] = '\0';
        }

        s_mqtt.configured = true;
        ESP_LOGI(TAG, "MQTT loaded from NVS: URI=%s", s_mqtt.broker_uri);
    } else {
        // Fall back to Kconfig defaults
#ifdef CONFIG_MQTT_BROKER_URI
        strncpy(s_mqtt.broker_uri, CONFIG_MQTT_BROKER_URI, sizeof(s_mqtt.broker_uri) - 1);
        s_mqtt.broker_uri[sizeof(s_mqtt.broker_uri) - 1] = '\0';
#endif
#ifdef CONFIG_MQTT_USERNAME
        strncpy(s_mqtt.username, CONFIG_MQTT_USERNAME, sizeof(s_mqtt.username) - 1);
        s_mqtt.username[sizeof(s_mqtt.username) - 1] = '\0';
#endif
#ifdef CONFIG_MQTT_PASSWORD
        strncpy(s_mqtt.password, CONFIG_MQTT_PASSWORD, sizeof(s_mqtt.password) - 1);
        s_mqtt.password[sizeof(s_mqtt.password) - 1] = '\0';
#endif
#ifdef CONFIG_MQTT_CLIENT_ID
        strncpy(s_mqtt.client_id, CONFIG_MQTT_CLIENT_ID, sizeof(s_mqtt.client_id) - 1);
        s_mqtt.client_id[sizeof(s_mqtt.client_id) - 1] = '\0';
#endif
        s_mqtt.configured = false;
        ESP_LOGI(TAG, "MQTT using Kconfig defaults: URI=%s", s_mqtt.broker_uri);
    }

    // Auto-generate client_id if empty
    if (strlen(s_mqtt.client_id) == 0) {
        snprintf(s_mqtt.client_id, sizeof(s_mqtt.client_id), "omniapi_gw_%s", s_gateway_id);
    }
}

static void load_mesh_config(void)
{
    char pass[65] = {0};

    // Try to load from NVS
    if (nvs_storage_load_string(NVS_KEY_MESH_PASS, pass, sizeof(pass)) == ESP_OK && strlen(pass) > 0) {
        strncpy(s_mesh.ap_password, pass, sizeof(s_mesh.ap_password) - 1);
        s_mesh.ap_password[sizeof(s_mesh.ap_password) - 1] = '\0';
        ESP_LOGI(TAG, "Mesh password loaded from NVS");
    } else {
#ifdef CONFIG_MESH_AP_PASSWD
        strncpy(s_mesh.ap_password, CONFIG_MESH_AP_PASSWD, sizeof(s_mesh.ap_password) - 1);
        s_mesh.ap_password[sizeof(s_mesh.ap_password) - 1] = '\0';
#else
        strncpy(s_mesh.ap_password, "omniapi_mesh", sizeof(s_mesh.ap_password) - 1);
        s_mesh.ap_password[sizeof(s_mesh.ap_password) - 1] = '\0';
#endif
        ESP_LOGI(TAG, "Mesh password using Kconfig defaults");
    }

    // Channel - try NVS first
    // For now, always use Kconfig
#ifdef CONFIG_MESH_CHANNEL
    s_mesh.channel = CONFIG_MESH_CHANNEL;
#else
    s_mesh.channel = 6;
#endif

#ifdef CONFIG_MESH_MAX_LAYER
    s_mesh.max_layer = CONFIG_MESH_MAX_LAYER;
#else
    s_mesh.max_layer = 6;
#endif

#ifdef CONFIG_MESH_AP_CONNECTIONS
    s_mesh.max_connections = CONFIG_MESH_AP_CONNECTIONS;
#else
    s_mesh.max_connections = 6;
#endif
}

static void determine_provision_state(void)
{
    if (s_wifi_sta.configured && s_mqtt.configured) {
        s_provision_state = PROVISION_STATE_CONFIGURED;
    } else if (s_wifi_sta.configured) {
        s_provision_state = PROVISION_STATE_WIFI_ONLY;
    } else {
        s_provision_state = PROVISION_STATE_UNCONFIGURED;
    }

    ESP_LOGI(TAG, "Provision state: %d (0=unconfig, 1=wifi_only, 2=configured)",
             s_provision_state);
}

// ============================================================================
// Public Functions - Initialization
// ============================================================================

esp_err_t config_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing configuration manager...");

    // Generate gateway identifiers from MAC
    generate_gateway_identifiers();

    // Load all configurations
    load_wifi_ap_config();   // AP config first (uses MAC)
    load_wifi_sta_config();
    load_mqtt_config();
    load_mesh_config();

    // Determine provisioning state
    determine_provision_state();

    ESP_LOGI(TAG, "Configuration manager initialized");
    return ESP_OK;
}

// ============================================================================
// Public Functions - Getters
// ============================================================================

const config_wifi_sta_t* config_get_wifi_sta(void)
{
    return &s_wifi_sta;
}

const config_wifi_ap_t* config_get_wifi_ap(void)
{
    return &s_wifi_ap;
}

const config_mqtt_t* config_get_mqtt(void)
{
    return &s_mqtt;
}

const config_mesh_t* config_get_mesh(void)
{
    return &s_mesh;
}

provision_state_t config_get_provision_state(void)
{
    return s_provision_state;
}

bool config_is_configured(void)
{
    return s_provision_state == PROVISION_STATE_CONFIGURED;
}

const char* config_get_gateway_id(void)
{
    return s_gateway_id;
}

const char* config_get_hostname(void)
{
    return s_hostname;
}

// ============================================================================
// Public Functions - Setters
// ============================================================================

esp_err_t config_set_wifi_sta(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting WiFi STA: SSID=%s", ssid);

    // Save to NVS
    esp_err_t err = nvs_storage_save_string(NVS_KEY_WIFI_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(err));
        return err;
    }

    if (password != NULL) {
        err = nvs_storage_save_string(NVS_KEY_WIFI_PASS, password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(err));
            return err;
        }
    }

    // Update runtime config
    strncpy(s_wifi_sta.ssid, ssid, sizeof(s_wifi_sta.ssid) - 1);
    s_wifi_sta.ssid[sizeof(s_wifi_sta.ssid) - 1] = '\0';

    if (password != NULL) {
        strncpy(s_wifi_sta.password, password, sizeof(s_wifi_sta.password) - 1);
        s_wifi_sta.password[sizeof(s_wifi_sta.password) - 1] = '\0';
    }

    s_wifi_sta.configured = true;

    // Update provision state
    determine_provision_state();

    ESP_LOGI(TAG, "WiFi STA configuration saved");
    return ESP_OK;
}

esp_err_t config_set_mqtt(const char *broker_uri, const char *username, const char *password)
{
    if (broker_uri == NULL || strlen(broker_uri) == 0) {
        ESP_LOGE(TAG, "Invalid MQTT broker URI");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Setting MQTT: URI=%s", broker_uri);

    // Save to NVS
    esp_err_t err = nvs_storage_save_string(NVS_KEY_MQTT_URI, broker_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT URI: %s", esp_err_to_name(err));
        return err;
    }

    if (username != NULL && strlen(username) > 0) {
        nvs_storage_save_string(NVS_KEY_MQTT_USER, username);
    }

    if (password != NULL && strlen(password) > 0) {
        nvs_storage_save_string(NVS_KEY_MQTT_PASS, password);
    }

    // Update runtime config
    strncpy(s_mqtt.broker_uri, broker_uri, sizeof(s_mqtt.broker_uri) - 1);
    s_mqtt.broker_uri[sizeof(s_mqtt.broker_uri) - 1] = '\0';

    if (username != NULL) {
        strncpy(s_mqtt.username, username, sizeof(s_mqtt.username) - 1);
        s_mqtt.username[sizeof(s_mqtt.username) - 1] = '\0';
    }

    if (password != NULL) {
        strncpy(s_mqtt.password, password, sizeof(s_mqtt.password) - 1);
        s_mqtt.password[sizeof(s_mqtt.password) - 1] = '\0';
    }

    s_mqtt.configured = true;

    // Update provision state
    determine_provision_state();

    ESP_LOGI(TAG, "MQTT configuration saved");
    return ESP_OK;
}

esp_err_t config_set_mesh(const char *ap_password, uint8_t channel)
{
    ESP_LOGI(TAG, "Setting Mesh: channel=%d", channel);

    if (ap_password != NULL && strlen(ap_password) > 0) {
        esp_err_t err = nvs_storage_save_string(NVS_KEY_MESH_PASS, ap_password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save mesh password: %s", esp_err_to_name(err));
            return err;
        }

        strncpy(s_mesh.ap_password, ap_password, sizeof(s_mesh.ap_password) - 1);
        s_mesh.ap_password[sizeof(s_mesh.ap_password) - 1] = '\0';
    }

    if (channel >= 1 && channel <= 14) {
        s_mesh.channel = channel;
        // TODO: Save channel to NVS if needed
    }

    ESP_LOGI(TAG, "Mesh configuration saved");
    return ESP_OK;
}

// ============================================================================
// Public Functions - Factory Reset
// ============================================================================

esp_err_t config_factory_reset(void)
{
    ESP_LOGW(TAG, "Performing factory reset - clearing all NVS configuration");

    // Erase all config keys
    nvs_storage_erase(NVS_KEY_WIFI_SSID);
    nvs_storage_erase(NVS_KEY_WIFI_PASS);
    nvs_storage_erase(NVS_KEY_MQTT_URI);
    nvs_storage_erase(NVS_KEY_MQTT_USER);
    nvs_storage_erase(NVS_KEY_MQTT_PASS);
    nvs_storage_erase(NVS_KEY_MQTT_CLIENT);
    nvs_storage_erase(NVS_KEY_MESH_PASS);
    nvs_storage_erase(NVS_KEY_MESH_CHANNEL);
    nvs_storage_erase(NVS_KEY_PROVISIONED);

    ESP_LOGW(TAG, "Factory reset complete - restart to apply");
    return ESP_OK;
}

// ============================================================================
// Public Functions - Utility
// ============================================================================

void config_print_current(void)
{
    ESP_LOGI(TAG, "=== Current Configuration ===");
    ESP_LOGI(TAG, "Gateway ID: %s", s_gateway_id);
    ESP_LOGI(TAG, "Hostname: %s", s_hostname);
    ESP_LOGI(TAG, "Provision State: %d", s_provision_state);
    ESP_LOGI(TAG, "--- WiFi STA ---");
    ESP_LOGI(TAG, "  SSID: %s", s_wifi_sta.ssid);
    ESP_LOGI(TAG, "  Password: %s", s_wifi_sta.configured ? "****" : "(default)");
    ESP_LOGI(TAG, "  Configured: %s", s_wifi_sta.configured ? "YES" : "NO (using defaults)");
    ESP_LOGI(TAG, "--- WiFi AP ---");
    ESP_LOGI(TAG, "  SSID: %s", s_wifi_ap.ssid);
    ESP_LOGI(TAG, "  Channel: %d", s_wifi_ap.channel);
    ESP_LOGI(TAG, "--- MQTT ---");
    ESP_LOGI(TAG, "  Broker: %s", s_mqtt.broker_uri);
    ESP_LOGI(TAG, "  Username: %s", strlen(s_mqtt.username) > 0 ? s_mqtt.username : "(none)");
    ESP_LOGI(TAG, "  Client ID: %s", s_mqtt.client_id);
    ESP_LOGI(TAG, "  Configured: %s", s_mqtt.configured ? "YES" : "NO (using defaults)");
    ESP_LOGI(TAG, "--- Mesh ---");
    ESP_LOGI(TAG, "  Channel: %d", s_mesh.channel);
    ESP_LOGI(TAG, "  Max Layer: %d", s_mesh.max_layer);
    ESP_LOGI(TAG, "  Max Connections: %d", s_mesh.max_connections);
    ESP_LOGI(TAG, "=============================");
}
