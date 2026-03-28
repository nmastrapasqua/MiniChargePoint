/**
 * SessionManager — Implementazione della coordinazione tra firmware, OCPP e web UI.
 *
 * Flusso principale:
 *   1. Eventi firmware (IPC) → onConnectorStateChanged / onMeterValue / onError / onErrorCleared
 *   2. SessionManager traduce in chiamate ProtocolAdapter (OCPP)
 *   3. Risposte OCPP → onProtocolResponse aggiorna stato interno
 *   4. Comandi web UI → requestXxx → comandi IPC al firmware
 *   5. Comandi remoti Central_System → onRemoteCommand → comandi IPC al firmware
 *
 * Requisiti validati: 3.4, 3.5, 3.6, 3.7, 3.10, 3.11, 3.12, 3.13, 3.14, 5.1, 7.3, 7.4
 */
#include "app/SessionManager.h"
#include "common/IpcMessage.h"

#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>

#include <sstream>

using Poco::Logger;

// ---------------------------------------------------------------------------
// Costruttore
// ---------------------------------------------------------------------------

SessionManager::SessionManager(ProtocolAdapter& protocol, IIpcSender& ipc)
    : _protocol(protocol)
    , _ipc(ipc)
    , _awaitingAuthorize(false)
    , _pendingMeterStart(0)
{
    _status.connectorState = "Available";
    _status.currentMeterValue = 0;
    _status.isCharging = false;
    _status.transactionId = -1;
    _status.centralSystemConnected = false;
    _status.firmwareConnected = false;
    _status.lastError = "";
}

// ---------------------------------------------------------------------------
// Eventi firmware
// ---------------------------------------------------------------------------

void SessionManager::onConnectorStateChanged(const std::string& newState)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Connector state changed to: %s", newState);

    bool wasCharging = false;
    int txId = -1;
    int meterStop = 0;

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        wasCharging = _status.isCharging;
        txId = _status.transactionId;
        meterStop = _status.currentMeterValue;

        _status.connectorState = newState;

        if (newState == "Charging") {
            _status.isCharging = true;
        } else if (newState == "Available") {
            _status.isCharging = false;
            _status.currentMeterValue = 0;
            _status.transactionId = -1;
            _status.idTag = "";
            _status.lastError = "";
        } else if (newState == "Faulted") {
            _status.isCharging = false;
        } else if (newState == "Finishing") {
            _status.isCharging = false;
        }
    }

    // Invia StatusNotification al Central_System
    std::string errorCode = mapErrorCode(newState);
    _protocol.sendStatusNotification(1, newState, errorCode);

    // Se era in ricarica e il connettore è andato in Faulted → StopTransaction con EmergencyStop
    if (wasCharging && newState == "Faulted" && txId >= 0) {
        logger.warning("Fault during active session (txId=%d) — sending StopTransaction with EmergencyStop", txId);
        _protocol.sendStopTransaction(txId, meterStop, "EmergencyStop");
    }

    // Se era in ricarica e il connettore è in Finishing → StopTransaction normale
    if (wasCharging && newState == "Finishing" && txId >= 0) {
        logger.information("Charging session ending (txId=%d), sending StopTransaction", txId);
        _protocol.sendStopTransaction(txId, meterStop, "Local");
    }

    notifyStatusUpdate();
}

void SessionManager::onMeterValue(int meterValueWh)
{
    Logger& logger = Logger::get("SessionManager");

    int txId = -1;
    bool charging = false;

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        _status.currentMeterValue = meterValueWh;
        txId = _status.transactionId;
        charging = _status.isCharging;
    }

    // Inoltra MeterValues al Central_System solo durante sessione attiva (Proprietà 5)
    if (charging && txId >= 0) {
        logger.debug("Forwarding MeterValue %d Wh (txId=%d)", meterValueWh, txId);
        _protocol.sendMeterValues(1, txId, meterValueWh);
    }

    notifyStatusUpdate();
}

void SessionManager::onError(const std::string& errorType, const std::string& errorCode)
{
    Logger& logger = Logger::get("SessionManager");
    logger.warning("Hardware error reported: %s (OCPP code: %s)", errorType, errorCode);

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        _status.lastError = errorType;
    }

    // Il cambio stato a Faulted arriverà tramite onConnectorStateChanged
    notifyStatusUpdate();
}

void SessionManager::onErrorCleared()
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Hardware error cleared");

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        _status.lastError = "";
    }

    // Il cambio stato ad Available arriverà tramite onConnectorStateChanged
    notifyStatusUpdate();
}

void SessionManager::onCentralSystemConnectionChanged(bool connected)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Central_System connection status: %s",
                       std::string(connected ? "connected" : "disconnected"));

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        _status.centralSystemConnected = connected;
    }
    notifyStatusUpdate();
}

void SessionManager::onFirmwareConnectionChanged(bool connected)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Firmware IPC connection status: %s",
                       std::string(connected ? "connected" : "disconnected"));

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        _status.firmwareConnected = connected;
    }
    notifyStatusUpdate();
}

// ---------------------------------------------------------------------------
// Comandi remoti dal Central_System (Proprietà 13)
// ---------------------------------------------------------------------------

void SessionManager::onRemoteCommand(const std::string& action,
                                     const Poco::JSON::Object& payload,
                                     const std::string& uniqueId)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Remote command received: %s", action);

    if (action == "RemoteStartTransaction") {
        std::string connState;
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            connState = _status.connectorState;
        }

        Poco::JSON::Object response;

        if (connState == "Available") {
            // Accettare: rispondere Accepted, poi avviare sequenza
            response.set("status", "Accepted");
            _protocol.sendCallResult(uniqueId, response);

            // Estrarre idTag dal payload
            std::string idTag = "UNKNOWN";
            if (payload.has("idTag")) {
                idTag = payload.getValue<std::string>("idTag");
            }

            // Sequenza: plug_in → authorize → start_charge
            sendIpcCommand("plug_in");

            // Avviare la sequenza Authorize direttamente (il connettore passerà
            // a Preparing in modo asincrono, ma per il flusso remoto non serve
            // attendere — il check stato è già stato fatto sopra)
            int meterStart;
            {
                Poco::Mutex::ScopedLock lock(_mutex);
                meterStart = _status.currentMeterValue;
            }
            _awaitingAuthorize = true;
            _pendingIdTag = idTag;
            _pendingMeterStart = meterStart;
            _protocol.sendAuthorize(idTag);
        } else {
            // Connettore non disponibile → Rejected (Requisito 3.14)
            logger.warning("RemoteStartTransaction rejected: connector is %s", connState);
            response.set("status", "Rejected");
            _protocol.sendCallResult(uniqueId, response);
        }

    } else if (action == "RemoteStopTransaction") {
        int requestedTxId = -1;
        if (payload.has("transactionId")) {
            requestedTxId = payload.getValue<int>("transactionId");
        }

        int currentTxId;
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            currentTxId = _status.transactionId;
        }

        Poco::JSON::Object response;

        if (requestedTxId >= 0 && requestedTxId == currentTxId) {
            // TransactionId corrisponde → Accepted
            response.set("status", "Accepted");
            _protocol.sendCallResult(uniqueId, response);

            // Arrestare la ricarica: stop_charge → plug_out
            sendIpcCommand("stop_charge");
            sendIpcCommand("plug_out");
        } else {
            // TransactionId non corrisponde → Rejected (Requisito 3.13)
            logger.warning("RemoteStopTransaction rejected: requested txId=%d, current txId=%d",
                           requestedTxId, currentTxId);
            response.set("status", "Rejected");
            _protocol.sendCallResult(uniqueId, response);
        }

    } else {
        logger.warning("Unknown remote command: %s", action);
    }
}

// ---------------------------------------------------------------------------
// Risposte OCPP (Authorize.conf, StartTransaction.conf, ecc.)
// ---------------------------------------------------------------------------

void SessionManager::onProtocolResponse(const Poco::JSON::Object& response)
{
    Logger& logger = Logger::get("SessionManager");

    // Gestione BootNotification.conf — aggiorna stato connessione Central_System
    std::string action = response.optValue<std::string>("action", "");
    if (action == "BootNotification") {
        std::string bootStatus = "";
        if (response.has("status")) {
            bootStatus = response.getValue<std::string>("status");
        }
        bool accepted = (bootStatus == "Accepted");
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            _status.centralSystemConnected = accepted;
        }
        if (accepted) {
            logger.information("Central_System connection confirmed (BootNotification Accepted)");
        } else {
            logger.warning("BootNotification not accepted: %s", bootStatus);
        }
        notifyStatusUpdate();
        return;
    }

    // Gestione Authorize.conf (Proprietà 12)
    if (response.has("idTagInfo") && _awaitingAuthorize) {
        Poco::JSON::Object::Ptr idTagInfo = response.getObject("idTagInfo");
        std::string authStatus = idTagInfo->getValue<std::string>("status");

        if (authStatus == "Accepted") {
            logger.information("Authorize accepted for idTag: %s", _pendingIdTag);

            // Procedere con StartTransaction
            sendIpcCommand("start_charge");
            _protocol.sendStartTransaction(1, _pendingIdTag, _pendingMeterStart);

            {
                Poco::Mutex::ScopedLock lock(_mutex);
                _status.idTag = _pendingIdTag;
            }
        } else {
            // Authorize rifiutato → non avviare la ricarica (Requisito 3.11)
            logger.warning("Authorize rejected for idTag: %s (status: %s)", _pendingIdTag, authStatus);
        }

        _awaitingAuthorize = false;
        _pendingIdTag = "";
        notifyStatusUpdate();
        return;
    }

    // Gestione StartTransaction.conf
    if (response.has("transactionId")) {
        int txId = response.getValue<int>("transactionId");
        logger.information("StartTransaction confirmed, transactionId=%d", txId);

        {
            Poco::Mutex::ScopedLock lock(_mutex);
            _status.transactionId = txId;
        }
        notifyStatusUpdate();
        return;
    }
}

// ---------------------------------------------------------------------------
// Comandi dall'interfaccia web
// ---------------------------------------------------------------------------

void SessionManager::requestPlugIn()
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Web UI: plug_in requested");
    sendIpcCommand("plug_in");
}

void SessionManager::requestPlugOut()
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Web UI: plug_out requested");
    sendIpcCommand("plug_out");
}

void SessionManager::requestStartCharge(const std::string& idTag)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Start charge requested with idTag: %s", idTag);

    // Verificare che il connettore sia in stato Preparing prima di procedere
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (_status.connectorState != "Preparing") {
            logger.warning("Cannot start charge: connector is %s (expected Preparing)",
                           _status.connectorState);
            return;
        }
    }

    // Proprietà 12: Authorize prima di StartTransaction
    int meterStart;
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        meterStart = _status.currentMeterValue;
    }

    _awaitingAuthorize = true;
    _pendingIdTag = idTag;
    _pendingMeterStart = meterStart;

    _protocol.sendAuthorize(idTag);
}

void SessionManager::requestStopCharge()
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Web UI: stop_charge requested");
    sendIpcCommand("stop_charge");
}

void SessionManager::requestTriggerError(const std::string& errorType)
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Web UI: trigger_error requested (%s)", errorType);
    sendIpcCommand("trigger_error", errorType);
}

void SessionManager::requestClearError()
{
    Logger& logger = Logger::get("SessionManager");
    logger.information("Web UI: clear_error requested");
    sendIpcCommand("clear_error");
}

// ---------------------------------------------------------------------------
// Stato e callback UI
// ---------------------------------------------------------------------------

SessionManager::ChargePointStatus SessionManager::getStatus() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return _status;
}

void SessionManager::setStatusCallback(StatusCallback cb)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _statusCallback = cb;
}

// ---------------------------------------------------------------------------
// Metodi privati
// ---------------------------------------------------------------------------

void SessionManager::sendIpcCommand(const std::string& action,
                                    const std::string& errorType)
{
    Poco::JSON::Object cmd;
    cmd.set("type", IpcMessage::TYPE_COMMAND);
    cmd.set("action", action);
    if (!errorType.empty()) {
        cmd.set("errorType", errorType);
    }
    _ipc.sendMessage(cmd);
}

void SessionManager::notifyStatusUpdate()
{
    StatusCallback cb;
    ChargePointStatus currentStatus;
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        cb = _statusCallback;
        currentStatus = _status;
    }
    if (cb) {
        cb(currentStatus);
    }
}

std::string SessionManager::mapErrorCode(const std::string& connectorState) const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (connectorState == "Faulted" && !_status.lastError.empty()) {
        if (_status.lastError == "HardwareFault") return "InternalError";
        if (_status.lastError == "TamperDetection") return "OtherError";
    }
    return "NoError";
}
