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
 *
 * Requisiti validati: 3.4, 3.5, 3.6, 3.7, 3.10, 3.11, 3.12, 3.13, 3.14, 5.1, 7.3, 7.4
 */
#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include "app/ProtocolAdapter.h"
#include "common/IIpcSender.h"

#include <string>
#include <functional>
#include <Poco/Mutex.h>
#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>

class SessionManager {
public:
    /// Stato corrente della colonnina, esposto alla UI.
    struct ChargePointStatus {
        std::string connectorState;
        int currentMeterValue;
        bool isCharging;
        int transactionId;
        std::string idTag;
        bool centralSystemConnected;
        std::string lastError;
    };

    /**
     * Costruttore.
     * @param protocol  Riferimento al ProtocolAdapter (es. OcppClient16J)
     * @param ipc       Riferimento all'IpcClient per comunicare col Firmware_Layer
     */
    SessionManager(ProtocolAdapter& protocol, IIpcSender& ipc);

    // --- Gestione eventi firmware ---

    /// Chiamato quando il Firmware_Layer notifica un cambio stato del connettore.
    void onConnectorStateChanged(const std::string& newState);

    /// Chiamato quando il Firmware_Layer invia un nuovo Meter_Value (Wh).
    void onMeterValue(int meterValueWh);

    /// Chiamato quando il Firmware_Layer notifica un errore hardware.
    void onError(const std::string& errorType, const std::string& errorCode);

    /// Chiamato quando il Firmware_Layer notifica la risoluzione di un errore.
    void onErrorCleared();

    // --- Gestione comandi remoti dal Central_System ---

    /**
     * Chiamato quando il Central_System invia un CALL (RemoteStartTransaction,
     * RemoteStopTransaction) tramite il ProtocolAdapter.
     */
    void onRemoteCommand(const std::string& action,
                         const Poco::JSON::Object& payload,
                         const std::string& uniqueId);

    // --- Gestione risposte OCPP ---

    /**
     * Chiamato quando il Central_System risponde a un CALL inviato
     * (Authorize.conf, StartTransaction.conf, ecc.).
     */
    void onProtocolResponse(const Poco::JSON::Object& response);

    // --- Comandi dall'interfaccia web ---

    void requestPlugIn();
    void requestPlugOut();
    void requestStartCharge(const std::string& idTag);
    void requestStopCharge();
    void requestTriggerError(const std::string& errorType);
    void requestClearError();

    // --- Stato e callback UI ---

    ChargePointStatus getStatus() const;

    using StatusCallback = std::function<void(const ChargePointStatus&)>;
    void setStatusCallback(StatusCallback cb);

private:
    ProtocolAdapter& _protocol;
    IIpcSender& _ipc;

    ChargePointStatus _status;
    StatusCallback _statusCallback;
    mutable Poco::Mutex _mutex;

    // Stato interno per la sequenza Authorize → StartTransaction
    bool _awaitingAuthorize;
    std::string _pendingIdTag;
    int _pendingMeterStart;

    /// Invia un comando IPC al Firmware_Layer.
    void sendIpcCommand(const std::string& action,
                        const std::string& errorType = "");

    /// Notifica la UI del cambio stato.
    void notifyStatusUpdate();

    /// Mappa lo stato del connettore all'errorCode OCPP per StatusNotification.
    std::string mapErrorCode(const std::string& connectorState) const;
};

#endif // SESSIONMANAGER_H
