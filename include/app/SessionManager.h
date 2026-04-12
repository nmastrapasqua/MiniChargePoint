/**
 * SessionManager — Coordina eventi firmware, protocollo OCPP e interfaccia web.
 *
 * Responsabilità:
 *   - Tradurre eventi firmware (cambio stato connettore, meter values, errori)
 *     in messaggi OCPP verso il Central_System
 *   - Gestire la sequenza Authorize → StartTransaction per avvio ricarica
 *   - Gestire comandi remoti (RemoteStartTransaction, RemoteStopTransaction)
 *   - Inoltrare comandi dall'interfaccia web al Firmware_Layer via IPC
 *   - Mantenere lo stato corrente (ChargePointStatus) e notificare la UI
 *     tramite _uiQueue (JSON serializzato)
 *
 * Gli eventi vengono accodati in _eventQueue e processati da un thread
 * dedicato, garantendo serializzazione e disaccoppiamento dai chiamanti.
 * OcppClient16J e IpcClient pushano direttamente nella _eventQueue.
 *
 */
#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include "common/ThreadSafeQueue.h"
#include "common/SessionEvent.h"
#include "common/CentralSystemEvent.h"

#include <string>
#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>
#include <Poco/Thread.h>
#include <Poco/Runnable.h>


class SessionManager: public Poco::Runnable {
public:
    /// Stato corrente della colonnina, esposto alla UI.
    struct ChargePointStatus {
        std::string connectorState;
        int currentMeterValue;
        bool isCharging;
        int transactionId;
        std::string idTag;
        bool centralSystemConnected;
        bool firmwareConnected;
        std::string lastError;
    };

    SessionManager(ThreadSafeQueue<SessionEvent>* eventQ,
			ThreadSafeQueue<std::string>* uiQ,
			ThreadSafeQueue<std::string>* ipcQ,
			ThreadSafeQueue<CentralSystemEvent>* csysQueue);

    ~SessionManager();

    // --- Avvio/arresto del thread di processing ---
    void start();
    void stop();

    void run() override;


private:
    ChargePointStatus _status;

    std::atomic<bool> _running{false};

    // Accesso solo dal thread di processing
    bool _awaitingAuthorize;
    std::string _pendingIdTag;
    int _pendingMeterStart;

    // --- Coda eventi e thread dedicato ---
    ThreadSafeQueue<SessionEvent>* _eventQueue = nullptr;

    // --- Coda UI (puntatore, impostato da main_app) ---
    ThreadSafeQueue<std::string>* _uiQueue = nullptr;

    // --- Coda ipc (puntatore, impostato da main_app) ---
    ThreadSafeQueue<std::string>* _ipcQueue = nullptr;

    // --- Coda per comunicazione con Central System
    ThreadSafeQueue<CentralSystemEvent>* _csysQueue = nullptr;

    Poco::Thread _eventThread;

    Poco::Logger& _logger = Poco::Logger::get("SessionManager");

    // --- Messaggi ricevuti da ipc ---
    void handleConnectorStateChanged(const std::string& newState, const std::string& errorType);
    void handleMeterValue(int meterValueWh);
    void handleError(const std::string& errorType);
    void handleErrorCleared();
    void handleFirmwareConnectionChanged(bool connected);

    // Messaggi ricevuti da OcppClient
    void handleProtocolResponse(const Poco::JSON::Object& response);
    void handleRemoteCommand(const std::string& action,
                                 const Poco::JSON::Object& payload,
                                 const std::string& uniqueId);
    void handleCentralSystemConnectionChanged(bool connected);

    // Messaggi ricevuti dall'interfaccia web
    void handleRequestPlugIn();
    void handleRequestPlugOut();
    void handleRequestStartCharge(const std::string& idTag);
    void handleRequestStopCharge();
    void handleRequestTriggerError(const std::string& errorType);
    void handleRequestClearError();

    // Invia aggiornamento all'interfaccia web
    void notifyStatusUpdate();

    // Invia comandi aipc
    void sendIpcCommand(const std::string& action,
                        const std::string& errorType = "");

    std::string serializeStatus(const ChargePointStatus& status) const;
    std::string mapErrorCode(const std::string& connectorState) const;
};

#endif // SESSIONMANAGER_H
