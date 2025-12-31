# OmniaPi Firmware - Piano di Sviluppo

> Questo file documenta il piano completo per lo sviluppo del firmware OmniaPi.
> Repository: `OmniaPi_HomeDomotic_Firmware`
> Data creazione: 29/12/2025
> **Ultimo aggiornamento: 30/12/2025**

---

## ✅ STATO ATTUALE (30/12/2025)

### Firmware Funzionanti
| Componente | Versione | File | Stato |
|------------|----------|------|-------|
| **Gateway** | v1.3.0 | `gateway_arduino_test.ino` | ✅ Funzionante + Controllo Relè |
| **Node** | v1.5.0 | `node_arduino_test.ino` | ✅ Funzionante (senza USB!) |

### Funzionalità Implementate
- ✅ **ESP-NOW Bidirezionale** - Gateway ↔ Node comunicano perfettamente
- ✅ **Gateway OTA via Web UI** - Aggiornamento firmware Gateway da browser
- ✅ **Node OTA via ESP-NOW** - Aggiornamento firmware Node tramite Gateway
- ✅ **Tracking Nodi** - Web UI mostra nodi connessi con versione firmware
- ✅ **Heartbeat veloce** - 3 secondi (rilevamento nodi quasi istantaneo)
- ✅ **LittleFS per Node FW** - Firmware Node salvato su flash (non RAM)
- ✅ **Controllo Relè via Web UI** - Pulsanti ON/OFF per ogni canale relè
- ✅ **API REST /api/command** - Endpoint per controllo relè programmatico

### Sistema di Build
```
⚠️ IMPORTANTE: Si usa Arduino CLI, NON PlatformIO!
```
| Impostazione | Valore |
|--------------|--------|
| Build Tool | Arduino CLI v1.1.3 |
| ESP32 Core | 3.3.5 (ESP-IDF 5.5) |
| Path Arduino CLI | `C:/Users/edoar/arduino-cli/arduino-cli.exe` |
| Board Gateway | `esp32:esp32:esp32` |
| Board Node | `esp32:esp32:esp32c3` |

### Comandi Build
```bash
# Gateway
arduino-cli compile --fqbn esp32:esp32:esp32 gateway_arduino_test.ino
arduino-cli upload -p COM6 --fqbn esp32:esp32:esp32 gateway_arduino_test.ino

# Node
arduino-cli compile --fqbn esp32:esp32:esp32c3 node_arduino_test.ino
arduino-cli upload -p COM8 --fqbn esp32:esp32:esp32c3 node_arduino_test.ino
```

---

## ARCHITETTURA SISTEMA

```
                         ┌─────────────────────┐
                         │   Cloud OmniaPi     │ (FASE 2 - opzionale)
                         │   - Backup          │
                         │   - Accesso remoto  │
                         └─────────┬───────────┘
                                   │ Internet
                                   │
═══════════════════════════════════════════════════════════════
                              CASA
═══════════════════════════════════════════════════════════════
                                   │
                         ┌─────────▼───────────┐
                         │      GATEWAY        │
                         │   WT32-ETH01        │
                         │   (ARCELI Amazon)   │
                         │                     │
                         │   - Automazioni     │
                         │   - Scene           │
                         │   - Timer           │
                         │   - Web UI locale   │
                         │   - Ethernet + WiFi │
                         └─────────┬───────────┘
                                   │ ESP-NOW Mesh
        ┌──────────────────┬───────┼───────┬──────────────────┐
        ▼                  ▼       ▼       ▼                  ▼
   ┌─────────┐        ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
   │  RELÈ   │◄──────►│  RELÈ   │◄│ DIMMER  │►│ SENSORE │►│  RELÈ   │
   │ Stanza1 │  mesh  │ Stanza2 │ │ Stanza3 │ │ Stanza4 │ │ Stanza5 │
   │         │        │         │ │         │ │         │ │         │
   │ESP32-C3 │        │ESP32-C3 │ │ESP32-C3 │ │ESP32-C3 │ │ESP32-C3 │
   │HLK-PM01 │        │HLK-PM01 │ │HLK-PM01 │ │HLK-PM01 │ │HLK-PM01 │
   │GTIWUNG  │        │GTIWUNG  │ │+TRIAC   │ │+PIR     │ │GTIWUNG  │
   └─────────┘        └─────────┘ └─────────┘ └─────────┘ └─────────┘
```

---

## HARDWARE CONFERMATO

### Gateway: WT32-ETH01 (ARCELI Amazon)
| Impostazione | Valore |
|--------------|--------|
| Board | `esp32:esp32:esp32` |
| Flash Frequency | 40MHz |
| Flash Mode | DIO |
| Upload Speed | 115200 |
| Porta Seriale | COM6 |
| WiFi SSID | `Porte Di Durin` |
| WiFi Password | `Mellon!!!` |
| WiFi Channel | 10 (automatico dal router) |
| IP attuale | **192.168.1.203** |
| Firmware attuale | **v1.3.0** |
| Storage Node FW | LittleFS (`/node_fw.bin`) |

### Nodi: ESP32-C3 SuperMini (TECNOIOT Amazon)
| Impostazione | Valore |
|--------------|--------|
| Board | `esp32:esp32:esp32c3` |
| Flash Frequency | 40MHz |
| Flash Mode | DIO |
| USB CDC On Boot | **ENABLED** (importante!) |
| Porta Seriale | COM8 |
| WiFi Channel | 10 (deve matchare Gateway) |
| Firmware attuale | **v1.5.0** |
| GPIO Relè 1 | GPIO1 |
| GPIO Relè 2 | GPIO2 |
| GPIO da evitare | GPIO0 (boot pin) |

### Alimentazione: HLK-PM01
- Input: 220V AC
- Output: 5V DC (testato: 5.11V)

### Relè: GTIWUNG 2ch 5V
- Alimentazione: 5V (NON 3.3V!)
- Logica: **INVERTITA** (LOW = ON, HIGH = OFF)

---

## STRUTTURA REPOSITORY

```
OmniaPi_HomeDomotic_Firmware/
├── TODO.md                    ← Task list (questo file copiato)
├── README.md                  ← Documentazione progetto
├── LICENSE                    ← Licenza (da definire)
│
├── gateway/                   ← Firmware WT32-ETH01
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── config.h
│   │   ├── ethernet.cpp/.h
│   │   ├── espnow_master.cpp/.h
│   │   ├── webserver.cpp/.h
│   │   ├── storage.cpp/.h
│   │   └── ota.cpp/.h
│   ├── include/
│   ├── lib/
│   └── data/                  ← Web UI files (SPIFFS)
│
├── node/                      ← Firmware ESP32-C3
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── config.h
│   │   ├── espnow_slave.cpp/.h
│   │   ├── relay.cpp/.h
│   │   ├── button.cpp/.h
│   │   ├── led.cpp/.h
│   │   └── storage.cpp/.h
│   ├── include/
│   └── lib/
│
├── shared/                    ← Codice condiviso
│   ├── protocol/
│   │   ├── messages.h         ← Definizioni messaggi ESP-NOW
│   │   └── commands.h         ← Comandi supportati
│   ├── security/
│   │   └── encryption.h       ← AES per ESP-NOW
│   └── config/
│       └── hardware.h         ← Pin definitions
│
├── docs/
│   ├── hardware.md            ← Schema collegamenti
│   ├── protocol.md            ← Specifiche protocollo
│   ├── production.md          ← Guida produzione
│   └── troubleshooting.md     ← Problemi comuni
│
└── tools/
    ├── flash_gateway.sh       ← Script flash gateway
    ├── flash_node.sh          ← Script flash nodo
    └── test_mesh.py           ← Test comunicazione
```

---

## MILESTONE E TASK

### ✅ MILESTONE 1: Setup Repository e Struttura Base (COMPLETATA)
- [x] Creare repository `OmniaPi_HomeDomotic_Firmware`
- [x] Inizializzare struttura cartelle (gateway/, node/, shared/)
- [x] ~~Configurare PlatformIO~~ → Usato Arduino CLI invece
- [x] Creare shared/protocol/messages.h con strutture base
- [ ] Creare README.md con overview progetto

### ✅ MILESTONE 2: Gateway - WiFi e Web Server (COMPLETATA)
- [x] Implementare connessione WiFi (WIFI_AP_STA mode)
- [x] Creare web server base (AsyncWebServer)
- [x] Implementare API REST: GET /api/status
- [x] Implementare API REST: GET /api/nodes
- [x] Creare pagina web UI base (HTML/CSS/JS inline)
- [x] Testare accesso da browser locale (192.168.1.203)
- [x] Implementare API REST: POST /api/command

### ✅ MILESTONE 3: Gateway - ESP-NOW Master (COMPLETATA)
- [x] Inizializzare ESP-NOW in modalità master (WIFI_IF_AP)
- [x] Implementare broadcast heartbeat (ogni 3 secondi)
- [x] Gestire registrazione nodi (MAC address tracking)
- [x] Implementare ricezione stati dai nodi
- [x] Gestire heartbeat (HEARTBEAT → HEARTBEAT_ACK)
- [x] Marcare nodi offline dopo timeout (10 secondi)
- [x] Implementare invio comandi a nodi specifici

### ✅ MILESTONE 4: Nodo - Base Funzionante (COMPLETATA)
- [x] Inizializzare ESP-NOW in modalità slave (WIFI_STA)
- [x] Rispondere a heartbeat del gateway
- [x] Inviare versione firmware al gateway
- [x] Ricevere comandi (ON/OFF/TOGGLE)
- [x] Controllare relè (logica invertita!)
- [x] Inviare stato al gateway dopo ogni cambio

### MILESTONE 5: Nodo - Pulsante Fisico
- [ ] Configurare GPIO per pulsante (con debounce)
- [ ] Toggle relè immediato su pressione (NO LATENZA!)
- [ ] Notificare gateway del cambio stato
- [ ] Funzionamento offline (senza gateway)
- [ ] Salvare stato in NVS per ripristino dopo blackout

### MILESTONE 6: Nodo - LED di Stato
- [ ] Definire pattern LED:
  - Lampeggio lento (1Hz) = cerca gateway
  - Lampeggio veloce (4Hz) = pairing
  - Fisso ON = connesso
  - Fisso OFF = errore critico
- [ ] Implementare state machine per LED
- [ ] LED feedback su pressione pulsante

### MILESTONE 7: Auto-Discovery e Pairing
- [ ] Nodo: modalità pairing (pulsante premuto 5 sec)
- [ ] Gateway: scansione nuovi dispositivi
- [ ] Assegnazione automatica node ID
- [ ] Salvataggio configurazione nodo in NVS
- [ ] Salvataggio lista nodi in gateway (LittleFS)

### MILESTONE 8: Sicurezza Base
- [ ] Generare chiave AES unica per impianto
- [ ] Crittografare messaggi ESP-NOW
- [ ] Validare MAC address (whitelist)
- [ ] Proteggere Web UI con password

### ✅ MILESTONE 9: OTA Updates (COMPLETATA)
- [x] Gateway: upload firmware da Web UI locale
- [x] Gateway: self-update OTA
- [x] Gateway: storage firmware Node su LittleFS
- [x] Gateway: distribuzione firmware ai nodi via ESP-NOW
- [x] Nodo: ricevere e applicare update OTA
- [ ] Rollback automatico se update fallisce

### MILESTONE 10: Watchdog e Recovery
- [ ] Implementare watchdog timer (reboot se bloccato)
- [ ] Salvare stato pre-crash
- [ ] Recovery automatico dopo reboot
- [ ] Logging errori per debug

### MILESTONE 11: Integrazione Backend (Cloud MQTT)
- [ ] Connessione MQTT al broker cloud
- [ ] Publish stato nodi su topic MQTT
- [ ] Subscribe a comandi da cloud
- [ ] Heartbeat con backend
- [ ] Testare controllo da Web App esistente

### MILESTONE 12: Tipi Dispositivo Aggiuntivi
- [ ] Dimmer (controllo TRIAC)
- [ ] Sensore PIR (motion detection)
- [ ] Sensore temperatura/umidità
- [ ] Tapparella (2 relè: su/giù)

### MILESTONE 13: Produzione
- [ ] Script flash automatizzato
- [ ] Test automatici hardware
- [ ] QR code per pairing rapido
- [ ] Documentazione utente finale
- [ ] Packaging firmware per release

---

## PROTOCOLLO ESP-NOW (Implementazione Attuale)

### Configurazione
```cpp
#define WIFI_CHANNEL 10           // Canale WiFi (uguale per tutti)
#define OTA_CHUNK_SIZE 200        // Max 250 bytes per ESP-NOW
#define HEARTBEAT_INTERVAL 3000   // 3 secondi
#define OFFLINE_TIMEOUT 10000     // 10 secondi
```

### Tipi Messaggio (Implementati)
| Type | Nome | Direzione | Descrizione |
|------|------|-----------|-------------|
| 0x01 | MSG_HEARTBEAT | G→N | Gateway ping broadcast |
| 0x02 | MSG_HEARTBEAT_ACK | N→G | Nodo risponde con versione FW |
| 0x10 | MSG_OTA_BEGIN | G→N | Inizio OTA (size in 4 bytes) |
| 0x11 | MSG_OTA_READY | N→G | Nodo pronto a ricevere |
| 0x12 | MSG_OTA_DATA | G→N | Chunk firmware (idx + data) |
| 0x13 | MSG_OTA_ACK | N→G | ACK con next chunk expected |
| 0x14 | MSG_OTA_END | G→N | Fine trasferimento |
| 0x15 | MSG_OTA_DONE | N→G | Nodo conferma update OK |
| 0x1F | MSG_OTA_ERROR | N→G | Errore durante OTA |

### Formato MSG_HEARTBEAT_ACK
```cpp
// Inviato dal Nodo al Gateway
uint8_t response[10] = {0};  // Zero-init importante!
response[0] = MSG_HEARTBEAT_ACK;  // 0x02
response[1] = otaInProgress;      // 0 o 1
response[2..9] = FIRMWARE_VERSION; // es. "1.2.0\0"
```

### Formato MSG_OTA_BEGIN
```cpp
// Gateway → Node
data[0] = MSG_OTA_BEGIN;  // 0x10
data[1] = size & 0xFF;
data[2] = (size >> 8) & 0xFF;
data[3] = (size >> 16) & 0xFF;
data[4] = (size >> 24) & 0xFF;
```

### Formato MSG_OTA_DATA
```cpp
// Gateway → Node
data[0] = MSG_OTA_DATA;   // 0x12
data[1..4] = chunkIndex;  // Little endian
data[5..N] = firmware_data; // Max 200 bytes
```

### ESP-IDF 5.5 Callback Signatures
```cpp
// IMPORTANTE: Queste sono le signature corrette per ESP32 Core 3.3.5

// Receive callback
void OnDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data, int len);

// Send callback
void OnDataSent(const wifi_tx_info_t *tx_info,
                esp_now_send_status_t status);
```

### Tipi Messaggio (Implementati v1.3.0)
| Type | Nome | Direzione | Descrizione |
|------|------|-----------|-------------|
| 0x20 | MSG_COMMAND | G→N | Comando relè (ON/OFF/TOGGLE) ✅ |
| 0x21 | MSG_COMMAND_ACK | N→G | Conferma comando ✅ |
| 0x22 | MSG_STATE | N→G | Stato relè cambiato |

---

## SCHEMA COLLEGAMENTI NODO 230V

```
┌─────────────────────────────────────────────────────────────┐
│                      QUADRO 503                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌─────────────┐                                          │
│   │  HLK-PM01   │  220V AC → 5V DC                         │
│   │             │                                          │
│   │  L ──────── │◄── 220V Fase                             │
│   │  N ──────── │◄── 220V Neutro                           │
│   │  +Vo ───────│──┬──► ESP32-C3 (5V)                      │
│   │  -Vo ───────│──┼──► ESP32-C3 (GND)                     │
│   └─────────────┘  │                                       │
│                    │                                        │
│   ┌─────────────┐  │                                       │
│   │ ESP32-C3    │  │                                       │
│   │             │  │                                       │
│   │  5V ────────│◄─┤                                       │
│   │  GND ───────│◄─┴──► Relè GND                           │
│   │  GPIO1 ─────│─────► Relè IN1                           │
│   │  GPIO2 ─────│─────► Relè IN2                           │
│   └─────────────┘                                          │
│                                                             │
│   ┌─────────────┐                                          │
│   │ RELÈ 2CH    │                                          │
│   │             │                                          │
│   │  VCC ───────│◄── 5V (da HLK-PM01)                      │
│   │  GND ───────│◄── GND                                   │
│   │  IN1 ───────│◄── GPIO1                                 │
│   │  IN2 ───────│◄── GPIO2                                 │
│   │             │                                          │
│   │  COM1 ──────│◄── 220V Fase (per carico 1)              │
│   │  NO1 ───────│──► Carico 1 (es. lampadina)              │
│   │  COM2 ──────│◄── 220V Fase (per carico 2)              │
│   │  NO2 ───────│──► Carico 2                              │
│   └─────────────┘                                          │
│                                                             │
│   Neutro 220V ─────────────────► Carichi (diretto)         │
│                                                             │
└─────────────────────────────────────────────────────────────┘

ATTENZIONE: Logica relè INVERTITA!
  - digitalWrite(GPIO, LOW)  = Relè ON  = Carico ACCESO
  - digitalWrite(GPIO, HIGH) = Relè OFF = Carico SPENTO
```

---

## NOTE IMPORTANTI

### Sicurezza 230V
- **MAI** lavorare con tensione presente
- Usare interruttore magnetotermico dedicato
- Rispettare sezioni cavi (min 1.5mm² per luci)
- Isolamento adeguato in scatola 503

### Problemi Noti e Soluzioni
1. **Flash fallisce su WT32-ETH01**
   - Flash Frequency: 40MHz (non 80!)
   - Flash Mode: DIO (non QIO!)
   - Jumper IO0↔GND solo durante flash

2. **Serial Monitor ESP32-C3 mostra garbage**
   - Abilitare "USB CDC On Boot: Enabled"

3. **Relè non scatta**
   - Verificare alimentazione 5V (non 3.3V!)
   - Controllare saldature GPIO

4. **ESP-NOW non funziona**
   - Stesso canale WiFi su tutti i dispositivi
   - MAC address corretto nel codice

5. **Node OTA fallisce con "No firmware uploaded"** (RISOLTO 30/12/2025)
   - Causa: RAM insufficiente per buffer firmware (~953KB > ~320KB RAM)
   - Soluzione: Usare LittleFS per salvare firmware su flash
   ```cpp
   File uploadFile = LittleFS.open("/node_fw.bin", "w");
   uploadFile.write(data, len);
   ```

6. **JSON parse error "Bad control character"** (RISOLTO 30/12/2025)
   - Causa: Array response non inizializzato, garbage nello stack
   - Soluzione Node: `uint8_t response[10] = {0};` (zero-init!)
   - Soluzione Gateway: Sanitizzare stringhe prima di JSON
   ```cpp
   for (int j = 0; j < 8; j++) {
       if (ver[j] != '\0' && (ver[j] < 32 || ver[j] > 126)) {
           ver[j] = '\0'; break;
       }
   }
   ```

7. **PlatformIO non compila con ESP-IDF 5.5**
   - Le callback ESP-NOW hanno signature diverse in ESP-IDF 5.5
   - Soluzione: Usare Arduino CLI con ESP32 Core 3.3.5

8. **ESP32-C3 non funziona senza USB al PC** (RISOLTO 30/12/2025)
   - Causa: `Serial.begin()` e `Serial.print()` bloccano il boot quando USB non è connesso
   - Sintomo: Node funziona con USB al PC, va offline con alimentazione esterna
   - Soluzione: Rimuovere TUTTO il codice Serial dal firmware Node
   ```cpp
   // NON USARE:
   // Serial.begin(115200);
   // Serial.println("...");
   ```
   - Versione fix: Node v1.5.0

---

## CRONOLOGIA VERSIONI

### Gateway
| Versione | Data | Note |
|----------|------|------|
| 1.0.0 | 29/12/2025 | ESP-NOW + WiFi base |
| 1.1.0 | 29/12/2025 | Node tracking + OTA Gateway |
| 1.2.0 | 30/12/2025 | Node OTA via ESP-NOW (LittleFS) |
| 1.2.1 | 30/12/2025 | Heartbeat veloce (3s) |
| 1.2.2 | 30/12/2025 | Fix JSON sanitization |
| 1.3.0 | 30/12/2025 | **Controllo Relè via Web UI + API /api/command** |

### Node
| Versione | Data | Note |
|----------|------|------|
| 1.0.0 | 29/12/2025 | ESP-NOW slave base |
| 1.1.0 | 30/12/2025 | OTA receiver |
| 1.2.0 | 30/12/2025 | Fix zero-init response array |
| 1.5.0 | 30/12/2025 | **FIX: Rimosso Serial per funzionare senza USB** |

---

## PIANO D'AZIONE - PROSSIMI PASSI

### ✅ FASE 1: Controllo Relè (COMPLETATA - 30/12/2025)
> Obiettivo: Controllare relè da Web UI

1. **Gateway - Aggiungere comando relè** ✅
   - ✅ Definire MSG_COMMAND (0x20) e MSG_COMMAND_ACK (0x21)
   - ✅ Implementare `/api/command` POST endpoint
   - ✅ Aggiungere pulsanti ON/OFF nella Web UI per ogni nodo
   - ✅ Inviare comando ESP-NOW al nodo specifico

2. **Node - Ricevere e eseguire comandi** ✅
   - ✅ Gestire MSG_COMMAND nel switch case
   - ✅ Controllare GPIO1/GPIO2 (logica invertita!)
   - ✅ Inviare MSG_COMMAND_ACK con stato attuale

3. **Gateway - Mostrare stato relè** ✅
   - ✅ Memorizzare stato relè per ogni nodo
   - ✅ Aggiornare Web UI con stato ON/OFF
   - ✅ Indicatore visivo (verde=ON, rosso=OFF)

### FASE 2: Pulsante Fisico (Priorità ALTA)
> Obiettivo: Toggle locale senza latenza

1. **Node - Input pulsante**
   - Configurare GPIO per pulsante (pull-up interno)
   - Implementare debounce software (50ms)
   - Toggle relè IMMEDIATO su pressione
   - Inviare stato al Gateway DOPO il toggle

2. **Node - Funzionamento offline**
   - Funziona anche senza Gateway
   - Salvare stato in NVS (ESP Preferences)
   - Ripristino stato dopo blackout

### FASE 3: Persistenza e Naming (Priorità MEDIA)
> Obiettivo: Nodi con nome e stato persistente

1. **Gateway - Gestione nodi**
   - Salvare lista nodi in LittleFS (`/nodes.json`)
   - Assegnare nome custom a ogni nodo
   - Ricordare ultimo stato visto

2. **Web UI - Configurazione nodi**
   - Form per rinominare nodo (es. "Luce Cucina")
   - Organizzazione per stanza
   - Icone per tipo dispositivo

### FASE 4: Cloud MQTT (Priorità MEDIA)
> Obiettivo: Controllo remoto da app

1. **Gateway - Client MQTT**
   - Connessione a broker (es. broker.hivemq.com o tuo server)
   - Topic: `omniapi/{gateway_id}/nodes/{node_mac}/state`
   - Topic: `omniapi/{gateway_id}/nodes/{node_mac}/command`
   - Publish stato ogni cambio + heartbeat periodico

2. **Gateway - Subscribe comandi**
   - Ricevere comandi da MQTT
   - Convertire in ESP-NOW e inviare al nodo
   - Confermare esecuzione su MQTT

### FASE 5: Sicurezza (Priorità BASSA per ora)
> Obiettivo: Proteggere comunicazioni

1. Password Web UI
2. Crittografia ESP-NOW (AES)
3. Whitelist MAC address
4. MQTT con TLS

---

## FILE PRINCIPALI

| File | Descrizione |
|------|-------------|
| `gateway_arduino_test/gateway_arduino_test.ino` | Firmware Gateway completo |
| `node_arduino_test/node_arduino_test.ino` | Firmware Node completo |
| `OmniaPi_HomeDomotic_Firmware/gateway/` | Versione PlatformIO (non usata) |
| `OmniaPi_HomeDomotic_Firmware/node/` | Versione PlatformIO (non usata) |

---

*Documento creato: 29/12/2025*
*Ultimo aggiornamento: 30/12/2025*
