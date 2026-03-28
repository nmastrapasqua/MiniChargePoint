/**
 * WebSocketHandler — Implementazione della gestione WebSocket per i browser.
 *
 * Ogni connessione WebSocket viene gestita in un handler dedicato che:
 *   1. Registra il socket nel broadcaster globale
 *   2. Invia lo stato corrente al browser appena connesso
 *   3. Riceve comandi JSON e li inoltra al SessionManager
 *   4. Rimuove il socket dal broadcaster alla disconnessione
 *
 * Il WebSocketBroadcaster è un singleton thread-safe che gestisce l'invio
 * broadcast degli aggiornamenti a tutti i browser connessi.
 *
 * Requisiti validati: 6.2, 6.4, 6.5
 */
#include "app/WebSocketHandler.h"

#include <Poco/Net/WebSocket.h>
#include <Poco/Net/NetException.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Logger.h>
#include <Poco/Timestamp.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>

#include <sstream>

using Poco::Logger;
using Poco::Net::WebSocket;

// ---------------------------------------------------------------------------
// WebSocketHandler
// ---------------------------------------------------------------------------

WebSocketHandler::WebSocketHandler(SessionManager& sessionManager)
    : _sessionManager(sessionManager)
{
}

void WebSocketHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                     Poco::Net::HTTPServerResponse& response)
{
    Logger& logger = Logger::get("WebSocketHandler");

    try {
        WebSocket ws(request, response);
        ws.setReceiveTimeout(Poco::Timespan(0, 500000)); // 500ms poll

        logger.information("Browser WebSocket connected");

        WebSocketBroadcaster::instance().addSocket(&ws);

        // Invia lo stato corrente al browser appena connesso
        WebSocketBroadcaster::instance().sendStatusUpdate(_sessionManager.getStatus());

        // Loop di ricezione comandi
        char buffer[4096];
        int flags = 0;
        int n = 0;

        while (true) {
            try {
                n = ws.receiveFrame(buffer, sizeof(buffer), flags);
            } catch (Poco::TimeoutException&) {
                // Timeout di polling, continua
                continue;
            }

            if (n <= 0 || (flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) {
                break;
            }

            if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_TEXT) {
                std::string msg(buffer, n);
                processCommand(msg);
            }
        }

        WebSocketBroadcaster::instance().removeSocket(&ws);
        logger.information("Browser WebSocket disconnected");

    } catch (Poco::Net::WebSocketException& ex) {
        logger.error("WebSocket error: %s", ex.displayText());
    } catch (Poco::Exception& ex) {
        logger.error("WebSocket handler error: %s", ex.displayText());
    }
}

void WebSocketHandler::processCommand(const std::string& json)
{
    Logger& logger = Logger::get("WebSocketHandler");

    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(json);
        Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();

        if (!obj->has("command")) {
            logger.warning("WebSocket command missing 'command' field: %s", json);
            return;
        }

        std::string command = obj->getValue<std::string>("command");
        logger.debug("Browser command received: %s", command);

        if (command == "plug_in") {
            _sessionManager.requestPlugIn();
        } else if (command == "plug_out") {
            _sessionManager.requestPlugOut();
        } else if (command == "start_charge") {
            std::string idTag = "TESTIDTAG1"; // default
            if (obj->has("idTag")) {
                idTag = obj->getValue<std::string>("idTag");
            }
            _sessionManager.requestStartCharge(idTag);
        } else if (command == "stop_charge") {
            _sessionManager.requestStopCharge();
        } else if (command == "trigger_error") {
            std::string errorType = "HardwareFault"; // default
            if (obj->has("errorType")) {
                errorType = obj->getValue<std::string>("errorType");
            }
            _sessionManager.requestTriggerError(errorType);
        } else if (command == "clear_error") {
            _sessionManager.requestClearError();
        } else {
            logger.warning("Unknown WebSocket command: %s", command);
        }

    } catch (Poco::Exception& ex) {
        logger.error("Failed to parse WebSocket command: %s", ex.displayText());
    }
}

// ---------------------------------------------------------------------------
// WebSocketBroadcaster (singleton)
// ---------------------------------------------------------------------------

WebSocketBroadcaster& WebSocketBroadcaster::instance()
{
    static WebSocketBroadcaster inst;
    return inst;
}

void WebSocketBroadcaster::addSocket(Poco::Net::WebSocket* ws)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _sockets.push_back(ws);
}

void WebSocketBroadcaster::removeSocket(Poco::Net::WebSocket* ws)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _sockets.erase(
        std::remove(_sockets.begin(), _sockets.end(), ws),
        _sockets.end()
    );
}

void WebSocketBroadcaster::sendStatusUpdate(const SessionManager::ChargePointStatus& status)
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
    obj.set("lastError", status.lastError);

    std::string timestamp = Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FORMAT);
    obj.set("timestamp", timestamp);

    std::ostringstream oss;
    obj.stringify(oss);
    broadcast(oss.str());
}

void WebSocketBroadcaster::sendLogEvent(const std::string& level,
                                        const std::string& message)
{
    Poco::JSON::Object obj;
    obj.set("type", "log_event");
    obj.set("level", level);
    obj.set("message", message);

    std::string timestamp = Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FORMAT);
    obj.set("timestamp", timestamp);

    std::ostringstream oss;
    obj.stringify(oss);
    broadcast(oss.str());
}

void WebSocketBroadcaster::broadcast(const std::string& json)
{
    Poco::Mutex::ScopedLock lock(_mutex);

    auto it = _sockets.begin();
    while (it != _sockets.end()) {
        try {
            (*it)->sendFrame(json.data(), static_cast<int>(json.size()),
                             WebSocket::FRAME_TEXT);
            ++it;
        } catch (Poco::Exception&) {
            // Client disconnesso, rimuovere
            it = _sockets.erase(it);
        }
    }
}
