# OmniaPi Firmware

Professional-grade firmware for the OmniaPi home automation system.

> **Last Updated**: 02/01/2026
> **Gateway Version**: v1.3.0 (Arduino)
> **Node Version**: v2.0.0 (ESP-IDF)

## Overview

OmniaPi is an open-source home automation system designed to control 230V loads safely and reliably. The system consists of:

- **Gateway**: WT32-ETH01 (Arduino framework + ESP-NOW master)
- **Nodes**: ESP32-C3 SuperMini (**ESP-IDF framework** + ESP-NOW slave + relay control)

Communication between gateway and nodes uses **ESP-NOW mesh**, providing low-latency, reliable communication without depending on WiFi infrastructure.

### Important: Dual Framework Architecture

| Component | Framework | Build Tool | Reason |
|-----------|-----------|------------|--------|
| **Gateway** | Arduino | Arduino CLI | Easy web server + OTA |
| **Node** | **ESP-IDF** | idf.py | Reliable boot without USB |

The Node was migrated from Arduino to ESP-IDF on 02/01/2026 to fix a critical boot issue where ESP32-C3 would not start without USB connected.

## Architecture

```
                         ┌─────────────────────┐
                         │   Cloud OmniaPi     │ (optional)
                         │   - Backup          │
                         │   - Remote access   │
                         └─────────┬───────────┘
                                   │ Internet
                                   │
═══════════════════════════════════════════════════════════════
                              HOME
═══════════════════════════════════════════════════════════════
                                   │
                         ┌─────────▼───────────┐
                         │      GATEWAY        │
                         │   WT32-ETH01        │
                         │                     │
                         │   - Automations     │
                         │   - Scenes          │
                         │   - Timers          │
                         │   - Local Web UI    │
                         │   - Ethernet        │
                         └─────────┬───────────┘
                                   │ ESP-NOW Mesh
        ┌──────────────────┬───────┼───────┬──────────────────┐
        ▼                  ▼       ▼       ▼                  ▼
   ┌─────────┐        ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
   │  RELAY  │        │  RELAY  │ │ DIMMER  │ │ SENSOR  │ │  RELAY  │
   │  Node   │        │  Node   │ │  Node   │ │  Node   │ │  Node   │
   │ESP32-C3 │        │ESP32-C3 │ │ESP32-C3 │ │ESP32-C3 │ │ESP32-C3 │
   └─────────┘        └─────────┘ └─────────┘ └─────────┘ └─────────┘
```

## Hardware

### Gateway: WT32-ETH01 (ARCELI)
- ESP32 with integrated Ethernet (LAN8720)
- Dual-core processor for handling mesh + web server
- Connects to home network via Ethernet cable

### Nodes: ESP32-C3 SuperMini (TECNOIOT)
- Ultra-compact form factor
- USB-C for programming
- Low power consumption
- 2 relay channels per node

### Power Supply: HLK-PM01
- 220V AC to 5V DC converter
- Compact design fits in junction box
- Provides stable power for ESP32 + relays

### Relay Module: GTIWUNG 2ch 5V
- Dual-channel relay
- 5V control voltage
- **Inverted logic**: LOW = ON, HIGH = OFF
- Rated for 230V 10A loads

## Project Structure

```
OmniaPi_HomeDomotic_Firmware/
├── TODO.md                     # Development task list
├── README.md                   # This file
│
├── gateway/                    # Gateway firmware (Arduino)
│   ├── src/
│   │   └── main.cpp            # Gateway firmware
│   └── data/
│       └── index.html          # Web UI (SPIFFS)
│
├── node/                       # Node firmware (ESP-IDF!)
│   ├── CMakeLists.txt          # ESP-IDF project config
│   ├── sdkconfig.defaults      # Default settings
│   └── main/
│       ├── CMakeLists.txt      # Component config
│       ├── main.c              # Entry point
│       ├── espnow_handler.c/h  # ESP-NOW communication
│       ├── relay_control.c/h   # Relay GPIO control
│       └── led_status.c/h      # LED status patterns
│
├── shared/                     # Shared code
│   ├── protocol/
│   │   └── messages.h          # ESP-NOW message definitions
│   └── config/
│       └── hardware.h          # Hardware configuration
│
└── docs/                       # Documentation
```

## Getting Started

### Prerequisites

**Gateway (Arduino):**
- [Arduino CLI](https://arduino.github.io/arduino-cli/) v1.1.3+
- ESP32 Core 3.3.5
- USB-TTL adapter (CH340 or similar) for WT32-ETH01

**Node (ESP-IDF):**
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/) v5.5.2+
- USB cable (ESP32-C3 has native USB)

### Building Gateway Firmware

```bash
# Using Arduino CLI
cd gateway
arduino-cli compile --fqbn esp32:esp32:esp32 src/main.cpp
arduino-cli upload -p COM6 --fqbn esp32:esp32:esp32 src/main.cpp
```

### Building Node Firmware

**IMPORTANT**: Use ESP-IDF CMD prompt, NOT PowerShell or VS Code terminal!

```bash
# First time setup
cd node
idf.py set-target esp32c3

# Build and flash
idf.py build
idf.py -p COM8 flash

# Monitor serial output (optional)
idf.py -p COM8 monitor
```

### Important Settings

**WT32-ETH01 (Gateway):**
- Flash Frequency: 40MHz
- Flash Mode: DIO
- Hold IO0 to GND before flashing

**ESP32-C3 SuperMini (Node):**
- Framework: ESP-IDF (NOT Arduino!)
- Target: esp32c3
- The node boots independently of USB connection

## Features

### Current (Gateway v1.3.0 + Node v2.0.0)
- [x] Ethernet + WiFi connectivity (Gateway)
- [x] ESP-NOW bidirectional communication
- [x] Web UI with relay control
- [x] REST API (`/api/status`, `/api/nodes`, `/api/command`)
- [x] Relay control with inverted logic
- [x] Node tracking with firmware version display
- [x] Gateway OTA via Web UI
- [x] Node OTA via ESP-NOW
- [x] Fast heartbeat (3 seconds)
- [x] Independent boot (Node works without USB)

### Planned
- [ ] Physical button support on Node
- [ ] Auto-discovery and pairing
- [ ] NVS state persistence on Node
- [ ] AES encryption for ESP-NOW
- [ ] MQTT cloud integration
- [ ] Dimmer support
- [ ] Sensor integration

## Safety

**WARNING: This system controls 230V mains voltage. Always:**
- Disconnect power before working on the system
- Use appropriate wire gauges (min 1.5mm² for lighting)
- Install proper circuit breakers
- Follow local electrical codes
- If unsure, consult a qualified electrician

## License

MIT License - See LICENSE file for details.

## Contributing

Contributions are welcome! Please read the TODO.md file for current development priorities.

## Links

- [OmniaPi Backend](https://github.com/BomberStealth/OmniaPi_HomeDomotic_BE)
- [OmniaPi Frontend](https://github.com/BomberStealth/OmniaPi_HomeDomotic_FE)
