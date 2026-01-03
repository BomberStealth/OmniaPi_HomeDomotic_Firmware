# OmniaPi Home Domotic - TODO

## ✅ COMPLETATE

### Firmware
- [x] Node ESP-IDF (boot senza USB)
- [x] Gateway ESP-IDF (migrazione da Arduino)
- [x] Comunicazione ESP-NOW Gateway ↔ Node
- [x] MQTT Gateway ↔ Backend
- [x] Web UI Gateway locale (controllo relay, stato nodi)
- [x] Gateway OTA via HTTP
- [x] Node OTA via ESP-NOW

### Backend
- [x] API REST OmniaPi (/api/omniapi/gateway, /api/omniapi/nodes, /api/omniapi/command)
- [x] Handler MQTT per topic OmniaPi
- [x] WebSocket events per aggiornamenti real-time

### Frontend
- [x] Pagina /omniapi con stato Gateway e controllo Nodi
- [x] Toggle relay funzionanti
- [x] Aggiornamento real-time via WebSocket

---

## 🔧 DA FARE - PRIORITÀ ALTA

### 1. Integrazione Dispositivi ESP-NOW nel sistema principale
- [ ] **BE**: Salvare nodi ESP-NOW nel database (come dispositivi)
- [ ] **BE**: API per rinominare nodo (nome friendly)
- [ ] **BE**: API per assegnare nodo a stanza
- [ ] **BE**: API per eliminare nodo (con cascade delete da scene/stanze)
- [ ] **FE**: Mostrare nodi ESP-NOW in sezione "Dispositivi"
- [ ] **FE**: Wizard "Aggiungi Dispositivo" per nodi ESP-NOW
- [ ] **FE**: Rinomina dispositivo
- [ ] **FE**: Sposta dispositivo tra stanze

### 2. Fix Scene
- [ ] **BE**: Cascade delete - rimuovere dispositivo da scene quando eliminato
- [ ] **BE**: Cascade delete - rimuovere dispositivo da stanze quando eliminato
- [ ] **BE**: API per modificare scene (aggiungere/rimuovere dispositivi)
- [ ] **FE**: UI per modificare scene esistenti
- [ ] **FE**: Aggiornare conteggio dispositivi in tempo reale

### 3. Sezione Impostazioni
- [ ] **FE**: Dispositivi Connessi - lista sessioni attive utente
- [ ] **FE**: Notifiche - configurazione push notifications
- [ ] **FE**: Guida - pagina help/tutorial
- [ ] **FE**: Informazioni - versione app, credits, contatti

---

## 🔧 DA FARE - PRIORITÀ MEDIA

### 4. Firmware - Pulsante Fisico e LED
- [ ] GPIO pulsante con debounce
- [ ] Toggle relay immediato (funzionamento offline)
- [ ] LED stato (lampeggio patterns)
- [ ] Salvataggio stato relay in NVS

### 5. Auto-Discovery e Pairing
- [ ] Gateway scan automatico nuovi nodi
- [ ] Workflow pairing (conferma da app)
- [ ] Assegnazione ID univoco

### 6. Sicurezza
- [ ] Password Web UI Gateway
- [ ] Whitelist MAC addresses
- [ ] Crittografia ESP-NOW (opzionale)

---

## 🔧 DA FARE - PRIORITÀ BASSA

### 7. Watchdog e Recovery
- [ ] Reboot automatico se bloccato
- [ ] Logging errori persistente
- [ ] Heartbeat monitoring

### 8. Tipi Dispositivo Aggiuntivi
- [ ] Dimmer (PWM)
- [ ] Sensore PIR
- [ ] Sensore temperatura/umidità
- [ ] Tapparelle

### 9. Wizard Installatore
- [ ] Setup iniziale guidato
- [ ] Configurazione WiFi
- [ ] Pairing dispositivi step-by-step
- [ ] QR code per pairing rapido
