# OmniaPi Firmware

Professional-grade firmware for the OmniaPi home automation system.

## Overview

OmniaPi is an open-source home automation system designed to control 230V loads safely and reliably. The system consists of:

- **Gateway**: WT32-ETH01 (Ethernet + ESP-NOW master)
- **Nodes**: ESP32-C3 SuperMini (ESP-NOW slave + relay control)

Communication between gateway and nodes uses **ESP-NOW mesh**, providing low-latency, reliable communication without depending on WiFi infrastructure.

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
├── TODO.md                 # Development task list
├── README.md               # This file
├── gateway/                # Gateway firmware (WT32-ETH01)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── data/               # Web UI files (SPIFFS)
├── node/                   # Node firmware (ESP32-C3)
│   ├── platformio.ini
│   └── src/main.cpp
├── shared/                 # Shared code
│   ├── protocol/           # ESP-NOW message definitions
│   ├── config/             # Hardware configuration
│   └── security/           # Encryption (future)
├── docs/                   # Documentation
└── tools/                  # Build and flash scripts
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB cable for programming
- For WT32-ETH01: USB-TTL adapter (CH340 or similar)

### Building Gateway Firmware

```bash
cd gateway
pio run
pio run -t upload
```

### Building Node Firmware

```bash
cd node
pio run
pio run -t upload
```

### Important Settings

**WT32-ETH01:**
- Flash Frequency: 40MHz
- Flash Mode: DIO
- Hold IO0 to GND before flashing

**ESP32-C3 SuperMini:**
- USB CDC On Boot: Enabled (required for serial output)
- Flash Frequency: 40MHz
- Flash Mode: DIO

## Features

### Current (v0.1.0)
- [x] Ethernet connectivity (Gateway)
- [x] ESP-NOW initialization
- [x] Basic Web API
- [x] Relay control with inverted logic
- [x] State persistence (NVS)
- [x] Physical button support

### Planned
- [ ] Auto-discovery and pairing
- [ ] Full ESP-NOW mesh communication
- [ ] Heartbeat monitoring
- [ ] OTA updates
- [ ] AES encryption
- [ ] Dimmer support
- [ ] Sensor integration
- [ ] Backend API integration

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
