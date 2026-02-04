/**
 * OmniaPi Node Mesh - Relay Device Implementation
 * Supports runtime switching between GPIO and UART control modes
 */

#include "device_relay.h"
#include "nvs_storage.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "omniapi_protocol.h"

static const char *TAG = "RELAY";

#ifdef CONFIG_NODE_DEVICE_TYPE_RELAY

// NVS key for relay mode
#define NVS_KEY_RELAY_MODE      "relay_mode"

// Current relay state
static bool relay_state = false;

// Current control mode (GPIO or UART)
static uint8_t s_relay_mode = RELAY_MODE_UART;  // Default to UART

// GPIO configuration
#ifndef CONFIG_RELAY_CH1_GPIO
#define CONFIG_RELAY_CH1_GPIO 2
#endif

static const int relay_gpio = CONFIG_RELAY_CH1_GPIO;

#ifdef CONFIG_RELAY_ACTIVE_HIGH
#define RELAY_ON_LEVEL   1
#define RELAY_OFF_LEVEL  0
#else
#define RELAY_ON_LEVEL   0
#define RELAY_OFF_LEVEL  1
#endif

// UART configuration
#define UART_NUM UART_NUM_1
#define UART_BUF_SIZE 256

#ifndef CONFIG_RELAY_UART_TX_GPIO
#define CONFIG_RELAY_UART_TX_GPIO 21
#endif

#ifndef CONFIG_RELAY_UART_BAUD
#define CONFIG_RELAY_UART_BAUD 9600
#endif

// UART relay commands (works with most Chinese relay modules)
static const uint8_t CMD_RELAY_ON[]  = {0xA0, 0x01, 0x01, 0xA2};
static const uint8_t CMD_RELAY_OFF[] = {0xA0, 0x01, 0x00, 0xA1};

static bool s_gpio_initialized = false;
static bool s_uart_initialized = false;

// ============================================================================
// GPIO Mode Functions
// ============================================================================

static esp_err_t gpio_relay_init(void)
{
    if (s_gpio_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing GPIO relay on GPIO %d (EXACTLY like old firmware)", relay_gpio);

    // EXACT same init as old working firmware:
    gpio_reset_pin(relay_gpio);
    gpio_set_direction(relay_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(relay_gpio, 1);  // HIGH = OFF (active-low relay)

    s_gpio_initialized = true;
    ESP_LOGI(TAG, "Relay initialized (active-low): GPIO%d", relay_gpio);
    return ESP_OK;
}

static void gpio_relay_set(bool state)
{
    // EXACT same as old firmware: ON=0, OFF=1
    gpio_set_level(relay_gpio, state ? 0 : 1);
    ESP_LOGI(TAG, "Relay %d -> %s", relay_gpio, state ? "ON" : "OFF");
}

// ============================================================================
// UART Mode Functions
// ============================================================================

static esp_err_t uart_relay_init(void)
{
    if (s_uart_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing UART relay on GPIO %d @ %d baud",
             CONFIG_RELAY_UART_TX_GPIO, CONFIG_RELAY_UART_BAUD);

    uart_config_t uart_config = {
        .baud_rate = CONFIG_RELAY_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));

    // Only configure TX pin (we don't need RX from relay)
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, CONFIG_RELAY_UART_TX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Send OFF command to ensure known state
    uart_write_bytes(UART_NUM, (const char *)CMD_RELAY_OFF, sizeof(CMD_RELAY_OFF));

    s_uart_initialized = true;
    ESP_LOGI(TAG, "UART relay initialized");
    return ESP_OK;
}

static void uart_relay_set(bool state)
{
    if (state) {
        uart_write_bytes(UART_NUM, (const char *)CMD_RELAY_ON, sizeof(CMD_RELAY_ON));
    } else {
        uart_write_bytes(UART_NUM, (const char *)CMD_RELAY_OFF, sizeof(CMD_RELAY_OFF));
    }
    ESP_LOGI(TAG, "Relay -> %s (UART)", state ? "ON" : "OFF");
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t device_relay_init(void)
{
    ESP_LOGI(TAG, "Initializing relay driver...");

    // Load saved mode from NVS
    uint8_t saved_mode = RELAY_MODE_UART;
    size_t len = sizeof(saved_mode);
    if (nvs_storage_load_blob(NVS_KEY_RELAY_MODE, &saved_mode, &len) == ESP_OK) {
        if (saved_mode == RELAY_MODE_GPIO || saved_mode == RELAY_MODE_UART) {
            s_relay_mode = saved_mode;
            ESP_LOGI(TAG, "Loaded relay mode from NVS: %s",
                     s_relay_mode == RELAY_MODE_GPIO ? "GPIO" : "UART");
        }
    } else {
        ESP_LOGI(TAG, "No saved relay mode, using default: UART");
    }

    // Initialize the current mode
    if (s_relay_mode == RELAY_MODE_GPIO) {
        return gpio_relay_init();
    } else {
        return uart_relay_init();
    }
}

esp_err_t device_relay_set_mode(uint8_t mode)
{
    if (mode != RELAY_MODE_GPIO && mode != RELAY_MODE_UART) {
        ESP_LOGE(TAG, "Invalid relay mode: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (mode == s_relay_mode) {
        ESP_LOGI(TAG, "Relay mode already set to %s",
                 mode == RELAY_MODE_GPIO ? "GPIO" : "UART");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Switching relay mode: %s -> %s",
             s_relay_mode == RELAY_MODE_GPIO ? "GPIO" : "UART",
             mode == RELAY_MODE_GPIO ? "GPIO" : "UART");

    // Initialize new mode if not already done
    if (mode == RELAY_MODE_GPIO) {
        gpio_relay_init();
    } else {
        uart_relay_init();
    }

    // Save to NVS
    esp_err_t ret = nvs_storage_save_blob(NVS_KEY_RELAY_MODE, &mode, sizeof(mode));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save relay mode to NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    s_relay_mode = mode;

    // Apply current state to new mode
    device_relay_set(0, relay_state);

    ESP_LOGI(TAG, "Relay mode changed to %s (saved to NVS)",
             mode == RELAY_MODE_GPIO ? "GPIO" : "UART");
    return ESP_OK;
}

uint8_t device_relay_get_mode(void)
{
    return s_relay_mode;
}

esp_err_t device_relay_set(uint8_t channel, bool state)
{
    if (channel != 0) {
        ESP_LOGW(TAG, "Invalid relay channel: %d (only channel 0 supported)", channel);
        return ESP_ERR_INVALID_ARG;
    }

    relay_state = state;

    if (s_relay_mode == RELAY_MODE_GPIO) {
        gpio_relay_set(state);
    } else {
        uart_relay_set(state);
    }

    return ESP_OK;
}

esp_err_t device_relay_toggle(uint8_t channel)
{
    if (channel != 0) {
        ESP_LOGW(TAG, "Invalid relay channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    return device_relay_set(0, !relay_state);
}

bool device_relay_get(uint8_t channel)
{
    if (channel != 0) {
        return false;
    }
    return relay_state;
}

uint8_t device_relay_get_all(void)
{
    return relay_state ? 0x01 : 0x00;
}

void device_relay_set_all(uint8_t bitmask)
{
    device_relay_set(0, (bitmask & 0x01) != 0);
}

#else
// Stub implementations when relay is not enabled
esp_err_t device_relay_init(void) { return ESP_OK; }
esp_err_t device_relay_set_mode(uint8_t mode) { return ESP_ERR_NOT_SUPPORTED; }
uint8_t device_relay_get_mode(void) { return 0; }
esp_err_t device_relay_set(uint8_t channel, bool state) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t device_relay_toggle(uint8_t channel) { return ESP_ERR_NOT_SUPPORTED; }
bool device_relay_get(uint8_t channel) { return false; }
uint8_t device_relay_get_all(void) { return 0; }
void device_relay_set_all(uint8_t bitmask) {}
#endif
