/**
 * OmniaPi Gateway - Ethernet Manager
 * LAN8720 Ethernet support for WT32-ETH01
 */

#include "eth_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "lwip/inet.h"

static const char *TAG = "eth_manager";

// ============== WT32-ETH01 LAN8720 Pin Configuration ==============
#define ETH_PHY_TYPE        ETH_PHY_LAN8720
#define ETH_PHY_ADDR        1
#define ETH_PHY_MDC_GPIO    23
#define ETH_PHY_MDIO_GPIO   18
#define ETH_PHY_POWER_GPIO  16
#define ETH_PHY_RST_GPIO    -1  // Not used on WT32-ETH01

// Event group bits
#define ETH_CONNECTED_BIT   BIT0
#define ETH_LINK_UP_BIT     BIT1

// State
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static EventGroupHandle_t s_eth_event_group = NULL;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_link_up = false;
static esp_netif_ip_info_t s_ip_info;
static eth_event_callback_t s_callback = NULL;
static uint8_t s_mac[6] = {0};

// ============== Event Handler ==============
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                s_link_up = true;
                xEventGroupSetBits(s_eth_event_group, ETH_LINK_UP_BIT);
                break;

            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet Link Down");
                s_link_up = false;
                s_connected = false;
                xEventGroupClearBits(s_eth_event_group, ETH_CONNECTED_BIT | ETH_LINK_UP_BIT);

                // Notify callback
                if (s_callback) {
                    s_callback(false);
                }
                break;

            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;

            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet Stopped");
                s_link_up = false;
                s_connected = false;
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            memcpy(&s_ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));

            ESP_LOGI(TAG, "Ethernet Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
            ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&event->ip_info.gw));

            s_connected = true;
            xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);

            // Notify callback
            if (s_callback) {
                s_callback(true);
            }
        } else if (event_id == IP_EVENT_ETH_LOST_IP) {
            ESP_LOGW(TAG, "Ethernet Lost IP");
            s_connected = false;
            memset(&s_ip_info, 0, sizeof(s_ip_info));
            xEventGroupClearBits(s_eth_event_group, ETH_CONNECTED_BIT);

            // Notify callback
            if (s_callback) {
                s_callback(false);
            }
        }
    }
}

// ============== Initialization ==============
esp_err_t eth_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Ethernet Manager for WT32-ETH01");

    // Create event group
    s_eth_event_group = xEventGroupCreate();
    if (s_eth_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Note: esp_netif_init() and esp_event_loop_create_default()
    // should already be called by wifi_manager

    // Create default Ethernet netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create netif");
        return ESP_FAIL;
    }

    // Configure PHY power pin (GPIO16 on WT32-ETH01)
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << ETH_PHY_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&gpio_cfg);

    // Power cycle the PHY
    gpio_set_level(ETH_PHY_POWER_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(ETH_PHY_POWER_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

    // Configure ESP32 internal EMAC
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = ETH_PHY_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_PHY_MDIO_GPIO;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(TAG, "Failed to create MAC");
        return ESP_FAIL;
    }

    // Configure PHY (LAN8720)
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "Failed to create PHY");
        return ESP_FAIL;
    }

    // Create Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install driver: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get MAC address
    esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, s_mac);
    ESP_LOGI(TAG, "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);

    // Attach Ethernet driver to netif
    ret = esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach netif: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID,
        &eth_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP,
        &eth_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_ETH_LOST_IP,
        &eth_event_handler, NULL));

    s_initialized = true;
    ESP_LOGI(TAG, "Ethernet Manager initialized");

    return ESP_OK;
}

// ============== Start/Stop ==============
esp_err_t eth_manager_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Ethernet...");

    esp_err_t ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t eth_manager_stop(void)
{
    if (!s_initialized || s_eth_handle == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Ethernet...");

    esp_err_t ret = esp_eth_stop(s_eth_handle);
    s_connected = false;
    s_link_up = false;

    return ret;
}

// ============== Status Functions ==============
bool eth_manager_is_connected(void)
{
    return s_connected;
}

bool eth_manager_is_link_up(void)
{
    return s_link_up;
}

esp_err_t eth_manager_get_ip(char *buffer, size_t len)
{
    if (buffer == NULL || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected) {
        snprintf(buffer, len, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(buffer, len, IPSTR, IP2STR(&s_ip_info.ip));
    return ESP_OK;
}

esp_err_t eth_manager_get_mac(char *buffer, size_t len)
{
    if (buffer == NULL || len < 18) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buffer, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);

    return ESP_OK;
}

void eth_manager_set_callback(eth_event_callback_t callback)
{
    s_callback = callback;
}

int8_t eth_manager_get_rssi(void)
{
    // Wired connection, no RSSI
    return 0;
}
