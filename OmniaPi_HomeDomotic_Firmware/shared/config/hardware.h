/**
 * OmniaPi Hardware Configuration
 *
 * Pin definitions and hardware constants for all device types.
 *
 * @version 1.0.0
 * @date 2025-12-29
 */

#ifndef OMNIAPI_HARDWARE_H
#define OMNIAPI_HARDWARE_H

// ============================================
// WT32-ETH01 GATEWAY CONFIGURATION
// ============================================

#ifdef OMNIAPI_GATEWAY

// Ethernet PHY Configuration (LAN8720)
#define ETH_PHY_TYPE        ETH_PHY_LAN8720
#define ETH_PHY_ADDR        1
#define ETH_PHY_MDC         23
#define ETH_PHY_MDIO        18
#define ETH_PHY_POWER       -1  // Not used
#define ETH_CLK_MODE        ETH_CLOCK_GPIO0_IN

// Status LED (optional, directly on GPIO)
#define GATEWAY_LED_PIN     2

// Button for factory reset (optional)
#define GATEWAY_BUTTON_PIN  0

// Available GPIOs on WT32-ETH01:
// GPIO2  - General purpose (directly accessible)
// GPIO4  - General purpose
// GPIO12 - General purpose
// GPIO14 - General purpose
// GPIO15 - General purpose
// GPIO32 - General purpose
// GPIO33 - General purpose
// GPIO35 - Input only (no pullup)
// GPIO36 - Input only (no pullup)
// GPIO39 - Input only (no pullup)

#endif // OMNIAPI_GATEWAY

// ============================================
// ESP32-C3 NODE CONFIGURATION
// ============================================

#ifdef OMNIAPI_NODE

// Relay pins
// IMPORTANT: GTIWUNG relay has INVERTED logic (LOW = ON, HIGH = OFF)
#define RELAY_1_PIN         1
#define RELAY_2_PIN         2

// Relay logic (inverted for GTIWUNG)
#define RELAY_ON            LOW
#define RELAY_OFF           HIGH

// Button pin (optional, for manual control)
#define BUTTON_1_PIN        9   // Boot button can be used
#define BUTTON_2_PIN        10  // If available

// Status LED (optional)
#define STATUS_LED_PIN      8   // If available, or use built-in

// Debounce time for buttons (ms)
#define BUTTON_DEBOUNCE_MS  50

// Long press time for pairing mode (ms)
#define BUTTON_LONG_PRESS_MS 5000

// Available GPIOs on ESP32-C3 SuperMini:
// GPIO0  - BOOT button (avoid for outputs)
// GPIO1  - General purpose (Relay 1)
// GPIO2  - General purpose (Relay 2)
// GPIO3  - General purpose
// GPIO4  - General purpose
// GPIO5  - General purpose
// GPIO6  - Flash (avoid)
// GPIO7  - Flash (avoid)
// GPIO8  - General purpose / LED
// GPIO9  - Boot strapping (can use with pullup)
// GPIO10 - General purpose
// GPIO20 - USB D+ (if USB used for serial)
// GPIO21 - USB D- (if USB used for serial)

// Maximum number of relay channels per node
#define MAX_RELAY_CHANNELS  2

#endif // OMNIAPI_NODE

// ============================================
// COMMON TIMING CONSTANTS
// ============================================

// ESP-NOW
#define ESPNOW_CHANNEL          1       // WiFi channel for ESP-NOW
#define ESPNOW_SEND_TIMEOUT_MS  100     // Timeout for ESP-NOW send
#define ESPNOW_RETRY_COUNT      3       // Retries for failed sends

// Heartbeat
#define HEARTBEAT_INTERVAL_MS   30000   // 30 seconds
#define HEARTBEAT_TIMEOUT_MS    90000   // 3 missed heartbeats = offline

// Discovery
#define DISCOVERY_INTERVAL_MS   5000    // Broadcast discovery every 5s when searching
#define DISCOVERY_TIMEOUT_MS    30000   // Stop searching after 30s

// Watchdog
#define WATCHDOG_TIMEOUT_S      60      // Reboot if no activity for 60s

// NVS Keys
#define NVS_NAMESPACE           "omniapi"
#define NVS_KEY_NODE_ID         "node_id"
#define NVS_KEY_RELAY_STATE     "relay_state"
#define NVS_KEY_ENCRYPTION_KEY  "enc_key"
#define NVS_KEY_GATEWAY_MAC     "gw_mac"

// ============================================
// LED PATTERNS
// ============================================

typedef enum {
    LED_PATTERN_OFF,            // LED off
    LED_PATTERN_ON,             // LED on solid
    LED_PATTERN_SLOW_BLINK,     // 1 Hz - Searching for gateway
    LED_PATTERN_FAST_BLINK,     // 4 Hz - Pairing mode
    LED_PATTERN_DOUBLE_BLINK,   // Double blink - Connected
    LED_PATTERN_ERROR,          // Triple fast blink - Error
} LedPattern;

// LED timing (ms)
#define LED_SLOW_BLINK_MS       500
#define LED_FAST_BLINK_MS       125
#define LED_DOUBLE_BLINK_ON_MS  100
#define LED_DOUBLE_BLINK_OFF_MS 200
#define LED_DOUBLE_BLINK_GAP_MS 800

#endif // OMNIAPI_HARDWARE_H
