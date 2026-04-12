# MiniChargePoint

Simulatore di colonnina di ricarica per veicoli elettrici in C++. Comunica con un Central System (SteVe 3.11.0) tramite protocollo OCPP 1.6J su WebSocket e offre un'interfaccia web per il monitoraggio e il controllo manuale.

## Architettura

Il sistema è composto da due processi distinti che comunicano via Unix Domain Socket (IPC):

```
┌─────────────────────┐       IPC (JSON/newline)       ┌──────────────────────────┐
│  firmware_simulator  │◄──────────────────────────────►│    charge_point_app      │
│                      │    Unix Domain Socket          │                          │
│  ConnectorSimulator  │                                │  IpcClient               │
│  MeterGenerator      │                                │  SessionManager          │
│  ErrorSimulator      │                                │  OcppClient16J ──► SteVe │
│  IpcServer           │                                │  WebServer ──► Browser   │
└─────────────────────┘                                └──────────────────────────┘
```

- **firmware_simulator**: simula l'hardware della colonnina (connettore, meter energetico, errori)
- **charge_point_app**: gestisce la comunicazione OCPP 1.6J, la logica delle sessioni e l'interfaccia web

## Messaggi OCPP 1.6J Implementati

### Colonnina → Central System (CALL)

| Messaggio | Descrizione |
|-----------|-------------|
| BootNotification | Registrazione all'avvio e dopo ogni riconnessione |
| Heartbeat | Invio periodico (intervallo da BootNotification.conf) |
| Authorize | Verifica idTag prima di avviare una ricarica |
| StatusNotification | Notifica cambio stato del connettore |
| StartTransaction | Avvio sessione di ricarica |
| MeterValues | Valori energetici durante la ricarica (ogni 10s) |
| StopTransaction | Fine sessione (Local, Remote, EmergencyStop) |

### Central System → Colonnina (CALL)

| Messaggio | Descrizione |
|-----------|-------------|
| RemoteStartTransaction | Avvio ricarica da remoto (plug_in → authorize → start_charge) |
| RemoteStopTransaction | Arresto ricarica da remoto (stop_charge → plug_out) |

## Dipendenze

- **Poco C++ Libraries** 1.15.0 (PocoFoundation, PocoNet, PocoUtil, PocoJSON)
- **Debian 12** (testato)
- **g++** con supporto C++17

## Compilazione

```bash
cd impl
make            # compila firmware_simulator e charge_point_app
make test       # compila ed esegue tutti gli unit test
make clean      # rimuove artefatti di compilazione
```

Gli eseguibili vengono generati in `build/`:
- `build/firmware_simulator`
- `build/charge_point_app`

## Configurazione

Il file `config.json` nella directory di lavoro configura entrambi i processi:

```json
{
    "centralSystemUrl": "ws://localhost:8180/steve/websocket/CentralSystemService",
    "chargePointId": "MiniCP001",
    "socketPath": "/tmp/minichargepoint.sock",
    "httpPort": 8080,
    "meterInterval": 10,
    "defaultIdTag": "TESTIDTAG1",
    "logLevel": "information",
    "logFile": "minichargepoint.log"
}
```

| Campo | Descrizione | Default |
|-------|-------------|---------|
| centralSystemUrl | URL WebSocket del Central System | ws://localhost:8180/steve/websocket/CentralSystemService |
| chargePointId | Identificativo della colonnina (deve essere registrato in SteVe) | MiniCP001 |
| socketPath | Percorso Unix Domain Socket per IPC | /tmp/minichargepoint.sock |
| httpPort | Porta del server web | 8080 |
| meterInterval | Intervallo generazione meter values (secondi) | 10 |
| defaultIdTag | idTag di default per le ricariche | TESTIDTAG1 |
| logLevel | Livello di log (error, warning, information, debug) | information |
| logFile | File di log | minichargepoint.log |

Se il file non esiste, vengono usati i valori di default. Se contiene valori non validi, il processo termina con errore.

## Esecuzione

### 1. Avviare il firmware simulator

```bash
cd impl
build/firmware_simulator
```

Il firmware crea il Unix Domain Socket e attende la connessione dell'application layer.

### 2. Avviare l'application layer (in un altro terminale)

```bash
cd impl
build/charge_point_app
```

L'app si connette al firmware via IPC, al Central System via WebSocket e avvia il server web.

### 3. Aprire l'interfaccia web

Navigare a `http://<ip>:8080/` nel browser.

### Prerequisiti SteVe

- Il chargePointId (es. `MiniCP001`) deve essere registrato in SteVe: `http://<host>:8180/steve/manager/chargepoints/add`
- L'idTag (es. `TESTIDTAG1`) deve essere registrato: `http://<host>:8180/steve/manager/ocppTags/add`

## Sessione di Ricarica Tipica

1. **Plug In** → connettore passa a Preparing, StatusNotification inviata
2. **Start Charge** (con idTag) → Authorize → se accettato: StartTransaction, connettore passa a Charging
3. Durante la ricarica: MeterValues inviati ogni 10 secondi
4. **Stop Charge** → connettore passa a Finishing, StopTransaction inviata
5. **Plug Out** → connettore torna ad Available, meter azzerato

## Simulazione Errori

- **HW Fault** / **Tamper**: porta il connettore a Faulted, invia StatusNotification con errorCode. Se una ricarica è attiva, invia StopTransaction con reason "EmergencyStop"
- **Clear Error**: ripristina il connettore ad Available

## Comandi Remoti da SteVe

- **RemoteStartTransaction**: se il connettore è Available, avvia automaticamente plug_in → authorize → start_charge
- **RemoteStopTransaction**: se il transactionId corrisponde, esegue stop_charge → plug_out

## Struttura del Progetto

```
impl/
├── Makefile
├── README.md
├── config.json
├── include/
│   ├── firmware/          # Header Firmware_Layer
│   ├── app/               # Header Application_Layer
│   └── common/            # Header condivisi (IPC, interfacce)
├── src/
│   ├── firmware/          # Sorgenti Firmware_Layer + main
│   ├── app/               # Sorgenti Application_Layer + main
│   └── common/            # Sorgenti condivisi
├── web/                   # Interfaccia web (HTML/CSS/JS puro)
└── test/                  # Unit test
```

## Links
* [SteVe](https://github.com/steve-community/steve)
* [Poco C++](https://pocoproject.org/)