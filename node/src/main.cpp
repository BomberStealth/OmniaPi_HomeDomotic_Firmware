/**
 * OmniaPi Node Firmware
 *
 * Main entry point for ESP32-C3 relay nodes.
 * Handles ESP-NOW communication, relay control, and physical buttons.
 *
 * @version 0.1.0
 * @date 2025-12-29
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>

// Define as node before including shared headers
#define OMNIAPI_NODE
#include "../../shared/protocol/messages.h"
#include "../../shared/config/hardware.h"

// ============================================
// GLOBAL VARIABLES
// ============================================

// NVS storage
Preferences preferences;

// Node configuration
uint8_t nodeId = 0;  // 0 = not registered
uint8_t gatewayMac[6] = {0};
bool registered = false;

// Relay states
bool relayStates[MAX_RELAY_CHANNELS] = {false, false};
const uint8_t relayPins[MAX_RELAY_CHANNELS] = {RELAY_1_PIN, RELAY_2_PIN};

// Button handling
uint32_t buttonPressTime = 0;
bool buttonPressed = false;

// Heartbeat
uint32_t lastHeartbeat = 0;

// LED pattern
LedPattern currentLedPattern = LED_PATTERN_SLOW_BLINK;
uint32_t lastLedUpdate = 0;
bool ledState = false;

// Message sequence
uint8_t messageSequence = 0;

// ============================================
// RELAY CONTROL
// ============================================

void setRelay(uint8_t channel, bool state) {
    if (channel >= MAX_RELAY_CHANNELS) return;

    relayStates[channel] = state;

    // IMPORTANT: Relay has inverted logic (LOW = ON, HIGH = OFF)
    digitalWrite(relayPins[channel], state ? RELAY_ON : RELAY_OFF);

    Serial.printf("[RELAY] Channel %d = %s\n", channel + 1, state ? "ON" : "OFF");

    // Save state to NVS for power-loss recovery
    preferences.begin(NVS_NAMESPACE, false);
    preferences.putBool(String("relay_" + String(channel)).c_str(), state);
    preferences.end();
}

void toggleRelay(uint8_t channel) {
    if (channel >= MAX_RELAY_CHANNELS) return;
    setRelay(channel, !relayStates[channel]);
}

void restoreRelayStates() {
    preferences.begin(NVS_NAMESPACE, true);
    for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
        bool state = preferences.getBool(String("relay_" + String(i)).c_str(), false);
        setRelay(i, state);
    }
    preferences.end();
    Serial.println("[RELAY] States restored from NVS");
}

// ============================================
// ESP-NOW COMMUNICATION
// ============================================

void sendStateToGateway() {
    if (!registered) return;

    OmniaPiMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.version = OMNIAPI_PROTOCOL_VERSION;
    msg.header.type = MSG_STATE;
    msg.header.nodeId = nodeId;
    msg.header.sequence = ++messageSequence;

    StatePayload *payload = (StatePayload *)msg.payload;
    payload->channelCount = MAX_RELAY_CHANNELS;
    for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
        payload->states[i] = relayStates[i] ? 1 : 0;
        payload->values[i] = relayStates[i] ? 255 : 0;
    }
    payload->rssi = WiFi.RSSI();
    payload->errorFlags = ERROR_NONE;
    payload->uptime = millis() / 1000;

    msg.header.payloadLen = sizeof(StatePayload);
    omniapi_set_checksum(&msg);

    esp_err_t result = esp_now_send(gatewayMac, (uint8_t *)&msg, sizeof(OmniaPiHeader) + msg.header.payloadLen + 1);
    if (result == ESP_OK) {
        Serial.println("[ESP-NOW] State sent to gateway");
    } else {
        Serial.printf("[ESP-NOW] Send failed: %d\n", result);
    }
}

// ESP-NOW receive callback
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < sizeof(OmniaPiHeader)) {
        return;
    }

    const OmniaPiMessage *msg = (const OmniaPiMessage *)data;

    if (!omniapi_validate(msg)) {
        Serial.println("[ESP-NOW] Invalid checksum");
        return;
    }

    Serial.printf("[ESP-NOW] Received type=0x%02X\n", msg->header.type);

    switch (msg->header.type) {
        case MSG_DISCOVERY:
            // Gateway is looking for nodes
            Serial.println("[ESP-NOW] Discovery request - sending response");
            // TODO: Send discovery response
            break;

        case MSG_REGISTER:
            // Gateway assigning us an ID
            {
                const RegisterPayload *payload = (const RegisterPayload *)msg->payload;
                nodeId = payload->assignedNodeId;
                memcpy(gatewayMac, info->src_addr, 6);
                registered = true;

                // Save to NVS
                preferences.begin(NVS_NAMESPACE, false);
                preferences.putUChar(NVS_KEY_NODE_ID, nodeId);
                preferences.putBytes(NVS_KEY_GATEWAY_MAC, gatewayMac, 6);
                preferences.end();

                Serial.printf("[ESP-NOW] Registered as node %d\n", nodeId);
                currentLedPattern = LED_PATTERN_DOUBLE_BLINK;

                // Send acknowledgment
                // TODO: Send register ACK
            }
            break;

        case MSG_COMMAND:
            // Gateway sending a command
            {
                const CommandPayload *payload = (const CommandPayload *)msg->payload;
                uint8_t channel = payload->channel - 1;  // Convert to 0-indexed

                Serial.printf("[ESP-NOW] Command: ch=%d, action=%d\n", payload->channel, payload->action);

                if (channel < MAX_RELAY_CHANNELS) {
                    switch (payload->action) {
                        case ACTION_OFF:
                            setRelay(channel, false);
                            break;
                        case ACTION_ON:
                            setRelay(channel, true);
                            break;
                        case ACTION_TOGGLE:
                            toggleRelay(channel);
                            break;
                    }

                    // Send updated state back to gateway
                    sendStateToGateway();
                }
            }
            break;

        case MSG_PING:
            // Heartbeat request - send pong
            Serial.println("[ESP-NOW] Ping received - sending pong");
            // TODO: Send pong response
            break;

        default:
            break;
    }
}

void onEspNowSend(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Log send status if needed
}

bool setupEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed!");
        return false;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_register_send_cb(onEspNowSend);

    // If we know the gateway MAC, add it as a peer
    if (registered) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = ESPNOW_CHANNEL;
        peerInfo.encrypt = false;

        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            Serial.println("[ESP-NOW] Failed to add gateway peer");
        }
    }

    Serial.println("[ESP-NOW] Initialized");
    Serial.print("[ESP-NOW] MAC: ");
    Serial.println(WiFi.macAddress());

    return true;
}

// ============================================
// BUTTON HANDLING
// ============================================

void handleButton() {
    // Read button state (assuming active LOW with pullup)
    bool currentState = digitalRead(BUTTON_1_PIN) == LOW;

    if (currentState && !buttonPressed) {
        // Button just pressed
        buttonPressTime = millis();
        buttonPressed = true;
    } else if (!currentState && buttonPressed) {
        // Button released
        uint32_t pressDuration = millis() - buttonPressTime;
        buttonPressed = false;

        if (pressDuration >= BUTTON_LONG_PRESS_MS) {
            // Long press - enter pairing mode
            Serial.println("[BUTTON] Long press - pairing mode");
            currentLedPattern = LED_PATTERN_FAST_BLINK;
            // TODO: Enter pairing mode
        } else if (pressDuration >= BUTTON_DEBOUNCE_MS) {
            // Short press - toggle relay 1
            Serial.println("[BUTTON] Short press - toggle relay 1");
            toggleRelay(0);
            sendStateToGateway();
        }
    }
}

// ============================================
// LED HANDLING
// ============================================

void updateLed() {
    uint32_t now = millis();

    switch (currentLedPattern) {
        case LED_PATTERN_OFF:
            ledState = false;
            break;

        case LED_PATTERN_ON:
            ledState = true;
            break;

        case LED_PATTERN_SLOW_BLINK:
            if (now - lastLedUpdate >= LED_SLOW_BLINK_MS) {
                ledState = !ledState;
                lastLedUpdate = now;
            }
            break;

        case LED_PATTERN_FAST_BLINK:
            if (now - lastLedUpdate >= LED_FAST_BLINK_MS) {
                ledState = !ledState;
                lastLedUpdate = now;
            }
            break;

        case LED_PATTERN_DOUBLE_BLINK:
            // TODO: Implement double blink pattern
            break;

        case LED_PATTERN_ERROR:
            // TODO: Implement error pattern
            break;
    }

    // Write LED state (if LED pin is defined and available)
    #ifdef STATUS_LED_PIN
    digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
    #endif
}

// ============================================
// LOAD CONFIGURATION
// ============================================

void loadConfiguration() {
    preferences.begin(NVS_NAMESPACE, true);

    nodeId = preferences.getUChar(NVS_KEY_NODE_ID, 0);
    if (nodeId > 0) {
        registered = true;
        preferences.getBytes(NVS_KEY_GATEWAY_MAC, gatewayMac, 6);

        Serial.printf("[CONFIG] Loaded node ID: %d\n", nodeId);
        Serial.printf("[CONFIG] Gateway MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      gatewayMac[0], gatewayMac[1], gatewayMac[2],
                      gatewayMac[3], gatewayMac[4], gatewayMac[5]);

        currentLedPattern = LED_PATTERN_DOUBLE_BLINK;
    } else {
        Serial.println("[CONFIG] No saved configuration - waiting for pairing");
        currentLedPattern = LED_PATTERN_SLOW_BLINK;
    }

    preferences.end();
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC to initialize

    Serial.println("\n========================================");
    Serial.println("       OmniaPi Node v0.1.0");
    Serial.println("========================================\n");

    // Setup relay pins
    for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], RELAY_OFF);  // Start with relays OFF
    }

    // Setup button pin
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);

    // Setup LED pin (if available)
    #ifdef STATUS_LED_PIN
    pinMode(STATUS_LED_PIN, OUTPUT);
    #endif

    // Load configuration
    loadConfiguration();

    // Setup ESP-NOW
    if (!setupEspNow()) {
        Serial.println("[MAIN] ESP-NOW setup failed!");
        currentLedPattern = LED_PATTERN_ERROR;
    }

    // Restore relay states from NVS
    restoreRelayStates();

    Serial.println("\n[MAIN] Node ready!");
    Serial.println("========================================\n");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
    // Handle physical button
    handleButton();

    // Update LED pattern
    updateLed();

    // Periodic heartbeat to gateway
    if (registered && (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS)) {
        sendStateToGateway();
        lastHeartbeat = millis();
    }

    delay(10);  // Small delay to prevent watchdog issues
}
