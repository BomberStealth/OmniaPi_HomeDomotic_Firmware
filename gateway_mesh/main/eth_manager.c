/**
 * OmniaPi Gateway Mesh - Ethernet Manager Implementation
 */

#include "eth_manager.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ETH_MGR";

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static bool s_connected = false;
static const char *s_init_error = NULL;  // Stores which init step failed

// Callback declaration (implemented in main.c)
extern void on_network_connected(bool is_ethernet);
extern void on_network_disconnected(bool is_ethernet);

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet Link Down");
            s_connected = false;
            on_network_disconnected(true);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        on_network_connected(true);
    } else if (event_id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGW(TAG, "Lost IP address");
        s_connected = false;
        on_network_disconnected(true);
    }
}

esp_err_t eth_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet (WT32-ETH01 / LAN8720)...");
    ESP_LOGI(TAG, "  MDC=GPIO%d, MDIO=GPIO%d, PHY_ADDR=%d, CLK=GPIO0_IN",
             CONFIG_ETH_MDC_GPIO, CONFIG_ETH_MDIO_GPIO, CONFIG_ETH_PHY_ADDR);

    // WT32-ETH01: GPIO16 controls LAN8720 power enable - must be HIGH
    ESP_LOGI(TAG, "Step 0: Enabling PHY power (GPIO16 HIGH)...");
    gpio_config_t phy_pwr_cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_16),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&phy_pwr_cfg);
    gpio_set_level(GPIO_NUM_16, 1);
    vTaskDelay(pdMS_TO_TICKS(20));  // Wait for PHY to power up

    // Create default netif for ethernet
    ESP_LOGI(TAG, "Step 1/5: Creating ETH netif...");
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == NULL) {
        s_init_error = "netif_create_failed";
        ESP_LOGE(TAG, "FAILED step 1: create ETH netif");
        return ESP_FAIL;
    }

    // Configure MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    // WT32-ETH01 specific: clock from GPIO0
    esp32_emac_config.smi_gpio.mdc_num = CONFIG_ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = CONFIG_ETH_MDIO_GPIO;
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;

    ESP_LOGI(TAG, "Step 2/5: Creating EMAC (MDC=%d, MDIO=%d, CLK=GPIO0)...",
             CONFIG_ETH_MDC_GPIO, CONFIG_ETH_MDIO_GPIO);
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (mac == NULL) {
        s_init_error = "emac_create_failed";
        ESP_LOGE(TAG, "FAILED step 2: create EMAC");
        return ESP_FAIL;
    }

    // Configure PHY (LAN8720)
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_ETH_PHY_RST_GPIO;

    ESP_LOGI(TAG, "Step 3/5: Creating PHY (LAN8720, addr=%d, rst=%d)...",
             CONFIG_ETH_PHY_ADDR, CONFIG_ETH_PHY_RST_GPIO);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    if (phy == NULL) {
        s_init_error = "phy_create_failed";
        ESP_LOGE(TAG, "FAILED step 3: create PHY LAN8720");
        return ESP_FAIL;
    }

    // Create driver
    ESP_LOGI(TAG, "Step 4/5: Installing ETH driver...");
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        s_init_error = esp_err_to_name(ret);
        ESP_LOGE(TAG, "FAILED step 4: install driver: %s", s_init_error);
        return ret;
    }

    // Attach netif to driver
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    ESP_LOGI(TAG, "Ethernet initialized OK");
    return ESP_OK;
}

esp_err_t eth_manager_start(void)
{
    if (s_eth_handle == NULL) return ESP_ERR_INVALID_STATE;
    return esp_eth_start(s_eth_handle);
}

esp_err_t eth_manager_stop(void)
{
    if (s_eth_handle == NULL) return ESP_ERR_INVALID_STATE;
    return esp_eth_stop(s_eth_handle);
}

bool eth_manager_is_connected(void)
{
    return s_connected;
}

esp_netif_t *eth_manager_get_netif(void)
{
    return s_eth_netif;
}

const char *eth_manager_get_init_error(void)
{
    return s_init_error;
}

void eth_manager_get_ip(char *ip_str, size_t len)
{
    if (s_eth_netif && s_connected) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_eth_netif, &ip_info);
        snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(ip_str, len, "0.0.0.0");
    }
}
