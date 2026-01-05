# OmniaPi Home Domotic - TODO

**Ultimo aggiornamento:** 5 Gennaio 2025

---

## ✅ COMPLETATE

### Firmware Gateway (WT32-ETH01)
- [x] Migrazione a ESP-IDF puro (da Arduino)
- [x] Ethernet LAN8720 + WiFi dual-stack con failover
- [x] ESP-NOW master
- [x] MQTT comunicazione con Backend
- [x] Web UI locale (porta 80)
- [x] OTA firmware via HTTP
- [x] OTA Node via ESP-NOW
- [x] NVS per credenziali WiFi
- [x] Endpoint POST /api/wifi/credentials

### Firmware Node (ESP32-C3)
- [x] ESP-NOW slave
- [x] Controllo 2 relay
- [x] LED status patterns
- [x] Boot senza USB

### Backend (Express.js)
- [x] API REST completa (Auth, Impianti, Stanze, Dispositivi, Scene)
- [x] CRUD Dispositivi OmniaPi (registra, rinomina, assegna stanza, elimina)
- [x] Cascade delete - eliminazione dispositivo aggiorna scene automaticamente ✅ VERIFICATO
- [x] Auto-discovery nodi ESP-NOW ✅ VERIFICATO
- [x] Scene con scheduling (time-based, sun-based)
- [x] Handler MQTT per topic OmniaPi
- [x] WebSocket events real-time

### Frontend (React)
- [x] Pagine: Auth, Dashboard, Impianti, Stanze, Dispositivi, Scene, Impostazioni
- [x] Wizard "Aggiungi Dispositivo" con auto-discovery ✅ VERIFICATO
- [x] Rinomina dispositivo
- [x] Assegna dispositivo a stanza
- [x] Elimina dispositivo (cascade delete automatico)
- [x] UI Modifica Scene (aggiungere/rimuovere dispositivi)
- [x] Sezione Impostazioni (Profilo, DispositiviConnessi, Guida, InfoApp)
- [x] Aggiornamento real-time via WebSocket

---

## ❌ DA FARE

### Priorità Media - Firmware Node

#### 1. Pulsante Fisico
- [ ] GPIO pulsante con debounce
- [ ] Toggle relay immediato (funziona offline)
- [ ] Long-press per reset (opzionale)

#### 2. Persistenza Stato Relay (NVS)
- [ ] Salvare stato relay in NVS quando cambia
- [ ] Ripristinare stato relay al boot dopo blackout
- [ ] ⚠️ DA TESTARE A CASA

### Priorità Bassa - Sicurezza

#### 3. Sicurezza Web UI Gateway
- [ ] Password per accesso Web UI locale
- [ ] Salvataggio password in NVS

#### 4. Sicurezza ESP-NOW (opzionale)
- [ ] MAC Whitelist
- [ ] Crittografia ESP-NOW

---

## 🔮 FUTURO (Nice to Have)

- [ ] OmniaPi Switch (PCB dedicato)
- [ ] OmniaPi Dimmer (PWM per LED)
- [ ] OmniaPi Sensor (temp/umidità/lux)
- [ ] OmniaPi Shutter (tapparelle)
- [ ] Matter/Thread bridge
- [ ] App Mobile nativa

---

## 📋 Hardware Confermato

| Componente | Modello |
|------------|---------|
| Gateway | WT32-ETH01 |
| Node | ESP32-C3 SuperMini |
| Alimentazione | HLK-PM01 (220V→5V) |
| Relè | GTIWUNG 2ch 5V |

---

## 📝 Note

- **Versione Frontend:** v1.2.0
- **Versione Firmware:** 1.5.0-idf (da aggiornare a 1.6.0)
