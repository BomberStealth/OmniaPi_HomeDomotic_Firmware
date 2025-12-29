/**
 * OmniaPi Gateway Firmware
 *
 * Main entry point for the WT32-ETH01 gateway.
 * Handles Ethernet, ESP-NOW mesh coordination, and Web UI.
 *
 * @version 0.1.0
 * @date 2025-12-29
 */

#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Define as gateway before including shared headers
#define OMNIAPI_GATEWAY
#include "../../shared/protocol/messages.h"
#include "../../shared/config/hardware.h"

// ============================================
// GLOBAL VARIABLES
// ============================================

// Ethernet state
static bool ethConnected = false;

// Web server
AsyncWebServer server(80);

// Node registry
#define MAX_NODES 50
struct NodeInfo {
    bool active;
    uint8_t nodeId;
    uint8_t macAddress[6];
    OmniaPiDeviceType deviceType;
    uint8_t channelCount;
    uint8_t states[8];
    int8_t rssi;
    uint32_t lastSeen;
    char name[17];
};
NodeInfo nodes[MAX_NODES];
uint8_t nodeCount = 0;

// Message sequence counter
uint8_t messageSequence = 0;

// ============================================
// ETHERNET EVENT HANDLER
// ============================================

void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Starting...");
            ETH.setHostname("OmniaPi-Gateway");
            break;

        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Cable connected");
            break;

        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.println("[ETH] Connected!");
            Serial.print("[ETH] IP: ");
            Serial.println(ETH.localIP());
            Serial.print("[ETH] MAC: ");
            Serial.println(ETH.macAddress());
            Serial.print("[ETH] Speed: ");
            Serial.print(ETH.linkSpeed());
            Serial.println(" Mbps");
            ethConnected = true;
            break;

        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Cable disconnected");
            ethConnected = false;
            break;

        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("[ETH] Stopped");
            ethConnected = false;
            break;

        default:
            break;
    }
}

// ============================================
// ESP-NOW CALLBACKS
// ============================================

void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < sizeof(OmniaPiHeader)) {
        Serial.println("[ESP-NOW] Message too short");
        return;
    }

    const OmniaPiMessage *msg = (const OmniaPiMessage *)data;

    // Validate checksum
    if (!omniapi_validate(msg)) {
        Serial.println("[ESP-NOW] Invalid checksum");
        return;
    }

    // Log received message
    Serial.printf("[ESP-NOW] Received type=0x%02X from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  msg->header.type,
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5]);

    // Handle message based on type
    switch (msg->header.type) {
        case MSG_DISCOVERY_RESPONSE:
            // Node responding to discovery
            // TODO: Implement node registration
            Serial.println("[ESP-NOW] Discovery response received");
            break;

        case MSG_STATE:
            // Node reporting state
            // TODO: Update node state in registry
            Serial.println("[ESP-NOW] State update received");
            break;

        case MSG_PONG:
            // Heartbeat response
            // TODO: Update node lastSeen timestamp
            Serial.println("[ESP-NOW] Pong received");
            break;

        default:
            Serial.printf("[ESP-NOW] Unknown message type: 0x%02X\n", msg->header.type);
            break;
    }
}

void onEspNowSend(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESP-NOW] Send success");
    } else {
        Serial.println("[ESP-NOW] Send failed");
    }
}

// ============================================
// ESP-NOW SETUP
// ============================================

bool setupEspNow() {
    // Initialize WiFi in station mode (required for ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed!");
        return false;
    }

    // Register callbacks
    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_register_send_cb(onEspNowSend);

    Serial.println("[ESP-NOW] Initialized");
    Serial.print("[ESP-NOW] MAC: ");
    Serial.println(WiFi.macAddress());

    return true;
}

// ============================================
// WEB SERVER SETUP
// ============================================

void setupWebServer() {
    // Serve static files from SPIFFS
    if (SPIFFS.begin(true)) {
        server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
        Serial.println("[WEB] SPIFFS mounted");
    } else {
        Serial.println("[WEB] SPIFFS mount failed!");
    }

    // API: Get gateway status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["connected"] = ethConnected;
        doc["ip"] = ETH.localIP().toString();
        doc["mac"] = ETH.macAddress();
        doc["nodeCount"] = nodeCount;
        doc["uptime"] = millis() / 1000;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Get all nodes
    server.on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray nodesArray = doc.to<JsonArray>();

        for (int i = 0; i < MAX_NODES; i++) {
            if (nodes[i].active) {
                JsonObject node = nodesArray.add<JsonObject>();
                node["id"] = nodes[i].nodeId;
                node["name"] = nodes[i].name;
                node["type"] = nodes[i].deviceType;
                node["channels"] = nodes[i].channelCount;
                node["rssi"] = nodes[i].rssi;
                node["lastSeen"] = nodes[i].lastSeen;

                JsonArray states = node["states"].to<JsonArray>();
                for (int j = 0; j < nodes[i].channelCount; j++) {
                    states.add(nodes[i].states[j]);
                }

                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         nodes[i].macAddress[0], nodes[i].macAddress[1],
                         nodes[i].macAddress[2], nodes[i].macAddress[3],
                         nodes[i].macAddress[4], nodes[i].macAddress[5]);
                node["mac"] = macStr;
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Send command to node
    server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request) {},
        NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        uint8_t nodeId = doc["nodeId"] | 0;
        uint8_t channel = doc["channel"] | 1;
        uint8_t action = doc["action"] | 0;

        // TODO: Send ESP-NOW command to node

        Serial.printf("[API] Command: node=%d, channel=%d, action=%d\n", nodeId, channel, action);

        request->send(200, "application/json", "{\"success\":true}");
    });

    // API: Trigger discovery
    server.on("/api/discover", HTTP_POST, [](AsyncWebServerRequest *request) {
        // TODO: Broadcast discovery message
        Serial.println("[API] Discovery triggered");
        request->send(200, "application/json", "{\"success\":true}");
    });

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    server.begin();
    Serial.println("[WEB] Server started on port 80");
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("       OmniaPi Gateway v0.1.0");
    Serial.println("========================================\n");

    // Initialize node registry
    memset(nodes, 0, sizeof(nodes));

    // Setup Ethernet
    Network.onEvent(onEthEvent);
    ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, -1, ETH_CLK_MODE);

    // Wait for Ethernet connection
    Serial.println("[MAIN] Waiting for Ethernet...");
    uint32_t startTime = millis();
    while (!ethConnected && (millis() - startTime < 10000)) {
        delay(100);
    }

    if (!ethConnected) {
        Serial.println("[MAIN] Ethernet not connected, continuing anyway...");
    }

    // Setup ESP-NOW
    if (!setupEspNow()) {
        Serial.println("[MAIN] ESP-NOW setup failed!");
    }

    // Setup Web Server
    setupWebServer();

    Serial.println("\n[MAIN] Gateway ready!");
    Serial.println("========================================\n");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
    // TODO: Implement heartbeat to nodes
    // TODO: Implement periodic discovery

    delay(100);
}
