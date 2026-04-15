/**
 * SessionManager — Implementazione della coordinazione tra firmware, OCPP e web UI.
 *
 * Gli eventi vengono accodati in una ThreadSafeQueue e processati da un thread
 * dedicato (run).
 *
 * Notifiche UI: notifyStatusUpdate serializza lo stato in JSON e lo pusha
 * nella _uiQueue.
 *
 */
#include "app/SessionManager.h"
#include "common/IpcMessage.h"

#include <Poco/JSON/Object.h>
#include <Poco/Timestamp.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>

#include <sstream>


// ---------------------------------------------------------------------------
// Costruttore / Distruttore
// ---------------------------------------------------------------------------

SessionManager::SessionManager(ThreadSafeQueue<SessionEvent>* eventQ,
		ThreadSafeQueue<std::string>* uiQ,
		ThreadSafeQueue<std::string>* ipcQ,
		ThreadSafeQueue<CentralSystemEvent>* csysQueue,
		int remoteDelayMs)
    : _awaitingAuthorize(false)
	, _pendingIdTag("")
    , _pendingMeterStart(0)
    , _pendingRemoteStart(false)
    , _pendingRemoteStop(false)
    , _remoteDelayMs(remoteDelayMs)
	, _eventQueue(eventQ)
	, _uiQueue(uiQ)
	, _ipcQueue(ipcQ)
	, _csysQueue(csysQueue)
	, _logger(Poco::Logger::get("SessionManager"))
{
    _status.connectorState = "Available";
    _status.currentMeterValue = 0;
    _status.isCharging = false;
    _status.transactionId = -1;
    _status.idTag = "";
    _status.centralSystemConnected = false;
    _status.firmwareConnected = false;
    _status.lastError = "";
    _status.displayMessage = "Mini Charge Point";

    if (!_eventQueue) {
    	throw std::invalid_argument("eventQueue non può essere nullptr");
    }

    if (!_uiQueue) {
    	throw std::invalid_argument("uiQueue non può essere nullptr");
    }

    if (!_ipcQueue) {
    	throw std::invalid_argument("ipcQueue non può essere nullptr");
    }

    if (!_csysQueue) {
        	throw std::invalid_argument("csysQueue non può essere nullptr");
    }
}

SessionManager::~SessionManager()
{
    stop();
}

// ---------------------------------------------------------------------------
// Avvio / arresto thread di processing
// ---------------------------------------------------------------------------

void SessionManager::start()
{
	if (_running) return;
	_running = true;
    _eventThread.start(*this);
}

void SessionManager::stop()
{
	 if (!_running) return;

	 _running = false;

	 try { _eventThread.join(); } catch (...) {}

}

// ---------------------------------------------------------------------------
// Event loop (thread dedicato)
// ---------------------------------------------------------------------------

void SessionManager::run()
{
    while (_running) {
        auto evt = _eventQueue->pop();  // blocking
        if (!evt.has_value()) break;   // queue closed

        switch (evt->type) {
            case SessionEvent::Type::ConnectorStateChanged:
            	handleConnectorStateChanged(evt->stringParam, evt->stringParam2);
                break;
            case SessionEvent::Type::MeterValue:
            	handleMeterValue(evt->intParam);
                break;
            case SessionEvent::Type::Error:
            	handleError(evt->stringParam);
                break;
            case SessionEvent::Type::ErrorCleared:
            	handleErrorCleared();
                break;
            case SessionEvent::Type::CentralSystemConnection:
            	handleCentralSystemConnectionChanged(evt->boolParam);
                break;
            case SessionEvent::Type::FirmwareConnection:
                handleFirmwareConnectionChanged(evt->boolParam);
                break;
            case SessionEvent::Type::RemoteCommand:
            	handleRemoteCommand(evt->stringParam, evt->jsonParam, evt->stringParam2);
                break;
            case SessionEvent::Type::ProtocolResponse:
            	handleProtocolResponse(evt->jsonParam);
                break;
            case SessionEvent::Type::WebPlugIn:
            	handleRequestPlugIn();
                break;
            case SessionEvent::Type::WebPlugOut:
                handleRequestPlugOut();
                break;
            case SessionEvent::Type::WebStartCharge:
                handleRequestStartCharge(evt->stringParam);
                break;
            case SessionEvent::Type::WebStopCharge:
                handleRequestStopCharge();
                break;
            case SessionEvent::Type::WebTriggerError:
                handleRequestTriggerError(evt->stringParam);
                break;
            case SessionEvent::Type::WebClearError:
                handleRequestClearError();
                break;
            case SessionEvent::Type::NotifyStatusUpdate:
            	notifyStatusUpdate();
            	break;
        }
    }
}

// ---------------------------------------------------------------------------
// Handler privati
// ---------------------------------------------------------------------------

void SessionManager::transitionTo(const std::string& newState)
{
	_status.connectorState = newState;

	if (newState == "Charging") {
		_status.isCharging = true;
		_status.displayMessage = "Ricarica in corso";
	} else if (newState == "Available") {
		_status.isCharging = false;
		_status.currentMeterValue = 0;
		_status.transactionId = -1;
		_status.idTag = "";
		_status.lastError = "";
		_status.displayMessage = "Mini Charge Point";
	} else if (newState == "Faulted") {
		_status.isCharging = false;
		_status.displayMessage = "ERRORE";
	} else if (newState == "Finishing") {
		_status.isCharging = false;
		_status.displayMessage = "Rimuovi il cavo";
	}
}

void SessionManager::handleConnectorStateChanged(const std::string& newState, const std::string& errorType)
{
    _logger.debug("Connector state changed to: %s", newState);
    transitionTo(newState);

    // Invia StatusNotification al Central_System
    if (!errorType.empty()) _status.lastError = errorType;
    std::string errorCode = mapErrorCode(newState);
    sendStatusNotification(1, newState, errorCode);

    // Se c'è un RemoteStart pendente e siamo in Preparing → invia Authorize
    // con delay per dare tempo a SteVe di processare la StatusNotification Preparing
    // _awaitingAuthorize viene impostato a false quando arriva l'esito
    // dell'autorizzazione dal Central System
    if (_pendingRemoteStart && newState == "Preparing") {
        _pendingRemoteStart = false;
        _awaitingAuthorize = true;
        _status.displayMessage = "Autorizzazione...";
        notifyStatusUpdate();
        if (_remoteDelayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(_remoteDelayMs));
        sendAuthorize(_pendingIdTag);
    }

    // Se era in ricarica e il connettore è andato in Faulted → StopTransaction con EmergencyStop
    if (_status.isCharging && newState == "Faulted" && _status.transactionId >= 0) {
        _logger.debug("Fault during active session (txId=%d) — sending StopTransaction with EmergencyStop",
        		_status.transactionId);
        sendStopTransaction(_status.transactionId, _status.currentMeterValue, "EmergencyStop");
    }

    // Se era in ricarica e il connettore è in Finishing → StopTransaction normale
    if (_status.isCharging && newState == "Finishing" && _status.transactionId >= 0) {
        _logger.debug("Charging session ending (txId=%d), sending StopTransaction",
        		_status.transactionId);
        sendStopTransaction(_status.transactionId, _status.currentMeterValue, "Local");
    }

    // Se c'è un RemoteStop pendente e siamo in Finishing → non fare plug_out
    // automatico, l'utente deve rimuovere il cavo dalla web UI
    if (_pendingRemoteStop && newState == "Finishing") {
        _pendingRemoteStop = false;
    }

    notifyStatusUpdate();
}

void SessionManager::handleMeterValue(int meterValueWh)
{
    _status.currentMeterValue = meterValueWh;

    // Inoltra MeterValues al Central_System solo durante sessione attiva
    if (_status.isCharging && _status.transactionId >= 0) {
        _logger.debug("Forwarding MeterValue %d Wh (txId=%d)", meterValueWh, _status.transactionId);
        sendMeterValues(1, _status.transactionId, meterValueWh);
    }

    notifyStatusUpdate();
}

void SessionManager::handleError(const std::string& errorType)
{
    _logger.debug("Hardware error reported: %s:", errorType);

    _status.lastError = errorType;

    // Il cambio stato a Faulted arriverà tramite onConnectorStateChanged
    notifyStatusUpdate();
}

void SessionManager::handleErrorCleared()
{
    _logger.debug("Hardware error cleared");

    _status.lastError = "";

    // Il cambio stato ad Available arriverà tramite onConnectorStateChanged
    notifyStatusUpdate();
}

void SessionManager::handleCentralSystemConnectionChanged(bool connected)
{
    _logger.debug("Central_System connection status: %s",
                       std::string(connected ? "connected" : "disconnected"));

    _status.centralSystemConnected = connected;

    notifyStatusUpdate();
}

void SessionManager::handleFirmwareConnectionChanged(bool connected)
{
    _logger.debug("Firmware IPC connection status: %s",
                       std::string(connected ? "connected" : "disconnected"));

    _status.firmwareConnected = connected;

    notifyStatusUpdate();
}

// ---------------------------------------------------------------------------
// Comandi remoti dal Central_System (Proprietà 13)
// ---------------------------------------------------------------------------

void SessionManager::handleRemoteCommand(const std::string& action,
                                         const Poco::JSON::Object& payload,
                                         const std::string& uniqueId)
{
    _logger.debug("Remote command received: %s", action);
    Poco::JSON::Object response;

    if (action == "RemoteStartTransaction") {

        if (_status.connectorState == "Available")
        {
        	// Accettare: rispondere Accepted
            response.set("status", "Accepted");
            sendCallResult(uniqueId, response);

            // Estrarre idTag dal payload
            // idTag identifica l'utente che effettua la ricarica.
            std::string idTag = payload.optValue<std::string>("idTag", "UNKNOWN");

            // L'utente deve inserire il cavo.
            // Mostra messaggio sul display e attende Plug In dalla web UI.
            // Dopo il plug-in, il connettore invia il cambio stato -> Preparing
            // gestito in handleConnectorStateChanged (che imposta _pendingRemoteStart = false)
            // _pendingIdTag viene pulito quando arriva l'esito dell'autorizzazione.
            _pendingRemoteStart = true;
            _pendingIdTag = idTag;
            _pendingMeterStart = _status.currentMeterValue;
            _status.displayMessage = "Inserisci il cavo";
            notifyStatusUpdate();
        } else {
        	// Connettore non disponibile → Rejected
            _logger.debug("RemoteStartTransaction rejected: connector is %s", _status.connectorState);
            response.set("status", "Rejected");
            sendCallResult(uniqueId, response);
        }

    } else if (action == "RemoteStopTransaction") {
    	int requestedTxId = payload.optValue<int>("transactionId", -1);

        if (requestedTxId >= 0 && requestedTxId == _status.transactionId) {
        	// TransactionId corrisponde → Accepted
            response.set("status", "Accepted");
            sendCallResult(uniqueId, response);

            // Arrestare la ricarica: stop_charge ora, plug_out dopo Finishing
            sendIpcCommand("stop_charge");
            _pendingRemoteStop = true;
        } else {
        	// TransactionId non corrisponde → Rejected
            _logger.debug("RemoteStopTransaction rejected: requested txId=%d, current txId=%d",
                           requestedTxId, _status.transactionId);
            response.set("status", "Rejected");
            sendCallResult(uniqueId, response);
        }

    } else {
        _logger.debug("Unknown remote command: %s", action);
    }
}

// ---------------------------------------------------------------------------
// Risposte OCPP
// ---------------------------------------------------------------------------

void SessionManager::handleProtocolResponse(const Poco::JSON::Object& response)
{

    std::string action = response.optValue<std::string>("action", "");
    if (action == "BootNotification") {
        std::string bootStatus = response.optValue<std::string>("status", "");
        bool accepted = (bootStatus == "Accepted");
        _status.centralSystemConnected = accepted;
        if (accepted) {
            _logger.debug("Central_System connection confirmed (BootNotification Accepted)");
        } else {
            _logger.debug("BootNotification not accepted: %s", bootStatus);
        }
        notifyStatusUpdate();
        return;
    }

    // Gestione Authorize.conf
    if (response.has("idTagInfo") && _awaitingAuthorize) {
        Poco::JSON::Object::Ptr idTagInfo = response.getObject("idTagInfo");
        std::string authStatus = idTagInfo->getValue<std::string>("status");

        if (authStatus == "Accepted") {
            _logger.debug("Authorize accepted for idTag: %s", _pendingIdTag);
            _status.displayMessage = "Autorizzato";

            // Procedere con StartTransaction
			sendIpcCommand("start_charge");
			sendStartTransaction(1, _pendingIdTag, _pendingMeterStart);
			_status.idTag = _pendingIdTag;

        } else {
        	// Authorize rifiutato → non avviare la ricarica
            _logger.debug("Authorize rejected for idTag: %s (status: %s)", _pendingIdTag, authStatus);
            _status.displayMessage = "Autorizzazione fallita";
        }

        _awaitingAuthorize = false;
        _pendingIdTag = "";
        notifyStatusUpdate();
        return;
    }

    // Gestione StartTransaction.conf
    if (action == "StartTransaction" && response.has("transactionId")) {
        int txId = response.getValue<int>("transactionId");
        _logger.debug("StartTransaction confirmed, transactionId=%d", txId);

        _status.transactionId = txId;
        notifyStatusUpdate();
        return;
    }
}

// ---------------------------------------------------------------------------
// Comandi dall'interfaccia web
// ---------------------------------------------------------------------------

void SessionManager::handleRequestPlugIn()
{
    _logger.debug("Web UI: plug_in requested");
    sendIpcCommand("plug_in");
}

void SessionManager::handleRequestPlugOut()
{
    _logger.debug("Web UI: plug_out requested");
    sendIpcCommand("plug_out");
}

void SessionManager::handleRequestStartCharge(const std::string& idTag)
{
    _logger.debug("Start charge requested with idTag: %s", idTag);

    // L'utente deve aver già fatto plug-in e il connettore
    // deve aver già inviato il cambio stato -> Preparing.
    // Verifica che il connettore sia in stato Preparing prima di procedere
    if (_status.connectorState != "Preparing") {
    	_logger.debug("Cannot start charge: connector is %s (expected Preparing)",
                           _status.connectorState);
    	return;
    }

    // Authorize prima di StartTransaction
    // _awaitingAuthorize viene impostato a false quando
    // arriva l'esito dell'autorizzazione.
    // _pendingIdTag viene pulito quando arriva l'esito
    // dell'autorizzazione
    _awaitingAuthorize = true;
    _pendingIdTag = idTag;
    _pendingMeterStart = _status.currentMeterValue;
    _status.displayMessage = "Autorizzazione...";
    notifyStatusUpdate();

    sendAuthorize(_pendingIdTag);
}

void SessionManager::handleRequestStopCharge()
{
    _logger.debug("Web UI: stop_charge requested");
    sendIpcCommand("stop_charge");
}

void SessionManager::handleRequestTriggerError(const std::string& errorType)
{
    _logger.debug("Web UI: trigger_error requested (%s)", errorType);
    sendIpcCommand("trigger_error", errorType);
}

void SessionManager::handleRequestClearError()
{
    _logger.debug("Web UI: clear_error requested");
    sendIpcCommand("clear_error");
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

    std::ostringstream oss;
    cmd.stringify(oss);

    _ipcQueue->push(oss.str());
}

void SessionManager::notifyStatusUpdate()
{

    _uiQueue->push(serializeStatus(_status));
}

std::string SessionManager::serializeStatus(const ChargePointStatus& status) const
{
    Poco::JSON::Object obj;
    obj.set("type", "status_update");
    obj.set("connectorState", status.connectorState);
    obj.set("meterValue", status.currentMeterValue);
    obj.set("meterUnit", "Wh");
    obj.set("isCharging", status.isCharging);
    obj.set("transactionId", status.transactionId);
    obj.set("idTag", status.idTag);
    obj.set("centralSystemConnected", status.centralSystemConnected);
    obj.set("firmwareConnected", status.firmwareConnected);
    obj.set("lastError", status.lastError);
    obj.set("displayMessage", status.displayMessage);

    std::string timestamp = Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FORMAT);
    obj.set("timestamp", timestamp);

    std::ostringstream oss;
    obj.stringify(oss);
    return oss.str();
}

std::string SessionManager::mapErrorCode(const std::string& connectorState) const
{
    if (connectorState == "Faulted" && !_status.lastError.empty()) {
        if (_status.lastError == "HardwareFault") return "InternalError";
        if (_status.lastError == "TamperDetection") return "OtherError";
    }
    return "NoError";
}


// ---------------------------------------------------------------------------
// Invio messaggi al Central System
// ---------------------------------------------------------------------------
void SessionManager::sendStatusNotification(int connectorId, const std::string& status, const std::string& errorCode) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::StatusNotification;
	evt.connectorId = connectorId;
	evt.status = status;
	evt.errorCode = errorCode;
	_csysQueue->push(std::move(evt));
}

void SessionManager::sendAuthorize(const std::string& idTag) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::Authorize;
	evt.idTag = idTag;
	_csysQueue->push(std::move(evt));
}

void SessionManager::sendStartTransaction(int connectorId, const std::string& idTag, int meterValue) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::StartTransaction;
	evt.connectorId = connectorId;
	evt.idTag = idTag;
	evt.meterValue = meterValue;
	_csysQueue->push(std::move(evt));
}

void SessionManager::sendStopTransaction(int transactionId, int meterValue, const std::string& reason) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::StopTransaction;
	evt.transactionId = transactionId;
	evt.meterValue = meterValue;
	evt.reason = reason;
	_csysQueue->push(std::move(evt));
}

void SessionManager::sendMeterValues(int connectorId, int transactionId, int meterValue) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::MeterValues;
	evt.connectorId = connectorId;
	evt.transactionId = transactionId;
	evt.meterValue = meterValue;
	_csysQueue->push(std::move(evt));
}

void SessionManager::sendCallResult(const std::string& uniqueId, const Poco::JSON::Object& payload) {
	CentralSystemEvent evt;
	evt.type = CentralSystemEvent::Type::CallResult;
	evt.uniqueId = uniqueId;
	evt.payload = payload;
	_csysQueue->push(std::move(evt));
}
