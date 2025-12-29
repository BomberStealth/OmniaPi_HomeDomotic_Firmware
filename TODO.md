# OmniaPi Firmware - Piano di Sviluppo

> Questo file documenta il piano completo per lo sviluppo del firmware OmniaPi.
> Repository: `OmniaPi_HomeDomotic_Firmware`
> Data creazione: 29/12/2025

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
| Board | ESP32 Dev Module |
| Flash Frequency | 40MHz |
| Flash Mode | DIO |
| Upload Speed | 115200 |
| Ethernet | `ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN)` |
| IP testato | 192.168.1.209 |
| MAC testato | E8:9F:6D:BB:F8:FB |

### Nodi: ESP32-C3 SuperMini (TECNOIOT Amazon)
| Impostazione | Valore |
|--------------|--------|
| Board | ESP32C3 Dev Module |
| Flash Frequency | 40MHz |
| Flash Mode | DIO |
| USB CDC On Boot | **ENABLED** (importante!) |
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

### MILESTONE 1: Setup Repository e Struttura Base
- [ ] Creare repository GitHub `OmniaPi_HomeDomotic_Firmware`
- [ ] Inizializzare struttura cartelle
- [ ] Configurare PlatformIO per gateway (WT32-ETH01)
- [ ] Configurare PlatformIO per node (ESP32-C3)
- [ ] Creare README.md con overview progetto
- [ ] Creare shared/protocol/messages.h con strutture base

### MILESTONE 2: Gateway - Ethernet e Web Server
- [ ] Implementare connessione Ethernet (codice già testato)
- [ ] Creare web server base (AsyncWebServer)
- [ ] Implementare API REST: GET /api/status
- [ ] Implementare API REST: GET /api/nodes
- [ ] Implementare API REST: POST /api/command
- [ ] Creare pagina web UI base (HTML/CSS/JS in SPIFFS)
- [ ] Testare accesso da browser locale

### MILESTONE 3: Gateway - ESP-NOW Master
- [ ] Inizializzare ESP-NOW in modalità master
- [ ] Implementare broadcast discovery ("chi c'è?")
- [ ] Gestire registrazione nodi (MAC address → node ID)
- [ ] Implementare invio comandi a nodi specifici
- [ ] Implementare ricezione stati dai nodi
- [ ] Gestire heartbeat (ping/pong)
- [ ] Marcare nodi offline dopo timeout

### MILESTONE 4: Nodo - Base Funzionante
- [ ] Inizializzare ESP-NOW in modalità slave
- [ ] Rispondere a discovery del gateway
- [ ] Ricevere comandi (ON/OFF/TOGGLE)
- [ ] Controllare relè (logica invertita!)
- [ ] Inviare stato al gateway dopo ogni cambio
- [ ] Implementare heartbeat response

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
- [ ] Salvataggio lista nodi in gateway (SPIFFS)

### MILESTONE 8: Sicurezza Base
- [ ] Generare chiave AES unica per impianto
- [ ] Crittografare messaggi ESP-NOW
- [ ] Validare MAC address (whitelist)
- [ ] Proteggere Web UI con password

### MILESTONE 9: OTA Updates
- [ ] Gateway: download firmware da server/locale
- [ ] Gateway: self-update OTA
- [ ] Gateway: distribuzione firmware ai nodi
- [ ] Nodo: ricevere e applicare update OTA
- [ ] Rollback automatico se update fallisce

### MILESTONE 10: Watchdog e Recovery
- [ ] Implementare watchdog timer (reboot se bloccato)
- [ ] Salvare stato pre-crash
- [ ] Recovery automatico dopo reboot
- [ ] Logging errori per debug

### MILESTONE 11: Integrazione Backend
- [ ] Definire API compatibile con backend esistente
- [ ] Gateway espone endpoint per backend Node.js
- [ ] Backend rileva gateway OmniaPi vs Tasmota
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

## PROTOCOLLO ESP-NOW

### Struttura Messaggio Base
```cpp
struct OmniaPiMessage {
  uint8_t version;        // Versione protocollo (1)
  uint8_t type;           // Tipo messaggio (vedi sotto)
  uint8_t nodeId;         // ID nodo (0 = gateway, 1-254 = nodi)
  uint8_t sequence;       // Numero sequenza per ACK
  uint8_t payload[240];   // Dati (max 250 - header)
  uint8_t checksum;       // CRC8
};
```

### Tipi Messaggio
| Type | Nome | Direzione | Descrizione |
|------|------|-----------|-------------|
| 0x01 | DISCOVERY | G→N | Gateway cerca nodi |
| 0x02 | DISCOVERY_RESPONSE | N→G | Nodo risponde con info |
| 0x03 | REGISTER | G→N | Gateway assegna ID |
| 0x04 | REGISTER_ACK | N→G | Nodo conferma registrazione |
| 0x10 | COMMAND | G→N | Comando al nodo |
| 0x11 | COMMAND_ACK | N→G | Nodo conferma ricezione |
| 0x12 | STATE | N→G | Nodo invia stato |
| 0x20 | PING | G→N | Heartbeat request |
| 0x21 | PONG | N→G | Heartbeat response |
| 0x30 | OTA_START | G→N | Inizio update OTA |
| 0x31 | OTA_DATA | G→N | Chunk firmware |
| 0x32 | OTA_END | G→N | Fine update |
| 0x33 | OTA_ACK | N→G | ACK chunk ricevuto |

### Payload COMMAND
```cpp
struct CommandPayload {
  uint8_t channel;    // Canale relè (1, 2, ...)
  uint8_t action;     // 0=OFF, 1=ON, 2=TOGGLE
  uint8_t value;      // Per dimmer: 0-255
};
```

### Payload STATE
```cpp
struct StatePayload {
  uint8_t channelCount;     // Numero canali
  uint8_t states[8];        // Stato ogni canale (0=OFF, 1=ON)
  uint8_t values[8];        // Valore dimmer per canale
  int8_t rssi;              // Qualità segnale
  uint8_t errorFlags;       // Flag errori
};
```

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

---

## CRONOLOGIA VERSIONI

| Versione | Data | Note |
|----------|------|------|
| 0.0.1 | TBD | Setup iniziale |

---

*Documento creato: 29/12/2025*
*Ultimo aggiornamento: 29/12/2025*
