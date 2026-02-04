/**
 * OmniaPi Gateway Mesh - Web UI Implementation
 * Embedded HTML/CSS/JS
 */

#include "web_ui.h"

// ============================================================================
// HTML Page
// ============================================================================
static const char HTML_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OmniaPi Gateway</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <div class="header-left">
            <h1>OmniaPi Gateway</h1>
            <span id="status-indicator" class="status-dot offline"></span>
        </div>
        <div class="header-right">
            <button onclick="rebootGateway()" class="btn btn-warning">Reboot</button>
            <button onclick="toggleSettings()" class="btn btn-icon">⚙️</button>
        </div>
    </header>

    <main>
        <!-- Status Cards -->
        <section class="cards">
            <div class="card">
                <div class="card-icon" id="card-online">🟢</div>
                <div class="card-value" id="uptime">--:--:--</div>
                <div class="card-label">Uptime</div>
            </div>
            <div class="card">
                <div class="card-icon" id="route-icon">🔌</div>
                <div class="card-value" id="route-status">--</div>
                <div class="card-label">Route</div>
            </div>
            <div class="card">
                <div class="card-icon">📶</div>
                <div class="card-value" id="wifi-rssi">-- dBm</div>
                <div class="card-label">WiFi RSSI</div>
            </div>
            <div class="card">
                <div class="card-icon" id="mqtt-icon">🔗</div>
                <div class="card-value" id="mqtt-status">--</div>
                <div class="card-label">MQTT</div>
            </div>
            <div class="card">
                <div class="card-icon">🌐</div>
                <div class="card-value" id="mesh-info">Ch --</div>
                <div class="card-label">Mesh</div>
            </div>
            <div class="card">
                <div class="card-icon">📦</div>
                <div class="card-value" id="node-count">-- nodes</div>
                <div class="card-label">Nodes</div>
            </div>
            <div class="card">
                <div class="card-icon">💾</div>
                <div class="card-value" id="heap-free">-- KB</div>
                <div class="card-label">Free Heap</div>
            </div>
        </section>

        <!-- Nodes Section -->
        <section class="panel">
            <div class="panel-header">
                <h2>Nodes</h2>
                <div class="panel-actions">
                    <button onclick="startScan()" class="btn btn-primary" id="scan-btn">🔍 Scan</button>
                </div>
            </div>
            <div class="panel-content">
                <table id="nodes-table">
                    <thead>
                        <tr>
                            <th>Name</th>
                            <th>Type</th>
                            <th>FW</th>
                            <th>Status</th>
                            <th>RSSI</th>
                            <th>Last Seen</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody id="nodes-body">
                        <tr><td colspan="7" class="loading">Loading...</td></tr>
                    </tbody>
                </table>
            </div>
        </section>

        <!-- Scan Results (hidden by default) -->
        <section class="panel" id="scan-panel" style="display:none;">
            <div class="panel-header">
                <h2>Discovered Nodes <span id="scan-status" class="scan-status"></span></h2>
                <div class="panel-actions">
                    <button onclick="stopScan()" class="btn btn-warning btn-small" id="stop-scan-btn">Stop Scan</button>
                    <button onclick="hideScanPanel()" class="btn btn-small">✕</button>
                </div>
            </div>
            <div class="panel-content">
                <table id="scan-table">
                    <thead>
                        <tr>
                            <th>MAC</th>
                            <th>Type</th>
                            <th>Firmware</th>
                            <th>RSSI</th>
                            <th>Action</th>
                        </tr>
                    </thead>
                    <tbody id="scan-body"></tbody>
                </table>
            </div>
        </section>

        <!-- Logs Section -->
        <section class="panel">
            <div class="panel-header">
                <h2>Logs</h2>
                <button onclick="clearLogs()" class="btn btn-small">Clear</button>
            </div>
            <div class="panel-content logs-container">
                <div id="logs"></div>
            </div>
        </section>

        <!-- OTA Section -->
        <section class="panel">
            <div class="panel-header">
                <h2>Firmware Update</h2>
            </div>
            <div class="panel-content ota-section">
                <div class="ota-row">
                    <span>Current Version: <strong id="gw-version">v1.0.0</strong></span>
                </div>

                <!-- Gateway OTA Upload -->
                <div class="ota-upload-box" id="ota-upload-box">
                    <input type="file" id="firmware-file" accept=".bin" style="display:none;" onchange="handleFirmwareSelect(event)">
                    <div class="ota-dropzone" id="ota-dropzone" onclick="document.getElementById('firmware-file').click()">
                        <div class="ota-dropzone-icon">📦</div>
                        <div class="ota-dropzone-text">Click or drag firmware.bin here</div>
                        <div class="ota-dropzone-hint">Max 2MB</div>
                    </div>
                </div>

                <!-- Upload Progress -->
                <div class="ota-upload-progress" id="ota-upload-progress" style="display:none;">
                    <div class="ota-upload-info">
                        <span id="ota-upload-filename">firmware.bin</span>
                        <span id="ota-upload-percent">0%</span>
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill" id="ota-upload-fill"></div>
                    </div>
                    <div class="ota-upload-status" id="ota-upload-status">Uploading...</div>
                </div>

                <!-- Node OTA Progress (for node updates) -->
                <div class="ota-progress" id="ota-progress" style="display:none;">
                    <div class="ota-node-label">Node Update:</div>
                    <div class="progress-bar">
                        <div class="progress-fill" id="ota-fill"></div>
                    </div>
                    <span id="ota-text">0% (0/0 nodes)</span>
                </div>
            </div>
        </section>
    </main>

    <!-- Settings Modal -->
    <div id="settings-modal" class="modal" style="display:none;">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Settings</h2>
                <button onclick="toggleSettings()" class="btn btn-small">✕</button>
            </div>
            <div class="modal-body">
                <h3>Network Info</h3>
                <p>IP: <span id="gateway-ip">--</span></p>
                <p>MAC: <span id="gateway-mac">--</span></p>
                <p>WiFi SSID: <span id="wifi-ssid">--</span></p>
                <hr>
                <h3>WiFi Configuration</h3>
                <div class="form-group">
                    <label for="wifi-cfg-ssid">SSID</label>
                    <div style="display:flex;gap:8px;">
                        <select id="wifi-cfg-ssid" style="flex:1;padding:0.5rem;border:1px solid var(--border);border-radius:6px;background:var(--bg-card);color:var(--text-primary);font-size:0.9rem;">
                            <option value="">-- Click Scan --</option>
                        </select>
                        <button onclick="scanWifi()" class="btn btn-primary" id="wifi-scan-btn" style="white-space:nowrap;">Scan</button>
                    </div>
                </div>
                <div class="form-group">
                    <label for="wifi-cfg-pass">Password</label>
                    <input type="password" id="wifi-cfg-pass" placeholder="Password" maxlength="64">
                </div>
                <button onclick="saveWifiConfig()" class="btn btn-primary" id="wifi-save-btn">Save WiFi</button>
                <span id="wifi-save-status" style="margin-left:10px;"></span>
                <hr>
                <h3>MQTT Configuration</h3>
                <div class="form-group">
                    <label for="mqtt-cfg-uri">Broker URI</label>
                    <input type="text" id="mqtt-cfg-uri" placeholder="mqtt://192.168.1.100:1883" maxlength="127">
                </div>
                <div class="form-group">
                    <label for="mqtt-cfg-user">Username</label>
                    <input type="text" id="mqtt-cfg-user" placeholder="Username (optional)" maxlength="32">
                </div>
                <div class="form-group">
                    <label for="mqtt-cfg-pass">Password</label>
                    <input type="password" id="mqtt-cfg-pass" placeholder="Password (optional)" maxlength="64">
                </div>
                <button onclick="saveMqttConfig()" class="btn btn-primary" id="mqtt-save-btn">Save MQTT</button>
                <span id="mqtt-save-status" style="margin-left:10px;"></span>
                <hr>
                <h3>Danger Zone</h3>
                <button onclick="factoryReset()" class="btn btn-danger">Factory Reset</button>
            </div>
        </div>
    </div>

    <!-- Command Modal -->
    <div id="cmd-modal" class="modal" style="display:none;">
        <div class="modal-content modal-small">
            <div class="modal-header">
                <h2>Node Control</h2>
                <button onclick="closeCmdModal()" class="btn btn-small">✕</button>
            </div>
            <div class="modal-body">
                <p>Node: <strong id="cmd-node-name">--</strong></p>
                <p>MAC: <span id="cmd-node-mac">--</span></p>
                <div class="cmd-buttons" id="cmd-buttons"></div>
            </div>
        </div>
    </div>

    <!-- Node OTA Modal -->
    <div id="node-ota-modal" class="modal" style="display:none;">
        <div class="modal-content">
            <div class="modal-header">
                <h2>Node Firmware Update</h2>
                <button onclick="closeNodeOtaModal()" class="btn btn-small">✕</button>
            </div>
            <div class="modal-body">
                <p>Target Node: <strong id="node-ota-name">--</strong></p>
                <p>MAC: <span id="node-ota-mac">--</span></p>
                <hr style="margin: 15px 0; border-color: var(--border);">

                <!-- Upload Box -->
                <div id="node-ota-upload-box">
                    <input type="file" id="node-firmware-file" accept=".bin" style="display:none;" onchange="handleNodeFirmwareSelect(event)">
                    <div class="ota-dropzone" id="node-ota-dropzone" onclick="document.getElementById('node-firmware-file').click()">
                        <div class="ota-dropzone-icon">📦</div>
                        <div class="ota-dropzone-text">Click or drag node firmware.bin</div>
                        <div class="ota-dropzone-hint">Max 1.5MB</div>
                    </div>
                </div>

                <!-- Progress -->
                <div id="node-ota-progress" style="display:none;">
                    <div class="ota-upload-info">
                        <span id="node-ota-filename">firmware.bin</span>
                        <span id="node-ota-percent">0%</span>
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill" id="node-ota-fill"></div>
                    </div>
                    <div class="ota-upload-status" id="node-ota-status">Uploading...</div>
                </div>
            </div>
        </div>
    </div>

    <script src="/app.js"></script>
</body>
</html>
)rawliteral";

// ============================================================================
// CSS Stylesheet
// ============================================================================
static const char CSS_STYLE[] = R"rawliteral(
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

:root {
    --bg-primary: #1a1a2e;
    --bg-secondary: #16213e;
    --bg-card: #0f3460;
    --accent: #e94560;
    --accent-hover: #ff6b6b;
    --text-primary: #eaeaea;
    --text-secondary: #a0a0a0;
    --success: #4ade80;
    --warning: #fbbf24;
    --danger: #ef4444;
    --border: #2d3748;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: var(--bg-primary);
    color: var(--text-primary);
    min-height: 100vh;
}

header {
    background: var(--bg-secondary);
    padding: 1rem 2rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid var(--border);
}

.header-left {
    display: flex;
    align-items: center;
    gap: 1rem;
}

.header-left h1 {
    font-size: 1.5rem;
    font-weight: 600;
}

.header-right {
    display: flex;
    gap: 0.5rem;
}

.status-dot {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    animation: pulse 2s infinite;
}

.status-dot.online { background: var(--success); }
.status-dot.offline { background: var(--danger); }

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}

main {
    max-width: 1200px;
    margin: 0 auto;
    padding: 1.5rem;
}

.cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 1rem;
    margin-bottom: 1.5rem;
}

.card {
    background: var(--bg-card);
    border-radius: 12px;
    padding: 1.25rem;
    text-align: center;
    transition: transform 0.2s;
}

.card:hover {
    transform: translateY(-2px);
}

.card-icon {
    font-size: 1.5rem;
    margin-bottom: 0.5rem;
}

.card-value {
    font-size: 1.25rem;
    font-weight: 600;
    margin-bottom: 0.25rem;
}

.card-label {
    font-size: 0.8rem;
    color: var(--text-secondary);
}

.panel {
    background: var(--bg-secondary);
    border-radius: 12px;
    margin-bottom: 1.5rem;
    overflow: hidden;
}

.panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem 1.25rem;
    border-bottom: 1px solid var(--border);
}

.panel-header h2 {
    font-size: 1.1rem;
    font-weight: 600;
}

.panel-content {
    padding: 1rem;
}

table {
    width: 100%;
    border-collapse: collapse;
}

th, td {
    padding: 0.75rem;
    text-align: left;
    border-bottom: 1px solid var(--border);
}

th {
    font-weight: 600;
    color: var(--text-secondary);
    font-size: 0.85rem;
}

tr:hover {
    background: rgba(255,255,255,0.02);
}

.loading {
    text-align: center;
    color: var(--text-secondary);
    padding: 2rem !important;
}

.btn {
    padding: 0.5rem 1rem;
    border: none;
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.9rem;
    transition: all 0.2s;
}

.btn-primary {
    background: var(--accent);
    color: white;
}

.btn-primary:hover {
    background: var(--accent-hover);
}

.btn-warning {
    background: var(--warning);
    color: #1a1a2e;
}

.btn-danger {
    background: var(--danger);
    color: white;
}

.btn-small {
    padding: 0.25rem 0.5rem;
    font-size: 0.8rem;
    background: var(--bg-card);
    color: var(--text-primary);
}

.btn-icon {
    background: transparent;
    font-size: 1.25rem;
    padding: 0.25rem 0.5rem;
}

.btn-action {
    background: var(--bg-card);
    color: var(--text-primary);
    padding: 0.35rem 0.75rem;
    font-size: 0.85rem;
}

.logs-container {
    max-height: 200px;
    overflow-y: auto;
    font-family: monospace;
    font-size: 0.85rem;
}

#logs {
    display: flex;
    flex-direction: column;
}

.log-entry {
    padding: 0.35rem 0;
    border-bottom: 1px solid var(--border);
    display: flex;
    gap: 1rem;
}

.log-time {
    color: var(--text-secondary);
    min-width: 70px;
}

.ota-section {
    display: flex;
    flex-direction: column;
    gap: 1rem;
}

.ota-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.progress-bar {
    height: 8px;
    background: var(--bg-card);
    border-radius: 4px;
    overflow: hidden;
    flex: 1;
    margin-right: 1rem;
}

.progress-fill {
    height: 100%;
    background: var(--success);
    transition: width 0.3s;
}

.ota-progress {
    display: flex;
    align-items: center;
}

.ota-node-label {
    margin-right: 0.75rem;
    font-size: 0.85rem;
    color: var(--text-secondary);
}

.ota-upload-box {
    margin-top: 0.5rem;
}

.ota-dropzone {
    border: 2px dashed var(--border);
    border-radius: 8px;
    padding: 1.5rem;
    text-align: center;
    cursor: pointer;
    transition: all 0.2s;
}

.ota-dropzone:hover, .ota-dropzone.dragover {
    border-color: var(--accent);
    background: rgba(233, 69, 96, 0.1);
}

.ota-dropzone-icon {
    font-size: 2rem;
    margin-bottom: 0.5rem;
}

.ota-dropzone-text {
    font-size: 0.95rem;
    margin-bottom: 0.25rem;
}

.ota-dropzone-hint {
    font-size: 0.8rem;
    color: var(--text-secondary);
}

.ota-upload-progress {
    margin-top: 1rem;
    padding: 1rem;
    background: var(--bg-card);
    border-radius: 8px;
}

.ota-upload-info {
    display: flex;
    justify-content: space-between;
    margin-bottom: 0.5rem;
    font-size: 0.9rem;
}

.ota-upload-status {
    margin-top: 0.5rem;
    font-size: 0.85rem;
    color: var(--text-secondary);
}

.ota-upload-status.success {
    color: var(--success);
}

.ota-upload-status.error {
    color: var(--danger);
}

.modal {
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: rgba(0,0,0,0.7);
    display: flex;
    justify-content: center;
    align-items: center;
    z-index: 1000;
}

.modal-content {
    background: var(--bg-secondary);
    border-radius: 12px;
    width: 90%;
    max-width: 500px;
    max-height: 80vh;
    overflow-y: auto;
}

.modal-small {
    max-width: 350px;
}

.modal-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem 1.25rem;
    border-bottom: 1px solid var(--border);
}

.modal-body {
    padding: 1.25rem;
}

.modal-body h3 {
    margin-bottom: 0.75rem;
    font-size: 1rem;
}

.modal-body p {
    margin-bottom: 0.5rem;
    color: var(--text-secondary);
}

.modal-body hr {
    border: none;
    border-top: 1px solid var(--border);
    margin: 1rem 0;
}

.form-group {
    margin-bottom: 0.75rem;
}

.form-group label {
    display: block;
    margin-bottom: 0.25rem;
    color: var(--text-secondary);
    font-size: 0.85rem;
}

.form-group input[type="text"],
.form-group input[type="password"],
.form-group select {
    width: 100%;
    padding: 0.5rem;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg-card);
    color: var(--text-primary);
    font-size: 0.9rem;
    box-sizing: border-box;
}

.form-group input:focus,
.form-group select:focus {
    outline: none;
    border-color: var(--accent);
}

.btn-primary {
    background: var(--accent);
    color: #fff;
    border: none;
    padding: 0.5rem 1.25rem;
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.9rem;
}

.btn-primary:hover {
    opacity: 0.85;
}

.cmd-buttons {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 0.5rem;
    margin-top: 1rem;
}

.cmd-buttons .btn {
    padding: 0.75rem;
}

.status-online { color: var(--success); }
.status-offline { color: var(--danger); }

.scan-status {
    font-size: 0.8rem;
    padding: 0.2rem 0.5rem;
    border-radius: 4px;
    margin-left: 0.5rem;
}

.scan-status.scanning {
    background: var(--warning);
    color: #1a1a2e;
    animation: pulse 1s infinite;
}

.scan-status.stopped {
    background: var(--bg-card);
    color: var(--text-secondary);
}

.panel-actions {
    display: flex;
    gap: 0.5rem;
}

@media (max-width: 600px) {
    header {
        padding: 1rem;
    }
    .header-left h1 {
        font-size: 1.2rem;
    }
    .cards {
        grid-template-columns: repeat(2, 1fr);
    }
    th, td {
        padding: 0.5rem;
        font-size: 0.85rem;
    }
}
)rawliteral";

// ============================================================================
// JavaScript Application
// ============================================================================
static const char JS_APP[] = R"rawliteral(
let ws = null;
let wsReconnectTimer = null;
let scanRefreshTimer = null;
let isScanning = false;
let refreshTimer = null;
let otaInProgress = false;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    connectWebSocket();
    refreshAll();
    refreshTimer = setInterval(refreshAll, 3000);
});

function stopPolling() {
    otaInProgress = true;
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
    if (scanRefreshTimer) { clearInterval(scanRefreshTimer); scanRefreshTimer = null; }
    if (ws) { ws.close(); ws = null; }
    if (wsReconnectTimer) { clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }
    console.log('Polling stopped for OTA');
}

function resumePolling() {
    otaInProgress = false;
    if (!refreshTimer) { refreshTimer = setInterval(refreshAll, 3000); }
    if (!ws) { connectWebSocket(); }
    console.log('Polling resumed');
}

// WebSocket
function connectWebSocket() {
    const wsUrl = `ws://${window.location.host}/ws`;
    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.log('WebSocket connected');
        document.getElementById('status-indicator').className = 'status-dot online';
    };

    ws.onclose = () => {
        console.log('WebSocket disconnected');
        document.getElementById('status-indicator').className = 'status-dot offline';
        wsReconnectTimer = setTimeout(connectWebSocket, 3000);
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'log') {
                addLogEntry(data.ts, data.msg);
            }
        } catch (e) {}
    };
}

// API calls
async function api(endpoint, method = 'GET', body = null) {
    const options = { method };
    if (body) {
        options.headers = { 'Content-Type': 'application/json' };
        options.body = JSON.stringify(body);
    }
    const response = await fetch(endpoint, options);
    return response.json();
}

// Refresh all data
async function refreshAll() {
    if (otaInProgress) return;
    try {
        await Promise.all([
            refreshStatus(),
            refreshNetwork(),
            refreshMesh(),
            refreshNodes(),
            refreshOTA()
        ]);
    } catch (e) {
        console.error('Refresh error:', e);
    }
}

async function refreshStatus() {
    const data = await api('/api/status');
    document.getElementById('uptime').textContent = formatUptime(data.uptime);
    document.getElementById('heap-free').textContent = Math.round(data.heap_free / 1024) + ' KB';
    document.getElementById('gateway-mac').textContent = data.mac;
    document.getElementById('gw-version').textContent = 'v' + data.firmware;
}

async function refreshNetwork() {
    const data = await api('/api/network');
    if (data.wifi && data.wifi.connected) {
        document.getElementById('wifi-rssi').textContent = data.wifi.rssi + ' dBm';
        document.getElementById('wifi-ssid').textContent = data.wifi.ssid;
    } else {
        document.getElementById('wifi-rssi').textContent = '--';
    }
    if (data.ip) {
        document.getElementById('gateway-ip').textContent = data.ip;
    }
    // Route indicator
    const route = data.route || 'NONE';
    const routeEl = document.getElementById('route-status');
    const routeIcon = document.getElementById('route-icon');
    if (route === 'ETH') {
        routeEl.textContent = 'Ethernet';
        routeEl.style.color = '#4caf50';
        routeIcon.textContent = '🔌';
    } else if (route === 'WiFi') {
        routeEl.textContent = 'WiFi';
        routeEl.style.color = '#2196f3';
        routeIcon.textContent = '📶';
    } else {
        routeEl.textContent = 'Offline';
        routeEl.style.color = '#f44336';
        routeIcon.textContent = '❌';
    }
    document.getElementById('mqtt-status').textContent = data.mqtt.connected ? 'Connected' : 'Disconnected';
    document.getElementById('mqtt-icon').textContent = data.mqtt.connected ? '🔗' : '⛓️‍💥';
}

async function refreshMesh() {
    const data = await api('/api/mesh');
    document.getElementById('mesh-info').textContent = `Ch ${data.channel}`;
    document.getElementById('node-count').textContent = `${data.node_count} nodes`;
}

async function refreshNodes() {
    const data = await api('/api/nodes');
    const tbody = document.getElementById('nodes-body');

    if (data.nodes.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" class="loading">No nodes connected</td></tr>';
        return;
    }

    tbody.innerHTML = data.nodes.map(node => `
        <tr>
            <td>${escapeHtml(node.name)}</td>
            <td>${node.type_name}</td>
            <td><code style="font-size:0.85em">${node.firmware || '--'}</code></td>
            <td class="${node.online ? 'status-online' : 'status-offline'}">
                ${node.online ? '🟢 Online' : '🔴 Offline'}
            </td>
            <td>${node.online ? node.rssi + ' dBm' : '--'}</td>
            <td>${formatLastSeen(node.last_seen_sec)}</td>
            <td>
                <button class="btn btn-action" onclick="showCmdModal('${node.mac}', '${escapeHtml(node.name)}', ${node.device_type})">⚡</button>
                <button class="btn btn-action" onclick="showNodeOtaModal('${node.mac}', '${escapeHtml(node.name)}')" title="Firmware Update">📦</button>
            </td>
        </tr>
    `).join('');
}

async function refreshOTA() {
    const data = await api('/api/ota/status');
    const progressDiv = document.getElementById('ota-progress');

    if (data.active && data.total > 0) {
        progressDiv.style.display = 'flex';
        const pct = Math.round((data.completed / data.total) * 100);
        document.getElementById('ota-fill').style.width = pct + '%';
        document.getElementById('ota-text').textContent = `${pct}% (${data.completed}/${data.total} nodes)`;
    } else {
        progressDiv.style.display = 'none';
    }
}

// Scan
async function startScan() {
    document.getElementById('scan-btn').disabled = true;
    document.getElementById('scan-btn').textContent = '⏳ Scanning...';

    try {
        const result = await api('/api/scan', 'POST');
        console.log('Scan start result:', result);

        if (result && result.success) {
            isScanning = true;
            updateScanStatus();

            // Show discovered panel immediately
            document.getElementById('scan-panel').style.display = 'block';

            // Clear any existing timer
            if (scanRefreshTimer) clearInterval(scanRefreshTimer);

            // Start periodic refresh of scan results (every 2 seconds)
            scanRefreshTimer = setInterval(refreshScanResults, 2000);

            // Do first refresh immediately (show "Waiting for nodes...")
            await refreshScanResults();
        } else {
            alert('Failed to start scan. Is a scan already in progress?');
        }
    } catch (e) {
        console.error('Scan error:', e);
        alert('Scan error: ' + e.message);
    }

    document.getElementById('scan-btn').disabled = false;
    document.getElementById('scan-btn').textContent = '🔍 Scan';
}

async function stopScan() {
    document.getElementById('stop-scan-btn').disabled = true;
    document.getElementById('stop-scan-btn').textContent = 'Stopping...';

    await api('/api/scan/stop', 'POST');

    isScanning = false;
    if (scanRefreshTimer) {
        clearInterval(scanRefreshTimer);
        scanRefreshTimer = null;
    }

    updateScanStatus();
    document.getElementById('stop-scan-btn').disabled = false;
    document.getElementById('stop-scan-btn').textContent = 'Stop Scan';

    // Final refresh of results and nodes
    await refreshScanResults();
    await refreshNodes();
}

function updateScanStatus() {
    const statusEl = document.getElementById('scan-status');
    const stopBtn = document.getElementById('stop-scan-btn');

    if (isScanning) {
        statusEl.textContent = 'Scanning...';
        statusEl.className = 'scan-status scanning';
        stopBtn.style.display = 'inline-block';
    } else {
        statusEl.textContent = 'Stopped';
        statusEl.className = 'scan-status stopped';
        stopBtn.style.display = 'none';
    }
}

async function refreshScanResults() {
    try {
        const data = await api('/api/scan/results');
        const tbody = document.getElementById('scan-body');

        console.log('Scan results:', data);

        if (!data || !data.results) {
            console.error('Invalid scan results data:', data);
            tbody.innerHTML = '<tr><td colspan="5" class="loading">Error loading results</td></tr>';
            return;
        }

        // Update scanning state from server
        if (data.scanning !== isScanning) {
            isScanning = data.scanning;
            updateScanStatus();
            if (!isScanning && scanRefreshTimer) {
                clearInterval(scanRefreshTimer);
                scanRefreshTimer = null;
            }
        }

        // Filter only uncommissioned nodes
        const uncommissioned = data.results.filter(r => !r.commissioned);
        console.log('Uncommissioned nodes:', uncommissioned.length);

        if (uncommissioned.length === 0) {
            tbody.innerHTML = '<tr><td colspan="5" class="loading">' +
                (isScanning ? 'Waiting for nodes...' : 'No new nodes found') + '</td></tr>';
            return;
        }

        tbody.innerHTML = uncommissioned.map(node => `
            <tr>
                <td>${node.mac}</td>
                <td>${getTypeName(node.device_type)}</td>
                <td>${node.firmware}</td>
                <td>${node.rssi} dBm</td>
                <td>
                    <button class="btn btn-primary btn-small" onclick="commissionNode('${node.mac}')">+ Add</button>
                </td>
            </tr>
        `).join('');
    } catch (e) {
        console.error('refreshScanResults error:', e);
    }
}

async function hideScanPanel() {
    // Stop scan when closing the panel
    if (isScanning) {
        await stopScan();
    }
    document.getElementById('scan-panel').style.display = 'none';
}

async function commissionNode(mac) {
    const name = prompt('Enter node name:', mac.replace(/:/g, ''));
    if (name === null) return;

    // Commission the node
    await api('/api/commission', 'POST', { mac, name });

    // Refresh scan results (node should now show as commissioned)
    await refreshScanResults();

    // The gateway will switch back to production mode after commissioning
    // So stop our local scan state
    if (isScanning) {
        isScanning = false;
        if (scanRefreshTimer) {
            clearInterval(scanRefreshTimer);
            scanRefreshTimer = null;
        }
        updateScanStatus();
    }

    // Refresh nodes list
    await refreshNodes();
}

// Commands
let currentCmdMac = '';

function showCmdModal(mac, name, deviceType) {
    currentCmdMac = mac;
    document.getElementById('cmd-node-name').textContent = name;
    document.getElementById('cmd-node-mac').textContent = mac;

    const buttons = document.getElementById('cmd-buttons');
    let html = '';

    if (deviceType === 1) { // Relay
        html = `
            <button class="btn btn-primary" onclick="sendCmd('relay_on')">💡 ON</button>
            <button class="btn btn-small" onclick="sendCmd('relay_off')">OFF</button>
            <button class="btn btn-small" onclick="sendCmd('relay_toggle')">Toggle</button>
            <button class="btn btn-small" onclick="sendCmd('identify')">Identify</button>
        `;
        // Add relay mode selector
        html += `
            <div style="grid-column: span 2; margin-top: 10px; padding-top: 10px; border-top: 1px solid var(--border);">
                <label style="font-size: 0.85rem; color: var(--text-secondary);">Relay Mode:</label>
                <div style="display: flex; gap: 8px; margin-top: 5px;">
                    <button class="btn btn-action" onclick="setRelayMode('gpio')" title="GPIO direct (IN/CH1 pin)">🔌 GPIO</button>
                    <button class="btn btn-action" onclick="setRelayMode('uart')" title="UART serial (RX/TX pins)">📡 UART</button>
                </div>
            </div>
        `;
    } else if (deviceType === 2) { // LED
        html = `
            <button class="btn btn-primary" onclick="sendCmd('led_on')">💡 ON</button>
            <button class="btn btn-small" onclick="sendCmd('led_off')">OFF</button>
            <button class="btn btn-small" onclick="sendCmd('identify')">Identify</button>
        `;
    } else {
        html = `
            <button class="btn btn-small" onclick="sendCmd('identify')">Identify</button>
        `;
    }

    html += `<button class="btn btn-warning" onclick="sendCmd('reboot')">Reboot</button>`;

    buttons.innerHTML = html;
    document.getElementById('cmd-modal').style.display = 'flex';
}

function closeCmdModal() {
    document.getElementById('cmd-modal').style.display = 'none';
}

async function sendCmd(cmd) {
    await api('/api/command', 'POST', { mac: currentCmdMac, cmd });
    closeCmdModal();
}

async function setRelayMode(mode) {
    if (!confirm(`Set relay mode to ${mode.toUpperCase()}?\n\nGPIO = IN/CH1 pin (direct control)\nUART = RX/TX pins (serial command)`)) {
        return;
    }
    try {
        const result = await api('/api/node/config', 'POST', {
            mac: currentCmdMac,
            key: 'relay_mode',
            value: mode
        });
        if (result.success) {
            alert(`Relay mode set to ${mode.toUpperCase()}`);
        } else {
            alert('Failed to set relay mode: ' + (result.error || 'Unknown error'));
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}

// Logs
function addLogEntry(ts, msg) {
    const logs = document.getElementById('logs');
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.innerHTML = `<span class="log-time">${formatTime(ts)}</span><span>${escapeHtml(msg)}</span>`;
    logs.insertBefore(entry, logs.firstChild);

    // Keep max 50 entries
    while (logs.children.length > 50) {
        logs.removeChild(logs.lastChild);
    }
}

function clearLogs() {
    document.getElementById('logs').innerHTML = '';
}

// Settings
function toggleSettings() {
    const modal = document.getElementById('settings-modal');
    const isOpening = modal.style.display === 'none';
    modal.style.display = isOpening ? 'flex' : 'none';
    if (isOpening) loadProvisionStatus();
}

async function loadProvisionStatus() {
    try {
        const r = await fetch('/api/provision/status');
        const d = await r.json();
        if (d.wifi && d.wifi.ssid) {
            const sel = document.getElementById('wifi-cfg-ssid');
            // Add current SSID as option and select it
            const opt = document.createElement('option');
            opt.value = d.wifi.ssid;
            opt.textContent = d.wifi.ssid + ' (current)';
            sel.appendChild(opt);
            sel.value = d.wifi.ssid;
        }
        if (d.mqtt) {
            if (d.mqtt.broker_uri) document.getElementById('mqtt-cfg-uri').value = d.mqtt.broker_uri;
            if (d.mqtt.username) document.getElementById('mqtt-cfg-user').value = d.mqtt.username;
        }
    } catch(e) {}
}

async function scanWifi() {
    const btn = document.getElementById('wifi-scan-btn');
    const sel = document.getElementById('wifi-cfg-ssid');
    const prevValue = sel.value;

    btn.disabled = true;
    btn.textContent = 'Scanning...';
    sel.innerHTML = '<option value="">Scanning...</option>';

    try {
        const r = await fetch('/api/wifi/scan');
        const d = await r.json();

        sel.innerHTML = '<option value="">-- Select network --</option>';

        if (d.networks && d.networks.length > 0) {
            d.networks.forEach(net => {
                const opt = document.createElement('option');
                opt.value = net.ssid;
                const lock = net.secure ? '🔒' : '🔓';
                const bars = net.rssi > -50 ? '▓▓▓▓' : net.rssi > -65 ? '▓▓▓░' : net.rssi > -75 ? '▓▓░░' : '▓░░░';
                opt.textContent = lock + ' ' + net.ssid + '  ' + bars + ' (' + net.rssi + 'dBm, Ch' + net.channel + ')';
                sel.appendChild(opt);
            });
            // Re-select previous value if still in list
            if (prevValue) sel.value = prevValue;
        } else {
            sel.innerHTML = '<option value="">No networks found</option>';
        }
    } catch(e) {
        sel.innerHTML = '<option value="">Scan error</option>';
    }

    btn.disabled = false;
    btn.textContent = 'Scan';
}

async function saveWifiConfig() {
    const ssid = document.getElementById('wifi-cfg-ssid').value.trim();
    const pass = document.getElementById('wifi-cfg-pass').value;
    const st = document.getElementById('wifi-save-status');
    if (!ssid) { st.textContent = 'SSID required'; return; }
    st.textContent = 'Saving...';
    try {
        const r = await fetch('/api/provision/wifi', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ssid: ssid, password: pass})
        });
        const d = await r.json();
        st.textContent = d.success ? 'Saved! Rebooting...' : (d.error || 'Error');
    } catch(e) { st.textContent = 'Connection lost (rebooting)'; }
}

async function saveMqttConfig() {
    const uri = document.getElementById('mqtt-cfg-uri').value.trim();
    const user = document.getElementById('mqtt-cfg-user').value.trim();
    const pass = document.getElementById('mqtt-cfg-pass').value;
    const st = document.getElementById('mqtt-save-status');
    if (!uri) { st.textContent = 'Broker URI required'; return; }
    st.textContent = 'Saving...';
    try {
        const r = await fetch('/api/provision/mqtt', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({broker_uri: uri, username: user, password: pass})
        });
        const d = await r.json();
        st.textContent = d.success ? 'Saved! Reboot to apply.' : (d.error || 'Error');
    } catch(e) { st.textContent = 'Error'; }
}

async function rebootGateway() {
    if (!confirm('Reboot gateway?')) return;
    await api('/api/reboot', 'POST');
}

async function factoryReset() {
    if (!confirm('Factory reset? All settings will be lost!')) return;
    if (!confirm('Are you REALLY sure?')) return;
    await api('/api/factory-reset', 'POST');
}

// Helpers
function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

function formatTime(ts) {
    const h = Math.floor((ts % 86400) / 3600);
    const m = Math.floor((ts % 3600) / 60);
    const s = ts % 60;
    return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

function formatLastSeen(sec) {
    if (sec < 60) return sec + 's ago';
    if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
    return Math.floor(sec / 3600) + 'h ago';
}

function pad(n) {
    return n.toString().padStart(2, '0');
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function getTypeName(type) {
    switch(type) {
        case 1: return 'Relay';
        case 2: return 'LED';
        case 0x10: return 'Sensor';
        default: return 'Unknown';
    }
}

// ============================================================================
// OTA Firmware Upload
// ============================================================================

// Setup drag and drop on page load
document.addEventListener('DOMContentLoaded', () => {
    const dropzone = document.getElementById('ota-dropzone');
    if (dropzone) {
        dropzone.addEventListener('dragover', (e) => {
            e.preventDefault();
            dropzone.classList.add('dragover');
        });
        dropzone.addEventListener('dragleave', () => {
            dropzone.classList.remove('dragover');
        });
        dropzone.addEventListener('drop', (e) => {
            e.preventDefault();
            dropzone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) {
                handleFirmwareFile(e.dataTransfer.files[0]);
            }
        });
    }
});

function handleFirmwareSelect(event) {
    if (event.target.files.length > 0) {
        handleFirmwareFile(event.target.files[0]);
    }
}

function handleFirmwareFile(file) {
    // Validate file
    if (!file.name.endsWith('.bin')) {
        alert('Please select a .bin firmware file');
        return;
    }

    if (file.size > 2 * 1024 * 1024) {
        alert('Firmware file too large (max 2MB)');
        return;
    }

    if (file.size < 1024) {
        alert('Firmware file too small (corrupted?)');
        return;
    }

    // Confirm upload
    const sizeMB = (file.size / 1024 / 1024).toFixed(2);
    if (!confirm(`Upload ${file.name} (${sizeMB} MB) and flash gateway?\n\nThe gateway will reboot after flashing.`)) {
        return;
    }

    // Start upload
    uploadFirmware(file);
}

async function uploadFirmware(file) {
    const uploadBox = document.getElementById('ota-upload-box');
    const progressDiv = document.getElementById('ota-upload-progress');
    const filenameEl = document.getElementById('ota-upload-filename');
    const percentEl = document.getElementById('ota-upload-percent');
    const fillEl = document.getElementById('ota-upload-fill');
    const statusEl = document.getElementById('ota-upload-status');

    // CRITICAL: Stop all polling and WebSocket during OTA to prevent data corruption
    stopPolling();

    // Show progress UI, hide dropzone
    uploadBox.style.display = 'none';
    progressDiv.style.display = 'block';
    filenameEl.textContent = file.name;
    percentEl.textContent = '0%';
    fillEl.style.width = '0%';
    statusEl.textContent = 'Uploading firmware...';
    statusEl.className = 'ota-upload-status';

    try {
        // Use XMLHttpRequest for progress tracking
        const xhr = new XMLHttpRequest();

        const uploadPromise = new Promise((resolve, reject) => {
            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    percentEl.textContent = pct + '%';
                    fillEl.style.width = pct + '%';
                    statusEl.textContent = `Uploading... ${formatBytes(e.loaded)} / ${formatBytes(e.total)}`;
                }
            };

            xhr.onload = () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        resolve(JSON.parse(xhr.responseText));
                    } catch (e) {
                        resolve({ success: true });
                    }
                } else {
                    reject(new Error(`HTTP ${xhr.status}: ${xhr.statusText}`));
                }
            };

            xhr.onerror = () => reject(new Error('Network error'));
            xhr.ontimeout = () => reject(new Error('Upload timeout'));
        });

        xhr.open('POST', '/api/ota/upload', true);
        xhr.timeout = 120000; // 2 minute timeout
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.send(file);

        const result = await uploadPromise;

        if (result.success) {
            percentEl.textContent = '100%';
            fillEl.style.width = '100%';
            statusEl.textContent = 'Firmware uploaded! Gateway rebooting...';
            statusEl.className = 'ota-upload-status success';

            // Wait for reboot, then start checking if gateway is back
            setTimeout(() => {
                statusEl.textContent = 'Gateway rebooting, please wait...';
                checkGatewayAlive();
            }, 5000);
        } else {
            throw new Error(result.error || 'Unknown error');
        }

    } catch (e) {
        console.error('OTA upload error:', e);
        statusEl.textContent = 'Upload failed: ' + e.message;
        statusEl.className = 'ota-upload-status error';
        resumePolling();

        // Show upload box again after error
        setTimeout(() => {
            uploadBox.style.display = 'block';
            progressDiv.style.display = 'none';
        }, 5000);
    }
}

async function checkGatewayAlive() {
    const statusEl = document.getElementById('ota-upload-status');
    let attempts = 0;
    const maxAttempts = 30; // 30 seconds

    const check = async () => {
        attempts++;
        try {
            const response = await fetch('/api/status', { timeout: 2000 });
            if (response.ok) {
                statusEl.textContent = 'Gateway is back online! Reloading...';
                statusEl.className = 'ota-upload-status success';
                setTimeout(() => window.location.reload(), 1000);
                return;
            }
        } catch (e) {
            // Gateway not ready yet
        }

        if (attempts < maxAttempts) {
            statusEl.textContent = `Waiting for gateway... (${attempts}s)`;
            setTimeout(check, 1000);
        } else {
            statusEl.textContent = 'Gateway not responding. Please refresh manually.';
            statusEl.className = 'ota-upload-status error';
        }
    };

    check();
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

// ============================================================================
// Node OTA Functions
// ============================================================================
let nodeOtaMac = null;

function showNodeOtaModal(mac, name) {
    nodeOtaMac = mac;
    document.getElementById('node-ota-name').textContent = name;
    document.getElementById('node-ota-mac').textContent = mac;

    // Reset UI
    document.getElementById('node-ota-upload-box').style.display = 'block';
    document.getElementById('node-ota-progress').style.display = 'none';

    document.getElementById('node-ota-modal').style.display = 'flex';
}

function closeNodeOtaModal() {
    document.getElementById('node-ota-modal').style.display = 'none';
    nodeOtaMac = null;
}

function handleNodeFirmwareSelect(event) {
    if (event.target.files.length > 0) {
        handleNodeFirmwareFile(event.target.files[0]);
    }
}

function handleNodeFirmwareFile(file) {
    if (!file.name.endsWith('.bin')) {
        alert('Please select a .bin firmware file');
        return;
    }

    if (file.size > 1536 * 1024) {
        alert('Firmware file too large (max 1.5MB)');
        return;
    }

    if (file.size < 1024) {
        alert('Firmware file too small (corrupted?)');
        return;
    }

    const sizeMB = (file.size / 1024 / 1024).toFixed(2);
    const nodeName = document.getElementById('node-ota-name').textContent;
    if (!confirm(`Upload ${file.name} (${sizeMB} MB) to node "${nodeName}"?\n\nThe node will reboot after flashing.`)) {
        return;
    }

    uploadNodeFirmware(file);
}

async function uploadNodeFirmware(file) {
    const uploadBox = document.getElementById('node-ota-upload-box');
    const progressDiv = document.getElementById('node-ota-progress');
    const filenameEl = document.getElementById('node-ota-filename');
    const percentEl = document.getElementById('node-ota-percent');
    const fillEl = document.getElementById('node-ota-fill');
    const statusEl = document.getElementById('node-ota-status');

    uploadBox.style.display = 'none';
    progressDiv.style.display = 'block';
    filenameEl.textContent = file.name;
    percentEl.textContent = '0%';
    fillEl.style.width = '0%';
    statusEl.textContent = 'Uploading firmware to gateway...';
    statusEl.className = 'ota-upload-status';

    try {
        const xhr = new XMLHttpRequest();

        const uploadPromise = new Promise((resolve, reject) => {
            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 50); // Upload is 50%
                    percentEl.textContent = pct + '%';
                    fillEl.style.width = pct + '%';
                    statusEl.textContent = `Uploading... ${formatBytes(e.loaded)} / ${formatBytes(e.total)}`;
                }
            };

            xhr.onload = () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        resolve(JSON.parse(xhr.responseText));
                    } catch (e) {
                        resolve({ success: true });
                    }
                } else {
                    reject(new Error(`HTTP ${xhr.status}: ${xhr.statusText}`));
                }
            };

            xhr.onerror = () => reject(new Error('Network error'));
            xhr.ontimeout = () => reject(new Error('Upload timeout'));
        });

        // Send to node OTA endpoint with MAC
        xhr.open('POST', '/api/node/ota?mac=' + encodeURIComponent(nodeOtaMac), true);
        xhr.timeout = 120000;
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.send(file);

        const result = await uploadPromise;

        if (result.success) {
            percentEl.textContent = '50%';
            fillEl.style.width = '50%';
            statusEl.textContent = 'Firmware received. Pushing to node...';

            // Start polling node OTA status
            pollNodeOtaStatus();
        } else {
            throw new Error(result.error || 'Unknown error');
        }

    } catch (e) {
        console.error('Node OTA upload error:', e);
        statusEl.textContent = 'Upload failed: ' + e.message;
        statusEl.className = 'ota-upload-status error';

        setTimeout(() => {
            uploadBox.style.display = 'block';
            progressDiv.style.display = 'none';
        }, 5000);
    }
}

async function pollNodeOtaStatus() {
    const percentEl = document.getElementById('node-ota-percent');
    const fillEl = document.getElementById('node-ota-fill');
    const statusEl = document.getElementById('node-ota-status');
    const uploadBox = document.getElementById('node-ota-upload-box');
    const progressDiv = document.getElementById('node-ota-progress');

    let attempts = 0;
    const maxAttempts = 300; // 5 minutes max

    const check = async () => {
        attempts++;
        try {
            const data = await api('/api/node/ota/status');

            if (!data.active) {
                if (data.state_desc === 'complete') {
                    percentEl.textContent = '100%';
                    fillEl.style.width = '100%';
                    statusEl.textContent = 'Node OTA complete! Node is rebooting...';
                    statusEl.className = 'ota-upload-status success';
                    setTimeout(closeNodeOtaModal, 3000);
                    return;
                } else if (data.state_desc === 'failed' || data.state_desc === 'aborted') {
                    statusEl.textContent = 'Node OTA ' + data.state_desc;
                    statusEl.className = 'ota-upload-status error';
                    setTimeout(() => {
                        uploadBox.style.display = 'block';
                        progressDiv.style.display = 'none';
                    }, 5000);
                    return;
                } else if (data.state_desc === 'idle' && attempts > 5) {
                    // Became idle unexpectedly
                    statusEl.textContent = 'Node OTA ended unexpectedly';
                    statusEl.className = 'ota-upload-status error';
                    setTimeout(() => {
                        uploadBox.style.display = 'block';
                        progressDiv.style.display = 'none';
                    }, 5000);
                    return;
                }
            }

            // Active - show progress
            const overallPct = 50 + Math.round(data.progress / 2);
            percentEl.textContent = overallPct + '%';
            fillEl.style.width = overallPct + '%';
            statusEl.textContent = 'Pushing to node: ' + data.progress + '% (' + data.state_desc + ')';

            if (attempts < maxAttempts) {
                setTimeout(check, 1000);
            } else {
                statusEl.textContent = 'Node OTA timeout';
                statusEl.className = 'ota-upload-status error';
            }

        } catch (e) {
            if (attempts < maxAttempts) {
                setTimeout(check, 1000);
            }
        }
    };

    setTimeout(check, 500);
}

// Setup drag-drop for node OTA dropzone
document.addEventListener('DOMContentLoaded', function() {
    const nodeDropzone = document.getElementById('node-ota-dropzone');
    if (nodeDropzone) {
        nodeDropzone.addEventListener('dragover', function(e) {
            e.preventDefault();
            nodeDropzone.classList.add('dragover');
        });
        nodeDropzone.addEventListener('dragleave', function(e) {
            e.preventDefault();
            nodeDropzone.classList.remove('dragover');
        });
        nodeDropzone.addEventListener('drop', function(e) {
            e.preventDefault();
            nodeDropzone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) {
                handleNodeFirmwareFile(e.dataTransfer.files[0]);
            }
        });
    }
});
)rawliteral";

// ============================================================================
// Public Functions
// ============================================================================

const char* web_ui_get_html(void)
{
    return HTML_PAGE;
}

const char* web_ui_get_css(void)
{
    return CSS_STYLE;
}

const char* web_ui_get_js(void)
{
    return JS_APP;
}
