/**
 * OmniaPi Node - ESP-NOW + Relay Control
 * VERSION: 1.2.0 - Clean restart
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Update.h>

// ============== CONFIGURATION ==============
#define WIFI_CHANNEL 10  // Original channel
const char* FIRMWARE_VERSION = "1.5.0";

// Built-in LED (ESP32-C3 SuperMini)
#define LED_PIN 8
#define LED_ON  LOW
#define LED_OFF HIGH

// Relay pins
#define RELAY1_PIN 1
#define RELAY2_PIN 2
#define RELAY_COUNT 2
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ============== MESSAGE PROTOCOL ==============
#define MSG_HEARTBEAT     0x01
#define MSG_HEARTBEAT_ACK 0x02
#define MSG_OTA_BEGIN     0x10
#define MSG_OTA_READY     0x11
#define MSG_OTA_DATA      0x12
#define MSG_OTA_ACK       0x13
#define MSG_OTA_END       0x14
#define MSG_OTA_DONE      0x15
#define MSG_OTA_ERROR     0x1F
#define MSG_COMMAND       0x20
#define MSG_COMMAND_ACK   0x21

#define CMD_OFF    0x00
#define CMD_ON     0x01
#define CMD_TOGGLE 0x02

// ============== STATE ==============
uint8_t gatewayMac[6] = {0};
bool gatewayKnown = false;
uint8_t relayStates[RELAY_COUNT] = {0, 0};
const uint8_t relayPins[RELAY_COUNT] = {RELAY1_PIN, RELAY2_PIN};
bool otaInProgress = false;
uint32_t otaSize = 0;
uint32_t otaReceived = 0;
uint32_t otaChunkIndex = 0;
uint32_t otaLastChunkTime = 0;

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============== FUNCTIONS ==============
String macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void sendMessage(const uint8_t* mac, uint8_t msgType, const uint8_t* data, size_t len) {
    uint8_t buffer[250];
    buffer[0] = msgType;
    if (data && len > 0) {
        memcpy(buffer + 1, data, min(len, (size_t)249));
    }
    esp_now_send(mac, buffer, 1 + min(len, (size_t)249));
}

void setRelay(uint8_t channel, uint8_t state) {
    if (channel < 1 || channel > RELAY_COUNT) return;
    uint8_t idx = channel - 1;
    relayStates[idx] = state ? 1 : 0;
    digitalWrite(relayPins[idx], state ? RELAY_ON : RELAY_OFF);
}

void sendRelayState(const uint8_t* mac, uint8_t channel) {
    if (channel < 1 || channel > RELAY_COUNT) return;
    uint8_t response[2] = { channel, relayStates[channel - 1] };
    sendMessage(mac, MSG_COMMAND_ACK, response, 2);
}

void handleCommand(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 3) return;
    uint8_t channel = data[1];
    uint8_t action = data[2];

    if (channel < 1 || channel > RELAY_COUNT) return;

    uint8_t newState;
    switch (action) {
        case CMD_ON: newState = 1; break;
        case CMD_OFF: newState = 0; break;
        case CMD_TOGGLE: newState = relayStates[channel - 1] ? 0 : 1; break;
        default: return;
    }

    setRelay(channel, newState);
    sendRelayState(mac, channel);
}

void handleOtaBegin(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 5) {
        sendMessage(mac, MSG_OTA_ERROR, nullptr, 0);
        return;
    }
    otaSize = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    if (!Update.begin(otaSize)) {
        sendMessage(mac, MSG_OTA_ERROR, nullptr, 0);
        return;
    }
    otaInProgress = true;
    otaReceived = 0;
    otaChunkIndex = 0;
    otaLastChunkTime = millis();
    sendMessage(mac, MSG_OTA_READY, nullptr, 0);
}

void handleOtaData(const uint8_t* mac, const uint8_t* data, int len) {
    if (!otaInProgress || len < 5) return;

    uint32_t chunkIdx = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    size_t dataLen = len - 5;
    const uint8_t* chunkData = data + 5;

    if (chunkIdx != otaChunkIndex) {
        uint8_t nack[4] = {
            (uint8_t)(otaChunkIndex & 0xFF),
            (uint8_t)((otaChunkIndex >> 8) & 0xFF),
            (uint8_t)((otaChunkIndex >> 16) & 0xFF),
            (uint8_t)((otaChunkIndex >> 24) & 0xFF)
        };
        sendMessage(mac, MSG_OTA_ACK, nack, 4);
        return;
    }

    if (Update.write((uint8_t*)chunkData, dataLen) != dataLen) {
        otaInProgress = false;
        Update.abort();
        sendMessage(mac, MSG_OTA_ERROR, nullptr, 0);
        return;
    }

    otaReceived += dataLen;
    otaChunkIndex++;
    otaLastChunkTime = millis();

    uint8_t ack[4] = {
        (uint8_t)(otaChunkIndex & 0xFF),
        (uint8_t)((otaChunkIndex >> 8) & 0xFF),
        (uint8_t)((otaChunkIndex >> 16) & 0xFF),
        (uint8_t)((otaChunkIndex >> 24) & 0xFF)
    };
    sendMessage(mac, MSG_OTA_ACK, ack, 4);
}

void handleOtaEnd(const uint8_t* mac) {
    if (!otaInProgress) return;

    if (Update.end(true)) {
        sendMessage(mac, MSG_OTA_DONE, nullptr, 0);
        delay(500);
        ESP.restart();
    } else {
        sendMessage(mac, MSG_OTA_ERROR, nullptr, 0);
    }
    otaInProgress = false;
}

// ============== ESP-NOW CALLBACKS ==============
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < 1) return;

    // Register gateway on first contact
    if (!gatewayKnown) {
        memcpy(gatewayMac, info->src_addr, 6);
        gatewayKnown = true;

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, gatewayMac, 6);
        peerInfo.channel = 0;
        peerInfo.ifidx = WIFI_IF_STA;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    uint8_t msgType = data[0];

    switch (msgType) {
        case MSG_HEARTBEAT: {
            uint8_t response[10] = {0};
            response[0] = otaInProgress ? 1 : 0;
            size_t verLen = strlen(FIRMWARE_VERSION);
            memcpy(&response[1], FIRMWARE_VERSION, min(verLen, (size_t)8));
            sendMessage(info->src_addr, MSG_HEARTBEAT_ACK, response, 9);
            break;
        }
        case MSG_OTA_BEGIN:
            handleOtaBegin(info->src_addr, data, len);
            break;
        case MSG_OTA_DATA:
            handleOtaData(info->src_addr, data, len);
            break;
        case MSG_OTA_END:
            handleOtaEnd(info->src_addr);
            break;
        case MSG_COMMAND:
            handleCommand(info->src_addr, data, len);
            break;
    }
}

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    // Empty - just for callback registration
}

// ============== SETUP ==============
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_ON);

    // Skip Serial - it blocks boot without USB
    // Serial.begin(115200);
    // delay(2000);

    // Init relays
    for (int i = 0; i < RELAY_COUNT; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], RELAY_OFF);
    }

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Set channel
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // ESP-NOW
    if (esp_now_init() != ESP_OK) {
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = 0;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    digitalWrite(LED_PIN, LED_OFF);
}

// ============== LOOP ==============
void loop() {
    static uint32_t lastBlink = 0;
    static bool ledState = false;

    // LED blink pattern
    uint32_t now = millis();
    if (otaInProgress) {
        if (now - lastBlink > 100) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
            lastBlink = now;
        }
    } else if (gatewayKnown) {
        // Double blink every 2 sec
        uint32_t phase = now % 2000;
        if (phase < 100 || (phase > 200 && phase < 300)) {
            digitalWrite(LED_PIN, LED_ON);
        } else {
            digitalWrite(LED_PIN, LED_OFF);
        }
    } else {
        // Slow blink
        if (now - lastBlink > 1000) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
            lastBlink = now;
        }
    }

    // OTA timeout
    if (otaInProgress && (millis() - otaLastChunkTime > 30000)) {
        Update.abort();
        otaInProgress = false;
    }

    delay(10);
}
