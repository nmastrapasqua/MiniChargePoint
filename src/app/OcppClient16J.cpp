/**
 * OcppClient16J — Implementazione di ProtocolAdapter per OCPP 1.6J su WebSocket.
 *
 * Gestisce la connessione WebSocket con il Central_System, serializza/deserializza
 * messaggi OCPP tramite OcppMessageSerializer16J, e coordina Heartbeat periodico
 * e riconnessione automatica.
 *
 * Thread:
 *   - receiveThread: ricezione continua di messaggi dal Central_System
 *   - heartbeatTimer: invio periodico di Heartbeat
 *   - reconnectTimer: tentativo di riconnessione ogni 10 secondi
 *
 * Requisiti validati: 3.1–3.15, 5.3, 9.3
 */
#include "app/OcppClient16J.h"

#include <sstream>
#include <Poco/Logger.h>
#include <Poco/Exception.h>
#include <Poco/URI.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Timestamp.h>
#include <Poco/Net/WebSocket.h>

using Poco::Logger;
using Poco::Net::WebSocket;

// ---------------------------------------------------------------------------
// Costruttore / Distruttore
// ---------------------------------------------------------------------------

OcppClient16J::OcppClient16J(const std::string& centralSystemUrl,
                       const std::string& chargePointId,
					   ThreadSafeQueue<SessionEvent>* eventQ,
					   ThreadSafeQueue<CentralSystemEvent>* csysQueue)
    : _url(centralSystemUrl)
    , _chargePointId(chargePointId)
    , _host()
    , _port(80)
    , _path()
    , _connected(false)
    , _running(false)
    , _heartbeatTimer(0, 0)
    , _heartbeatInterval(0)
    , _heartbeatTimerStarted(false)
    , _reconnectTimer(0, 0)
    , _reconnectNeeded(false)
    , _reconnectTimerStarted(false)
    , _uniqueIdCounter(1)
	, _eventQueue(eventQ)
	, _csysQueue(csysQueue)
{
    if (!_eventQueue) {
    	throw std::invalid_argument("eventQueue non può essere nullptr");
    }
    if (!_csysQueue) {
    	throw std::invalid_argument("csysQueue non può essere nullptr");
    }

    parseUrl(centralSystemUrl);
}

OcppClient16J::~OcppClient16J()
{
    stop();
}

void OcppClient16J::start() {
    if (_running) return;
    _running = true;
    _thread.start(*this);
}

void OcppClient16J::stop() {
    if (!_running) return;

    _running = false;

    try { _thread.join(); } catch (...) {}

    stopHeartbeat();

    if (_ws) {
    	try { _ws->close(); } catch (...) {}
    	_ws.reset();
    }
    _session.reset();
}

void OcppClient16J::run() {

	Logger& logger = Logger::get("OcppClient");
	char buffer[4096];

	while (_running) {

		// --- CONNECT ---
		if (!_connected) {
			if (tryConnect()) {
				sendBootNotification("MiniChargePoint", "MiniCP");
			} else {
				std::this_thread::sleep_for(std::chrono::seconds(5));
				continue;
			}
		}

		// --- READ Messaggi da Central System
		try {
			int flags = 0;
			int n = 0;

			n = _ws->receiveFrame(buffer, sizeof(buffer), flags);

			if (n <= 0 || (flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) {
				_connected = false;
			}

			if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_TEXT) {
				std::string msg(buffer, n);
				logger.information("Received: %s", msg);
				auto ocppMsg = OcppMessageSerializer16J::deserialize(msg);
				switch (ocppMsg.type) {
					case OcppMessageSerializer16J::MessageType::CALLRESULT:
						handleCallResult(ocppMsg.uniqueId, ocppMsg.payload);
				        break;
					case OcppMessageSerializer16J::MessageType::CALLERROR:
						handleCallError(ocppMsg.uniqueId, ocppMsg.errorCode, ocppMsg.errorDescription);
				        break;
					case OcppMessageSerializer16J::MessageType::CALL:
						handleIncomingCall(ocppMsg.uniqueId, ocppMsg.action, ocppMsg.payload);
				        break;
				}
			}

		} catch (Poco::TimeoutException& e) {
			// Receive timeout — normal, just loop
			continue;
		} catch (Poco::Exception& e) {
			logger.warning("Receive error: %s", e.displayText());
			_connected = false;
		}

	}
}

// ---------------------------------------------------------------------------
// parseUrl — Estrae host, port, path dall'URL WebSocket
// ---------------------------------------------------------------------------

void OcppClient16J::parseUrl(const std::string& url)
{
    // ws://host:port/path → http://host:port/path for URI parsing
    std::string httpUrl = url;
    if (httpUrl.substr(0, 5) == "ws://") {
        httpUrl = "http://" + httpUrl.substr(5);
    } else if (httpUrl.substr(0, 6) == "wss://") {
        httpUrl = "https://" + httpUrl.substr(6);
    }

    Poco::URI uri(httpUrl);
    _host = uri.getHost();
    _port = uri.getPort();
    if (_port == 0) _port = 80;
    // Append chargePointId to the path
    _path = uri.getPath();
    if (!_path.empty() && _path.back() != '/') {
        _path += "/";
    }
    _path += _chargePointId;
}

// ---------------------------------------------------------------------------
// connect — Stabilisce connessione WebSocket con subprotocol "ocpp1.6"
// ---------------------------------------------------------------------------

bool OcppClient16J::tryConnect()
{
    Logger& logger = Logger::get("OcppClient");

    try {
        logger.debug("Connecting to Central_System at %s (host=%s, port=%hu, path=%s)",
                           _url, _host, _port, _path);

        _session.reset(new Poco::Net::HTTPClientSession(_host, _port));
        _session->setTimeout(Poco::Timespan(30, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, _path,
                                       Poco::Net::HTTPRequest::HTTP_1_1);
        request.set("Sec-WebSocket-Protocol", "ocpp1.6");

        Poco::Net::HTTPResponse response;
        _ws.reset(new Poco::Net::WebSocket(*_session, request, response));
        _ws->setReceiveTimeout(Poco::Timespan(0, 100000)); // 100 ms

        _connected = true;
        logger.information("WebSocket connection established with Central_System");
        return true;
    } catch (Poco::Exception& e) {
        logger.warning("Failed to connect to Central_System: %s", e.displayText());
        return false;
    }
}


// ---------------------------------------------------------------------------
// isConnected
// ---------------------------------------------------------------------------

bool OcppClient16J::isConnected() const
{
    return _connected;
}

// ---------------------------------------------------------------------------
// sendBootNotification
// ---------------------------------------------------------------------------

void OcppClient16J::sendBootNotification(const std::string& chargePointModel,
                                      const std::string& chargePointVendor)
{
    Poco::JSON::Object payload;
    payload.set("chargePointModel", chargePointModel);
    payload.set("chargePointVendor", chargePointVendor);
    sendCall("BootNotification", payload);
}

// ---------------------------------------------------------------------------
// sendHeartbeat
// ---------------------------------------------------------------------------

void OcppClient16J::sendHeartbeat()
{
    Poco::JSON::Object payload;
    sendCall("Heartbeat", payload);
}

// ---------------------------------------------------------------------------
// sendAuthorize
// ---------------------------------------------------------------------------

void OcppClient16J::sendAuthorize(const std::string& idTag)
{
    Poco::JSON::Object payload;
    payload.set("idTag", idTag);
    sendCall("Authorize", payload);
}

// ---------------------------------------------------------------------------
// sendStatusNotification
// ---------------------------------------------------------------------------

void OcppClient16J::sendStatusNotification(int connectorId,
                                        const std::string& status,
                                        const std::string& errorCode)
{
    Poco::JSON::Object payload;
    payload.set("connectorId", connectorId);
    payload.set("status", status);
    payload.set("errorCode", errorCode);
    payload.set("timestamp", Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FRAC_FORMAT));
    sendCall("StatusNotification", payload);
}

// ---------------------------------------------------------------------------
// sendStartTransaction
// ---------------------------------------------------------------------------

void OcppClient16J::sendStartTransaction(int connectorId,
                                      const std::string& idTag,
                                      int meterStart)
{
    Poco::JSON::Object payload;
    payload.set("connectorId", connectorId);
    payload.set("idTag", idTag);
    payload.set("meterStart", meterStart);
    payload.set("timestamp", Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FRAC_FORMAT));
    sendCall("StartTransaction", payload);
}

// ---------------------------------------------------------------------------
// sendMeterValues
// ---------------------------------------------------------------------------

void OcppClient16J::sendMeterValues(int connectorId,
                                 int transactionId,
                                 int meterValue)
{
    // Build the meterValue structure per OCPP 1.6J spec
    Poco::JSON::Object sampledValue;
    sampledValue.set("value", std::to_string(meterValue));
    sampledValue.set("measurand", "Energy.Active.Import.Register");
    sampledValue.set("unit", "Wh");

    Poco::JSON::Array sampledValues;
    sampledValues.add(sampledValue);

    Poco::JSON::Object meterVal;
    meterVal.set("timestamp", Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FRAC_FORMAT));
    meterVal.set("sampledValue", sampledValues);

    Poco::JSON::Array meterValueArr;
    meterValueArr.add(meterVal);

    Poco::JSON::Object payload;
    payload.set("connectorId", connectorId);
    payload.set("transactionId", transactionId);
    payload.set("meterValue", meterValueArr);
    sendCall("MeterValues", payload);
}

// ---------------------------------------------------------------------------
// sendStopTransaction
// ---------------------------------------------------------------------------

void OcppClient16J::sendStopTransaction(int transactionId,
                                     int meterStop,
                                     const std::string& reason)
{
    Poco::JSON::Object payload;
    payload.set("transactionId", transactionId);
    payload.set("meterStop", meterStop);
    payload.set("timestamp", Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FRAC_FORMAT));
    payload.set("reason", reason);
    sendCall("StopTransaction", payload);
}


// ---------------------------------------------------------------------------
// sendCallResult — Invia CALLRESULT in risposta a un CALL ricevuto
// ---------------------------------------------------------------------------

void OcppClient16J::sendCallResult(const std::string& uniqueId,
                                const Poco::JSON::Object& payload)
{
    OcppMessageSerializer16J::OcppMessage msg;
    msg.type = OcppMessageSerializer16J::MessageType::CALLRESULT;
    msg.uniqueId = uniqueId;
    msg.payload = payload;

    std::string json = OcppMessageSerializer16J::serialize(msg);

    Logger& logger = Logger::get("OcppClient");
    logger.debug("Sending CALLRESULT: %s", json);

    sendRaw(json);
}

// ---------------------------------------------------------------------------
// sendCall — Invia un messaggio CALL e registra il uniqueId
// ---------------------------------------------------------------------------

void OcppClient16J::sendCall(const std::string& action,
                          const Poco::JSON::Object& payload)
{
    std::string uid = generateUniqueId();

    OcppMessageSerializer16J::OcppMessage msg;
    msg.type = OcppMessageSerializer16J::MessageType::CALL;
    msg.uniqueId = uid;
    msg.action = action;
    msg.payload = payload;

    std::string json = OcppMessageSerializer16J::serialize(msg);

    _pendingCalls[uid] = action;

    Logger& logger = Logger::get("OcppClient");
    logger.information("Sending CALL [%s] %s: %s", uid, action, json);

    sendRaw(json);
}

// ---------------------------------------------------------------------------
// sendRaw — Invio thread-safe sul WebSocket
// ---------------------------------------------------------------------------

void OcppClient16J::sendRaw(const std::string& data)
{

    if (!_ws || !_connected) {
        Logger& logger = Logger::get("OcppClient");
        logger.warning("Cannot send message: not connected");
        return;
    }

    try {
        _ws->sendFrame(data.data(), static_cast<int>(data.size()),
                       Poco::Net::WebSocket::FRAME_TEXT);
    } catch (Poco::Exception& e) {
        Logger& logger = Logger::get("OcppClient");
        logger.warning("Send failed: %s", e.displayText());
        _connected = false;
        notifyConnectionStatus(false);
    }
}

// ---------------------------------------------------------------------------
// receiveLoop — Thread dedicato per ricezione messaggi
// ---------------------------------------------------------------------------



// ---------------------------------------------------------------------------
// handleCallResult — Gestisce risposte CALLRESULT dal Central_System
// ---------------------------------------------------------------------------

void OcppClient16J::handleCallResult(const std::string& uniqueId,
                                  const Poco::JSON::Object& payload)
{
    Logger& logger = Logger::get("OcppClient");

    // Find the original action for this uniqueId
    std::string action;

    auto it = _pendingCalls.find(uniqueId);
    if (it != _pendingCalls.end()) {
    	action = it->second;
        _pendingCalls.erase(it);
    }


    logger.debug("CALLRESULT [%s] for action '%s'", uniqueId, action);

    // Handle BootNotification.conf: if Accepted, start Heartbeat
    if (action == "BootNotification") {
        std::string status = payload.optValue<std::string>("status", "");
        if (status == "Accepted") {
            int interval = payload.optValue<int>("interval", 300);
            logger.information("BootNotification Accepted, heartbeat interval: %d s", interval);
            startHeartbeat(interval);
        } else {
            // Rejected or Pending — retry after interval
            int interval = payload.optValue<int>("interval", 30);
            logger.warning("BootNotification %s, retrying in %d s", status, interval);
        }
    }

    // Build a response object that includes the action for the SessionManager
    Poco::JSON::Object response;
    response.set("action", action);
    response.set("uniqueId", uniqueId);
    // Copy all payload fields into the response
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        response.set(it->first, it->second);
    }

    // Push ProtocolResponse event to _eventQueue
    SessionEvent evt;
    evt.type = SessionEvent::Type::ProtocolResponse;
    evt.jsonParam = response;
    _eventQueue->push(std::move(evt));

}

// ---------------------------------------------------------------------------
// handleCallError — Gestisce risposte CALLERROR dal Central_System
// ---------------------------------------------------------------------------

void OcppClient16J::handleCallError(const std::string& uniqueId,
                                 const std::string& errorCode,
                                 const std::string& errorDescription)
{
    Logger& logger = Logger::get("OcppClient");

    std::string action;

    auto it = _pendingCalls.find(uniqueId);
    if (it != _pendingCalls.end()) {
    	action = it->second;
        _pendingCalls.erase(it);
    }


    logger.warning("CALLERROR [%s] for action '%s': %s — %s",
                   uniqueId, action, errorCode, errorDescription);
}

// ---------------------------------------------------------------------------
// handleIncomingCall — Gestisce CALL in ingresso (comandi remoti)
// ---------------------------------------------------------------------------

void OcppClient16J::handleIncomingCall(const std::string& uniqueId,
                                    const std::string& action,
                                    const Poco::JSON::Object& payload)
{
    Logger& logger = Logger::get("OcppClient");
    logger.debug("Incoming CALL [%s] action: %s", uniqueId, action);

    // Dispatch RemoteStartTransaction and RemoteStopTransaction to callback
    if (action == "RemoteStartTransaction" || action == "RemoteStopTransaction") {
    	// Piccolo delay per permettere a SteVe di registrare il CALL come pendente
    	Poco::Thread::sleep(100);
    	SessionEvent evt;
    	evt.type = SessionEvent::Type::RemoteCommand;
    	evt.stringParam = action;
    	evt.stringParam2 = uniqueId;
    	evt.jsonParam = payload;
    	_eventQueue->push(std::move(evt));
    } else {
        // Unknown incoming CALL — log and ignore
        logger.warning("Unhandled incoming CALL action: %s", action);
    }
}

// ---------------------------------------------------------------------------
// generateUniqueId
// ---------------------------------------------------------------------------

std::string OcppClient16J::generateUniqueId()
{
    int id = _uniqueIdCounter.fetch_add(1);
    return _chargePointId + "-" + std::to_string(id);
}

// ---------------------------------------------------------------------------
// Heartbeat timer
// ---------------------------------------------------------------------------

void OcppClient16J::onHeartbeatTimer(Poco::Timer& /*timer*/)
{
    if (_connected && _running) {
        sendHeartbeat();
    }
}

void OcppClient16J::startHeartbeat(int intervalSeconds)
{
    if (intervalSeconds <= 0) return;

    _heartbeatInterval = intervalSeconds;
    long intervalMs = intervalSeconds * 1000L;

    Logger& logger = Logger::get("OcppClient");
    logger.debug("Starting Heartbeat timer: %d s", intervalSeconds);

    if (!_heartbeatTimerStarted) {
        _heartbeatTimer.setStartInterval(intervalMs);
        _heartbeatTimer.setPeriodicInterval(intervalMs);
        Poco::TimerCallback<OcppClient16J> cb(*this, &OcppClient16J::onHeartbeatTimer);
        _heartbeatTimer.start(cb);
        _heartbeatTimerStarted = true;
    } else {
        _heartbeatTimer.restart(intervalMs);
    }
}

void OcppClient16J::stopHeartbeat()
{
    if (_heartbeatTimerStarted) {
        try { _heartbeatTimer.restart(0); } catch (...) {}
    }
}

// ---------------------------------------------------------------------------
// notifyConnectionStatus — Notifica il cambio stato connessione
// ---------------------------------------------------------------------------

void OcppClient16J::notifyConnectionStatus(bool connected)
{
	SessionEvent evt;
	evt.type = SessionEvent::Type::CentralSystemConnection;
	evt.boolParam = connected;
	_eventQueue->push(std::move(evt));
}
