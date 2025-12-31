/**
 * OmniaPi Gateway - ESP-NOW + WiFi + OTA + Relay Control
 * BUILD: GW-RELAY-001
 *
 * Features:
 * - ESP-NOW bidirectional communication
 * - WiFi connection to router
 * - Web UI for Gateway OTA updates
 * - Node OTA via ESP-NOW
 * - Node tracking and management
 * - Relay control via Web UI (ON/OFF per channel)
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ============== CONFIGURATION ==============
const char* WIFI_SSID = "Porte Di Durin";
const char* WIFI_PASSWORD = "Mellon!!!";
const char* FIRMWARE_VERSION = "1.3.0";
const char* AP_SSID = "OmniaPi-GW";
const char* AP_PASSWORD = "12345678";
const char* NODE_FW_PATH = "/node_fw.bin";

// ============== MESSAGE PROTOCOL ==============
#define MSG_HEARTBEAT    0x01
#define MSG_HEARTBEAT_ACK 0x02
#define MSG_OTA_BEGIN    0x10
#define MSG_OTA_READY    0x11
#define MSG_OTA_DATA     0x12
#define MSG_OTA_ACK      0x13
#define MSG_OTA_END      0x14
#define MSG_OTA_DONE     0x15
#define MSG_OTA_ERROR    0x1F

// Relay control messages
#define MSG_COMMAND      0x20
#define MSG_COMMAND_ACK  0x21
#define MSG_STATE        0x22

// Command actions
#define CMD_OFF          0x00
#define CMD_ON           0x01
#define CMD_TOGGLE       0x02

#define OTA_CHUNK_SIZE   200

// ============== NODE TRACKING ==============
#define MAX_NODES 20

struct NodeInfo {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t lastSeen;
    uint32_t messagesReceived;
    bool online;
    char version[16];
    uint8_t relayStates[2];  // State of relay 1 and 2 (0=OFF, 1=ON)
    uint8_t relayCount;      // Number of relays (default 2)
};

NodeInfo nodes[MAX_NODES];
int nodeCount = 0;

// ============== NODE OTA STATE ==============
File nodeOtaFile;
size_t nodeOtaSize = 0;
size_t nodeOtaSent = 0;
uint32_t nodeOtaChunkIndex = 0;
uint8_t nodeOtaTargetMac[6] = {0};
bool nodeOtaInProgress = false;
uint32_t nodeOtaLastAck = 0;
int nodeOtaRetries = 0;
String nodeOtaStatus = "";
bool nodeOtaFileReady = false;

// ============== COUNTERS ==============
volatile int receivedCount = 0;
volatile int sentCount = 0;

// ============== WEB SERVER ==============
AsyncWebServer server(80);

// ============== FUNCTION DECLARATIONS ==============
void setupWiFi();
void setupESPNow();
void setupWebServer();
int findOrAddNode(const uint8_t* mac, int8_t rssi);
String getNodesJson();
String macToString(const uint8_t* mac);
bool macFromString(const char* str, uint8_t* mac);
void sendNodeOtaChunk();
void startNodeOta(const uint8_t* mac);
void sendCommand(const uint8_t* mac, uint8_t channel, uint8_t action);

// ============== HTML PAGE ==============
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>OmniaPi Gateway</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #1a1a2e; color: #eee; min-height: 100vh; }
        .container { max-width: 900px; margin: 0 auto; padding: 20px; }
        h1 { color: #00d4ff; margin-bottom: 10px; }
        .subtitle { color: #888; margin-bottom: 30px; }
        .card { background: #16213e; border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid #0f3460; }
        .card h2 { color: #00d4ff; font-size: 1.2em; margin-bottom: 15px; }
        .status-row { display: flex; justify-content: space-between; padding: 10px 0; border-bottom: 1px solid #0f3460; }
        .status-row:last-child { border-bottom: none; }
        .status-label { color: #888; }
        .status-value { color: #00d4ff; font-weight: 500; }
        .status-ok { color: #00ff88; }
        .status-error { color: #ff4444; }
        .node-card { background: #0f3460; border-radius: 8px; padding: 15px; margin-bottom: 10px; }
        .node-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .node-mac { font-family: monospace; color: #00d4ff; font-size: 0.95em; }
        .node-status { padding: 4px 12px; border-radius: 20px; font-size: 0.85em; }
        .node-online { background: #00ff8820; color: #00ff88; }
        .node-offline { background: #ff444420; color: #ff4444; }
        .node-stats { display: flex; gap: 15px; font-size: 0.85em; color: #888; flex-wrap: wrap; }
        .node-actions { margin-top: 10px; display: flex; gap: 10px; align-items: center; }
        .btn { background: #00d4ff; color: #1a1a2e; border: none; padding: 8px 16px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 0.85em; }
        .btn:hover { background: #00a8cc; }
        .btn:disabled { background: #444; cursor: not-allowed; }
        .btn-sm { padding: 6px 12px; font-size: 0.8em; }
        .btn-orange { background: #ff9800; }
        .btn-orange:hover { background: #f57c00; }
        .btn-green { background: #00c853; }
        .btn-green:hover { background: #00a844; }
        .btn-red { background: #ff4444; color: #fff; }
        .btn-red:hover { background: #cc3333; }
        .relay-controls { display: flex; gap: 8px; margin-top: 10px; flex-wrap: wrap; }
        .relay-box { background: #1a1a2e; border-radius: 6px; padding: 10px; display: flex; align-items: center; gap: 10px; }
        .relay-label { color: #888; font-size: 0.85em; min-width: 50px; }
        .relay-state { padding: 4px 10px; border-radius: 4px; font-size: 0.8em; font-weight: 600; }
        .relay-on { background: #00c85330; color: #00c853; }
        .relay-off { background: #ff444430; color: #ff4444; }
        .upload-area { border: 2px dashed #0f3460; border-radius: 8px; padding: 30px; text-align: center; transition: all 0.3s; cursor: pointer; }
        .upload-area:hover { border-color: #00d4ff; background: #0f346020; }
        input[type="file"] { display: none; }
        .progress { height: 6px; background: #0f3460; border-radius: 3px; overflow: hidden; margin-top: 10px; }
        .progress-bar { height: 100%; background: linear-gradient(90deg, #00d4ff, #00ff88); transition: width 0.3s; }
        .message { padding: 12px; border-radius: 6px; margin-top: 10px; font-size: 0.9em; }
        .message.success { background: #00ff8820; color: #00ff88; }
        .message.error { background: #ff444420; color: #ff4444; }
        .message.info { background: #00d4ff20; color: #00d4ff; }
        .hidden { display: none; }
        .refresh-btn { background: transparent; border: 1px solid #00d4ff; color: #00d4ff; }
        #nodeOtaProgress { margin-top: 15px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>OmniaPi Gateway</h1>
        <p class="subtitle">Home Automation Control Center - v%VERSION%</p>

        <div class="card">
            <h2>Gateway Status</h2>
            <div class="status-row">
                <span class="status-label">Firmware</span>
                <span class="status-value">%VERSION%</span>
            </div>
            <div class="status-row">
                <span class="status-label">WiFi</span>
                <span class="status-value status-ok" id="wifiStatus">Connected</span>
            </div>
            <div class="status-row">
                <span class="status-label">IP Address</span>
                <span class="status-value">%IP%</span>
            </div>
            <div class="status-row">
                <span class="status-label">Channel</span>
                <span class="status-value">%CHANNEL%</span>
            </div>
            <div class="status-row">
                <span class="status-label">Messages RX/TX</span>
                <span class="status-value"><span id="rx">%RECEIVED%</span> / <span id="tx">%SENT%</span></span>
            </div>
        </div>

        <div class="card">
            <h2>Connected Nodes <button class="btn btn-sm refresh-btn" onclick="refreshNodes()">Refresh</button></h2>
            <div id="nodesList"></div>

            <div id="nodeOtaSection" class="hidden" style="margin-top: 20px; padding-top: 20px; border-top: 1px solid #0f3460;">
                <h3 style="color: #ff9800; margin-bottom: 15px;">Update Node: <span id="nodeOtaTarget"></span></h3>
                <div class="upload-area" onclick="document.getElementById('nodeFile').click()">
                    <p>Click to select node firmware (.bin)</p>
                </div>
                <input type="file" id="nodeFile" accept=".bin" onchange="handleNodeFile(this)">
                <div id="nodeFileInfo" class="hidden" style="margin-top: 10px;">
                    <p>File: <span id="nodeFileName"></span> (<span id="nodeFileSize"></span>)</p>
                    <button class="btn btn-orange" onclick="uploadNodeFirmware()">Send to Node</button>
                </div>
                <div id="nodeOtaProgress" class="hidden">
                    <div class="progress"><div class="progress-bar" id="nodeProgressBar" style="width: 0%"></div></div>
                    <p id="nodeOtaStatusText" style="margin-top: 8px; font-size: 0.9em; color: #888;"></p>
                </div>
                <div id="nodeOtaMessage"></div>
            </div>
        </div>

        <div class="card">
            <h2>Gateway Firmware Update</h2>
            <div class="upload-area" onclick="document.getElementById('gwFile').click()">
                <p>Click to select gateway firmware (.bin)</p>
            </div>
            <input type="file" id="gwFile" accept=".bin" onchange="handleGwFile(this)">
            <div id="gwFileInfo" class="hidden" style="margin-top: 10px;">
                <p>File: <span id="gwFileName"></span> (<span id="gwFileSize"></span>)</p>
                <button class="btn" onclick="uploadGwFirmware()">Upload & Restart</button>
            </div>
            <div id="gwProgress" class="hidden">
                <div class="progress"><div class="progress-bar" id="gwProgressBar"></div></div>
            </div>
            <div id="gwMessage"></div>
        </div>
    </div>

    <script>
        let selectedNodeMac = null;
        let nodeFile = null;
        let gwFile = null;

        function sendCmd(mac, channel, action) {
            const formData = new FormData();
            formData.append('mac', mac);
            formData.append('channel', channel);
            formData.append('action', action);
            fetch('/api/command', { method: 'POST', body: formData })
                .then(r => r.json())
                .then(d => {
                    if (d.success) {
                        setTimeout(refreshNodes, 300);
                    } else {
                        alert('Command failed: ' + (d.error || 'Unknown error'));
                    }
                })
                .catch(e => alert('Error: ' + e));
        }

        function refreshNodes() {
            fetch('/api/nodes').then(r => r.json()).then(data => {
                const list = document.getElementById('nodesList');
                if (!data.nodes || data.nodes.length === 0) {
                    list.innerHTML = '<p style="color:#666;text-align:center;padding:20px;">No nodes detected</p>';
                    return;
                }
                list.innerHTML = data.nodes.map(n => `
                    <div class="node-card">
                        <div class="node-header">
                            <span class="node-mac">${n.mac}</span>
                            <span class="node-status ${n.online ? 'node-online' : 'node-offline'}">${n.online ? 'Online' : 'Offline'}</span>
                        </div>
                        <div class="node-stats">
                            <span>RSSI: ${n.rssi} dBm</span>
                            <span>Msgs: ${n.messages}</span>
                            <span>Ver: ${n.version || '?'}</span>
                            <span>${n.lastSeen}</span>
                        </div>
                        <div class="relay-controls">
                            ${(n.relays || [0,0]).map((state, idx) => `
                                <div class="relay-box">
                                    <span class="relay-label">Relay ${idx+1}</span>
                                    <span class="relay-state ${state ? 'relay-on' : 'relay-off'}">${state ? 'ON' : 'OFF'}</span>
                                    <button class="btn btn-sm btn-green" onclick="sendCmd('${n.mac}',${idx+1},'on')" ${!n.online ? 'disabled' : ''}>ON</button>
                                    <button class="btn btn-sm btn-red" onclick="sendCmd('${n.mac}',${idx+1},'off')" ${!n.online ? 'disabled' : ''}>OFF</button>
                                </div>
                            `).join('')}
                        </div>
                        <div class="node-actions">
                            <button class="btn btn-sm btn-orange" onclick="selectNodeForOta('${n.mac}')" ${!n.online ? 'disabled' : ''}>
                                Update Firmware
                            </button>
                        </div>
                    </div>
                `).join('');
            });
        }

        function selectNodeForOta(mac) {
            selectedNodeMac = mac;
            document.getElementById('nodeOtaTarget').textContent = mac;
            document.getElementById('nodeOtaSection').classList.remove('hidden');
            document.getElementById('nodeFileInfo').classList.add('hidden');
            document.getElementById('nodeOtaProgress').classList.add('hidden');
            document.getElementById('nodeOtaMessage').innerHTML = '';
            document.getElementById('nodeFile').value = '';
            nodeFile = null;
        }

        function handleNodeFile(input) {
            if (input.files[0] && input.files[0].name.endsWith('.bin')) {
                nodeFile = input.files[0];
                document.getElementById('nodeFileName').textContent = nodeFile.name;
                document.getElementById('nodeFileSize').textContent = (nodeFile.size/1024).toFixed(1) + ' KB';
                document.getElementById('nodeFileInfo').classList.remove('hidden');
            }
        }

        function uploadNodeFirmware() {
            if (!nodeFile || !selectedNodeMac) return;

            const formData = new FormData();
            formData.append('firmware', nodeFile);
            formData.append('mac', selectedNodeMac);

            document.getElementById('nodeOtaProgress').classList.remove('hidden');
            document.getElementById('nodeOtaStatusText').textContent = 'Uploading to gateway...';

            fetch('/api/node-ota', { method: 'POST', body: formData })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        document.getElementById('nodeOtaStatusText').textContent = 'Sending to node via ESP-NOW...';
                        pollNodeOtaStatus();
                    } else {
                        showNodeOtaMessage(data.error || 'Upload failed', 'error');
                    }
                })
                .catch(e => showNodeOtaMessage('Error: ' + e, 'error'));
        }

        function pollNodeOtaStatus() {
            fetch('/api/node-ota-status').then(r => r.json()).then(data => {
                document.getElementById('nodeProgressBar').style.width = data.progress + '%';
                document.getElementById('nodeOtaStatusText').textContent = data.status;

                if (data.inProgress) {
                    setTimeout(pollNodeOtaStatus, 500);
                } else if (data.success) {
                    showNodeOtaMessage('Node updated successfully!', 'success');
                    setTimeout(refreshNodes, 3000);
                } else if (data.error) {
                    showNodeOtaMessage(data.status, 'error');
                }
            });
        }

        function showNodeOtaMessage(text, type) {
            document.getElementById('nodeOtaMessage').innerHTML = `<div class="message ${type}">${text}</div>`;
        }

        function handleGwFile(input) {
            if (input.files[0] && input.files[0].name.endsWith('.bin')) {
                gwFile = input.files[0];
                document.getElementById('gwFileName').textContent = gwFile.name;
                document.getElementById('gwFileSize').textContent = (gwFile.size/1024).toFixed(1) + ' KB';
                document.getElementById('gwFileInfo').classList.remove('hidden');
            }
        }

        function uploadGwFirmware() {
            if (!gwFile) return;
            const formData = new FormData();
            formData.append('firmware', gwFile);

            document.getElementById('gwProgress').classList.remove('hidden');
            const xhr = new XMLHttpRequest();
            xhr.upload.onprogress = e => {
                if (e.lengthComputable) {
                    document.getElementById('gwProgressBar').style.width = (e.loaded/e.total*100) + '%';
                }
            };
            xhr.onload = () => {
                if (xhr.status === 200) {
                    document.getElementById('gwMessage').innerHTML = '<div class="message success">Update OK! Rebooting...</div>';
                    setTimeout(() => location.reload(), 5000);
                } else {
                    document.getElementById('gwMessage').innerHTML = '<div class="message error">Update failed</div>';
                }
            };
            xhr.open('POST', '/update');
            xhr.send(formData);
        }

        function refreshStatus() {
            fetch('/api/status').then(r => r.json()).then(d => {
                document.getElementById('rx').textContent = d.received;
                document.getElementById('tx').textContent = d.sent;
            });
        }

        setInterval(refreshStatus, 5000);
        setInterval(refreshNodes, 10000);
        refreshNodes();
    </script>
</body>
</html>
)rawliteral";

// ============== ESP-NOW CALLBACKS ==============
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < 1) return;
    receivedCount++;

    int8_t rssi = info->rx_ctrl->rssi;
    uint8_t msgType = data[0];

    // Track node
    int nodeIdx = findOrAddNode(info->src_addr, rssi);

    // Handle OTA messages from node
    if (nodeOtaInProgress && memcmp(info->src_addr, nodeOtaTargetMac, 6) == 0) {
        switch (msgType) {
            case MSG_OTA_READY:
                Serial.println("[NODE-OTA] Node ready, sending chunks...");
                nodeOtaStatus = "Sending firmware...";
                nodeOtaChunkIndex = 0;
                nodeOtaSent = 0;
                nodeOtaLastAck = millis();
                sendNodeOtaChunk();
                break;

            case MSG_OTA_ACK:
                if (len >= 5) {
                    uint32_t ackIdx = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
                    nodeOtaChunkIndex = ackIdx;
                    nodeOtaLastAck = millis();
                    nodeOtaRetries = 0;
                    sendNodeOtaChunk();
                }
                break;

            case MSG_OTA_DONE:
                Serial.println("[NODE-OTA] Node update SUCCESS!");
                nodeOtaStatus = "Update successful!";
                nodeOtaInProgress = false;
                if (nodeOtaFile) { nodeOtaFile.close(); }
                break;

            case MSG_OTA_ERROR:
                Serial.println("[NODE-OTA] Node reported ERROR!");
                nodeOtaStatus = "Node reported error";
                nodeOtaInProgress = false;
                if (nodeOtaFile) { nodeOtaFile.close(); }
                break;
        }
    }

    // Handle heartbeat ACK (get node version)
    if (msgType == MSG_HEARTBEAT_ACK && nodeIdx >= 0 && len > 2) {
        // Extract version from response
        char ver[9] = {0};
        size_t copyLen = min((size_t)(len - 2), (size_t)8);
        memcpy(ver, data + 2, copyLen);
        // Sanitize: remove non-printable characters (prevent JSON errors)
        for (int j = 0; j < 8; j++) {
            if (ver[j] != '\0' && (ver[j] < 32 || ver[j] > 126)) {
                ver[j] = '\0';  // Terminate at first non-printable
                break;
            }
        }
        strncpy(nodes[nodeIdx].version, ver, sizeof(nodes[nodeIdx].version) - 1);
    }

    // Handle command ACK and state updates
    if ((msgType == MSG_COMMAND_ACK || msgType == MSG_STATE) && nodeIdx >= 0 && len >= 3) {
        // Format: [msgType][channel][state]
        uint8_t channel = data[1];
        uint8_t state = data[2];
        if (channel >= 1 && channel <= 2) {
            nodes[nodeIdx].relayStates[channel - 1] = state;
            Serial.printf("[RELAY] Node %s Ch%d = %s\n",
                         macToString(nodes[nodeIdx].mac).c_str(),
                         channel, state ? "ON" : "OFF");
        }
    }
}

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    sentCount++;
}

// ============== NODE OTA FUNCTIONS ==============
void startNodeOta(const uint8_t* mac) {
    if (!nodeOtaFileReady || nodeOtaSize == 0) {
        nodeOtaStatus = "No firmware loaded";
        return;
    }

    // Open file for reading
    nodeOtaFile = LittleFS.open(NODE_FW_PATH, "r");
    if (!nodeOtaFile) {
        nodeOtaStatus = "Failed to open firmware file";
        return;
    }

    memcpy(nodeOtaTargetMac, mac, 6);
    nodeOtaInProgress = true;
    nodeOtaChunkIndex = 0;
    nodeOtaSent = 0;
    nodeOtaRetries = 0;
    nodeOtaLastAck = millis();
    nodeOtaStatus = "Starting OTA...";

    // Add node as peer if not exists
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.ifidx = WIFI_IF_AP;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);  // Ignore error if already exists

    // Send OTA BEGIN
    uint8_t beginMsg[5];
    beginMsg[0] = MSG_OTA_BEGIN;
    beginMsg[1] = nodeOtaSize & 0xFF;
    beginMsg[2] = (nodeOtaSize >> 8) & 0xFF;
    beginMsg[3] = (nodeOtaSize >> 16) & 0xFF;
    beginMsg[4] = (nodeOtaSize >> 24) & 0xFF;

    Serial.printf("[NODE-OTA] Sending BEGIN to %s, size=%u\n", macToString(mac).c_str(), nodeOtaSize);
    esp_now_send(mac, beginMsg, 5);
}

void sendNodeOtaChunk() {
    if (!nodeOtaInProgress || !nodeOtaFile) return;

    size_t offset = nodeOtaChunkIndex * OTA_CHUNK_SIZE;
    if (offset >= nodeOtaSize) {
        // All chunks sent, send END
        Serial.println("[NODE-OTA] All chunks sent, sending END");
        nodeOtaStatus = "Finalizing...";
        uint8_t endMsg[1] = { MSG_OTA_END };
        esp_now_send(nodeOtaTargetMac, endMsg, 1);
        return;
    }

    size_t chunkLen = min((size_t)OTA_CHUNK_SIZE, nodeOtaSize - offset);

    // Build data packet: [MSG_TYPE][CHUNK_IDX x4][DATA...]
    uint8_t packet[5 + OTA_CHUNK_SIZE];
    packet[0] = MSG_OTA_DATA;
    packet[1] = nodeOtaChunkIndex & 0xFF;
    packet[2] = (nodeOtaChunkIndex >> 8) & 0xFF;
    packet[3] = (nodeOtaChunkIndex >> 16) & 0xFF;
    packet[4] = (nodeOtaChunkIndex >> 24) & 0xFF;

    // Read chunk from file
    nodeOtaFile.seek(offset);
    nodeOtaFile.read(packet + 5, chunkLen);

    esp_now_send(nodeOtaTargetMac, packet, 5 + chunkLen);
    nodeOtaSent = offset + chunkLen;

    int progress = (nodeOtaSent * 100) / nodeOtaSize;
    nodeOtaStatus = "Sending: " + String(progress) + "%";

    if (nodeOtaChunkIndex % 100 == 0) {
        Serial.printf("[NODE-OTA] Chunk %u, %u/%u bytes (%d%%)\n",
                      nodeOtaChunkIndex, nodeOtaSent, nodeOtaSize, progress);
    }
}

// ============== RELAY COMMAND ==============
void sendCommand(const uint8_t* mac, uint8_t channel, uint8_t action) {
    // Add node as peer if not exists
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.ifidx = WIFI_IF_AP;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);  // Ignore error if already exists

    // Send command: [MSG_COMMAND][channel][action]
    uint8_t msg[3] = { MSG_COMMAND, channel, action };
    esp_now_send(mac, msg, 3);
    Serial.printf("[CMD] Sent to %s: Ch%d %s\n",
                  macToString(mac).c_str(), channel,
                  action == CMD_ON ? "ON" : (action == CMD_OFF ? "OFF" : "TOGGLE"));
}

// ============== NODE MANAGEMENT ==============
int findOrAddNode(const uint8_t* mac, int8_t rssi) {
    for (int i = 0; i < nodeCount; i++) {
        if (memcmp(nodes[i].mac, mac, 6) == 0) {
            nodes[i].rssi = rssi;
            nodes[i].lastSeen = millis();
            nodes[i].messagesReceived++;
            nodes[i].online = true;
            return i;
        }
    }

    if (nodeCount < MAX_NODES) {
        memcpy(nodes[nodeCount].mac, mac, 6);
        nodes[nodeCount].rssi = rssi;
        nodes[nodeCount].lastSeen = millis();
        nodes[nodeCount].messagesReceived = 1;
        nodes[nodeCount].online = true;
        nodes[nodeCount].version[0] = '\0';
        nodes[nodeCount].relayStates[0] = 0;
        nodes[nodeCount].relayStates[1] = 0;
        nodes[nodeCount].relayCount = 2;
        return nodeCount++;
    }
    return -1;
}

String macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool macFromString(const char* str, uint8_t* mac) {
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

String getNodesJson() {
    JsonDocument doc;
    JsonArray arr = doc["nodes"].to<JsonArray>();
    uint32_t now = millis();

    for (int i = 0; i < nodeCount; i++) {
        if (now - nodes[i].lastSeen > 10000) nodes[i].online = false;  // 10s timeout

        JsonObject n = arr.add<JsonObject>();
        n["mac"] = macToString(nodes[i].mac);
        n["rssi"] = nodes[i].rssi;
        n["messages"] = nodes[i].messagesReceived;
        n["online"] = nodes[i].online;
        n["version"] = nodes[i].version;

        // Relay states
        JsonArray relays = n["relays"].to<JsonArray>();
        for (int r = 0; r < nodes[i].relayCount; r++) {
            relays.add(nodes[i].relayStates[r]);
        }

        uint32_t ago = (now - nodes[i].lastSeen) / 1000;
        n["lastSeen"] = ago < 60 ? String(ago) + "s ago" : String(ago/60) + "m ago";
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ============== WEB SERVER ==============
void setupWebServer() {
    // Main page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        String html = String(INDEX_HTML);
        html.replace("%VERSION%", FIRMWARE_VERSION);
        html.replace("%IP%", WiFi.localIP().toString());
        html.replace("%CHANNEL%", String(WiFi.channel()));
        html.replace("%RECEIVED%", String(receivedCount));
        html.replace("%SENT%", String(sentCount));
        req->send(200, "text/html", html);
    });

    // API: Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["received"] = receivedCount;
        doc["sent"] = sentCount;
        doc["wifiConnected"] = WiFi.isConnected();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // API: Nodes
    server.on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", getNodesJson());
    });

    // API: Send command to node
    server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        bool success = false;

        if (req->hasParam("mac", true) && req->hasParam("channel", true) && req->hasParam("action", true)) {
            String macStr = req->getParam("mac", true)->value();
            int channel = req->getParam("channel", true)->value().toInt();
            String actionStr = req->getParam("action", true)->value();

            uint8_t mac[6];
            if (macFromString(macStr.c_str(), mac)) {
                uint8_t action = CMD_TOGGLE;
                if (actionStr == "on") action = CMD_ON;
                else if (actionStr == "off") action = CMD_OFF;
                else if (actionStr == "toggle") action = CMD_TOGGLE;

                sendCommand(mac, channel, action);
                success = true;
            } else {
                doc["error"] = "Invalid MAC";
            }
        } else {
            doc["error"] = "Missing parameters (mac, channel, action)";
        }

        doc["success"] = success;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // API: Node OTA Status
    server.on("/api/node-ota-status", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["inProgress"] = nodeOtaInProgress;
        doc["progress"] = nodeOtaSize > 0 ? (nodeOtaSent * 100 / nodeOtaSize) : 0;
        doc["status"] = nodeOtaStatus;
        doc["success"] = !nodeOtaInProgress && nodeOtaStatus.indexOf("success") >= 0;
        doc["error"] = !nodeOtaInProgress && nodeOtaStatus.indexOf("error") >= 0;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // API: Node OTA Upload
    server.on("/api/node-ota", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            JsonDocument doc;
            if (nodeOtaFileReady && nodeOtaSize > 0) {
                // Get MAC from params
                if (req->hasParam("mac", true)) {
                    String macStr = req->getParam("mac", true)->value();
                    uint8_t mac[6];
                    if (macFromString(macStr.c_str(), mac)) {
                        startNodeOta(mac);
                        doc["success"] = true;
                    } else {
                        doc["success"] = false;
                        doc["error"] = "Invalid MAC";
                    }
                } else {
                    doc["success"] = false;
                    doc["error"] = "No MAC provided";
                }
            } else {
                doc["success"] = false;
                doc["error"] = "No firmware uploaded";
            }
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        },
        [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            static File uploadFile;
            if (!index) {
                Serial.printf("[NODE-OTA] Receiving firmware: %s, size: %u\n", filename.c_str(), req->contentLength());
                nodeOtaFileReady = false;
                nodeOtaSize = 0;
                // Open file for writing
                uploadFile = LittleFS.open(NODE_FW_PATH, "w");
                if (!uploadFile) {
                    Serial.println("[NODE-OTA] Failed to open file for writing!");
                    return;
                }
            }
            if (uploadFile) {
                uploadFile.write(data, len);
            }
            if (final) {
                if (uploadFile) {
                    uploadFile.close();
                    nodeOtaSize = index + len;
                    nodeOtaFileReady = true;
                    Serial.printf("[NODE-OTA] Firmware saved to flash: %u bytes\n", nodeOtaSize);
                }
            }
        }
    );

    // Gateway OTA
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            bool ok = !Update.hasError();
            req->send(200, "text/plain", ok ? "OK" : "FAIL");
            if (ok) { delay(1000); ESP.restart(); }
        },
        [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[GW-OTA] Start: %s\n", filename.c_str());
                Update.begin(UPDATE_SIZE_UNKNOWN);
            }
            Update.write(data, len);
            if (final) {
                Update.end(true);
                Serial.printf("[GW-OTA] Done: %u bytes\n", index + len);
            }
        }
    );

    server.begin();
    Serial.println("[WEB] Server started");
}

// ============== SETUP ==============
void setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int timeout = 30;
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
        delay(500); Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WIFI] Connected! IP: %s, Channel: %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
    } else {
        Serial.println("\n[WIFI] FAILED!");
        return;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASSWORD, WiFi.channel(), 1);
}

void setupESPNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED!");
        return;
    }
    Serial.println("[ESP-NOW] Initialized");

    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    // Add broadcast peer
    esp_now_peer_info_t peerInfo = {};
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    memcpy(peerInfo.peer_addr, broadcast, 6);
    peerInfo.channel = 0;
    peerInfo.ifidx = WIFI_IF_AP;
    esp_now_add_peer(&peerInfo);
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n========================================");
    Serial.printf("  OmniaPi Gateway v%s\n", FIRMWARE_VERSION);
    Serial.println("  ESP-NOW + WiFi + OTA + Node OTA");
    Serial.println("========================================\n");

    // Initialize LittleFS for Node OTA storage
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed!");
    } else {
        Serial.println("[FS] LittleFS mounted");
    }

    setupWiFi();
    setupESPNow();
    setupWebServer();

    Serial.printf("\n[READY] http://%s\n\n", WiFi.localIP().toString().c_str());
}

// ============== LOOP ==============
void loop() {
    static uint32_t lastBroadcast = 0;
    static uint32_t lastStatus = 0;

    // Heartbeat broadcast every 3s (fast node discovery)
    if (millis() - lastBroadcast > 3000) {
        uint8_t msg[1] = { MSG_HEARTBEAT };
        uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        esp_now_send(broadcast, msg, 1);
        lastBroadcast = millis();
    }

    // Node OTA timeout
    if (nodeOtaInProgress && (millis() - nodeOtaLastAck > 5000)) {
        nodeOtaRetries++;
        if (nodeOtaRetries > 3) {
            Serial.println("[NODE-OTA] Timeout! Aborting.");
            nodeOtaStatus = "Timeout error";
            nodeOtaInProgress = false;
            if (nodeOtaFile) { nodeOtaFile.close(); }
        } else {
            Serial.printf("[NODE-OTA] Retry %d...\n", nodeOtaRetries);
            sendNodeOtaChunk();
            nodeOtaLastAck = millis();
        }
    }

    // Status print
    if (millis() - lastStatus > 30000) {
        Serial.printf("[STATUS] Nodes: %d | RX: %d | TX: %d | WiFi: %s\n",
                      nodeCount, receivedCount, sentCount,
                      WiFi.isConnected() ? "OK" : "DISC");
        lastStatus = millis();
    }

    delay(10);
}
