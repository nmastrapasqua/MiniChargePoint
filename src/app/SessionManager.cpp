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
		ThreadSafeQueue<CentralSystemEvent>* csysQueue)
    : _awaitingAuthorize(false)
	, _pendingIdTag("")
    , _pendingMeterStart(0)
	, _eventQueue(eventQ)
	, _uiQueue(uiQ)
	, _ipcQueue(ipcQ)
	, _csysQueue(csysQueue)
{
    _status.connectorState = "Available";
    _status.currentMeterValue = 0;
    _status.isCharging = false;
    _status.transactionId = -1;
    _status.idTag = "";
    _status.centralSystemConnected = false;
    _status.firmwareConnected = false;
    _status.lastError = "";

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

void SessionManager::handleConnectorStateChanged(const std::string& newState, const std::string& errorType)
{
    _logger.information("Connector state changed to: %s", newState);

    bool wasCharging = _status.isCharging;
    int txId = _status.transactionId;
    int meterStop = _status.currentMeterValue;

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

    if (!errorType.empty()) _status.lastError = errorType;
    std::string errorCode = mapErrorCode(newState);
    CentralSystemEvent statusEvt;
    statusEvt.type = CentralSystemEvent::Type::StatusNotification;
    statusEvt.connectorId = 1;
    statusEvt.status = newState;
    statusEvt.errorCode = errorCode;
    _csysQueue->push(std::move(statusEvt));

    if (wasCharging && newState == "Faulted" && txId >= 0) {
        _logger.warning("Fault during active session (txId=%d) — sending StopTransaction with EmergencyStop", txId);
        CentralSystemEvent stopEvt;
        stopEvt.type = CentralSystemEvent::Type::StopTransaction;
        stopEvt.transactionId = txId;
        stopEvt.meterValue = meterStop;
        stopEvt.reason = "EmergencyStop";
        _csysQueue->push(std::move(stopEvt));
    }

    if (wasCharging && newState == "Finishing" && txId >= 0) {
        _logger.information("Charging session ending (txId=%d), sending StopTransaction", txId);
        CentralSystemEvent stopLocalEvt;
        stopLocalEvt.type = CentralSystemEvent::Type::StopTransaction;
        stopLocalEvt.transactionId = txId;
        stopLocalEvt.meterValue = meterStop;
        stopLocalEvt.reason = "Local";
        _csysQueue->push(std::move(stopLocalEvt));
    }

    notifyStatusUpdate();
}

void SessionManager::handleMeterValue(int meterValueWh)
{
    int txId = _status.transactionId;
    bool charging = _status.isCharging;

    _status.currentMeterValue = meterValueWh;

    if (charging && txId >= 0) {
        _logger.debug("Forwarding MeterValue %d Wh (txId=%d)", meterValueWh, txId);
        CentralSystemEvent meterEvt;
        meterEvt.type = CentralSystemEvent::Type::MeterValues;
        meterEvt.connectorId = 1;
        meterEvt.transactionId = txId;
        meterEvt.meterValue = meterValueWh;
        _csysQueue->push(std::move(meterEvt));
    }

    notifyStatusUpdate();
}

void SessionManager::handleError(const std::string& errorType)
{
	// Mappa errorType → OCPP errorCode
	std::string errorCode = "NoError";
	if (errorType == "HardwareFault")
		errorCode = "InternalError";
	else if (errorType == "TamperDetection")
		errorCode = "OtherError";

    _logger.warning("Hardware error reported: %s (OCPP code: %s)", errorType, errorCode);

    _status.lastError = errorType;

    notifyStatusUpdate();
}

void SessionManager::handleErrorCleared()
{
    _logger.information("Hardware error cleared");

    _status.lastError = "";

    notifyStatusUpdate();
}

void SessionManager::handleCentralSystemConnectionChanged(bool connected)
{
    _logger.information("Central_System connection status: %s",
                       std::string(connected ? "connected" : "disconnected"));

    _status.centralSystemConnected = connected;

    notifyStatusUpdate();
}

void SessionManager::handleFirmwareConnectionChanged(bool connected)
{
    _logger.information("Firmware IPC connection status: %s",
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
    _logger.information("Remote command received: %s", action);

    if (action == "RemoteStartTransaction") {

        Poco::JSON::Object response;

        if (_status.connectorState == "Available" &&
                _status.firmwareConnected &&
                _status.centralSystemConnected)
        {
            response.set("status", "Accepted");
            CentralSystemEvent acceptedEvt;
            acceptedEvt.type = CentralSystemEvent::Type::CallResult;
            acceptedEvt.uniqueId = uniqueId;
            acceptedEvt.payload = response;
            _csysQueue->push(std::move(acceptedEvt));

            std::string idTag = payload.optValue<std::string>("idTag", "UNKNOWN");

            sendIpcCommand("plug_in");

            _awaitingAuthorize = true;
            _pendingIdTag = idTag;
            _pendingMeterStart = _status.currentMeterValue;
            CentralSystemEvent authEvt;
            authEvt.type = CentralSystemEvent::Type::Authorize;
            authEvt.idTag = idTag;
            _csysQueue->push(std::move(authEvt));
        } else {
            _logger.warning("RemoteStartTransaction rejected: connector is %s", _status.connectorState);
            response.set("status", "Rejected");
            CentralSystemEvent rejEvt;
            rejEvt.type = CentralSystemEvent::Type::CallResult;
            rejEvt.uniqueId = uniqueId;
            rejEvt.payload = response;
            _csysQueue->push(std::move(rejEvt));
        }

    } else if (action == "RemoteStopTransaction") {
        int requestedTxId = -1;
        if (payload.has("transactionId")) {
            requestedTxId = payload.getValue<int>("transactionId");
        }

        int currentTxId = _status.transactionId;

        Poco::JSON::Object response;

        if (requestedTxId >= 0 && requestedTxId == currentTxId) {
            response.set("status", "Accepted");
            CentralSystemEvent acceptedEvt;
            acceptedEvt.type = CentralSystemEvent::Type::CallResult;
            acceptedEvt.uniqueId = uniqueId;
            acceptedEvt.payload = response;
            _csysQueue->push(std::move(acceptedEvt));

            sendIpcCommand("stop_charge");
            sendIpcCommand("plug_out");
        } else {
            _logger.warning("RemoteStopTransaction rejected: requested txId=%d, current txId=%d",
                           requestedTxId, currentTxId);
            response.set("status", "Rejected");
            CentralSystemEvent rejEvt;
            rejEvt.type = CentralSystemEvent::Type::CallResult;
            rejEvt.uniqueId = uniqueId;
            rejEvt.payload = response;
            _csysQueue->push(std::move(rejEvt));
        }

    } else {
        _logger.warning("Unknown remote command: %s", action);
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
            _logger.information("Central_System connection confirmed (BootNotification Accepted)");
        } else {
            _logger.warning("BootNotification not accepted: %s", bootStatus);
        }
        notifyStatusUpdate();
        return;
    }

    if (response.has("idTagInfo") && _awaitingAuthorize) {
        Poco::JSON::Object::Ptr idTagInfo = response.getObject("idTagInfo");
        std::string authStatus = idTagInfo->getValue<std::string>("status");

        if (authStatus == "Accepted") {
            _logger.information("Authorize accepted for idTag: %s", _pendingIdTag);

			sendIpcCommand("start_charge");
			CentralSystemEvent startEvt;
			startEvt.type = CentralSystemEvent::Type::StartTransaction;
			startEvt.connectorId = 1;
			startEvt.idTag = _pendingIdTag;
			startEvt.meterValue = _pendingMeterStart;
			_csysQueue->push(std::move(startEvt));
			_status.idTag = _pendingIdTag;

        } else {
            _logger.warning("Authorize rejected for idTag: %s (status: %s)", _pendingIdTag, authStatus);
        }

        _awaitingAuthorize = false;
        _pendingIdTag = "";
        _pendingMeterStart = 0;
        notifyStatusUpdate();
        return;
    }

    if (action == "StartTransaction" && response.has("transactionId")) {
        int txId = response.getValue<int>("transactionId");
        _logger.information("StartTransaction confirmed, transactionId=%d", txId);

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
    _logger.information("Web UI: plug_in requested");
    sendIpcCommand("plug_in");
}

void SessionManager::handleRequestPlugOut()
{
    _logger.information("Web UI: plug_out requested");
    sendIpcCommand("plug_out");
}

void SessionManager::handleRequestStartCharge(const std::string& idTag)
{
    _logger.information("Start charge requested with idTag: %s", idTag);

    if (_status.connectorState != "Preparing") {
    	_logger.warning("Cannot start charge: connector is %s (expected Preparing)",
                           _status.connectorState);
    	return;
    }

    _awaitingAuthorize = true;
    _pendingIdTag = idTag;
    _pendingMeterStart = _status.currentMeterValue;

    CentralSystemEvent authEvt;
    authEvt.type = CentralSystemEvent::Type::Authorize;
    authEvt.idTag = idTag;
    _csysQueue->push(std::move(authEvt));
}

void SessionManager::handleRequestStopCharge()
{
    _logger.information("Web UI: stop_charge requested");
    sendIpcCommand("stop_charge");
}

void SessionManager::handleRequestTriggerError(const std::string& errorType)
{
    _logger.information("Web UI: trigger_error requested (%s)", errorType);
    sendIpcCommand("trigger_error", errorType);
}

void SessionManager::handleRequestClearError()
{
    _logger.information("Web UI: clear_error requested");
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
