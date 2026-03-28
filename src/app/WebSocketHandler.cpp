/**
 * WebSocketHandler — Implementazione.
 *
 * Approccio: ogni client ha un sendMutex che protegge le operazioni di invio.
 * Il receiveFrame non ha bisogno di lock perché è chiamato solo dal thread
 * del handler. Il sendFrame è protetto dal sendMutex e può essere chiamato
 * dal thread del broadcaster.
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
        ws.setReceiveTimeout(Poco::Timespan(1, 0)); // 1s poll

        logger.information("Browser WebSocket connected");

        auto entry = WebSocketBroadcaster::instance().addClient(&ws);

        // Invia lo stato corrente
        WebSocketBroadcaster::instance().sendStatusUpdate(_sessionManager.getStatus());

        char buffer[4096];
        int flags = 0;
        int n = 0;

        while (entry->alive) {
            try {
                n = ws.receiveFrame(buffer, sizeof(buffer), flags);
            } catch (Poco::TimeoutException&) {
                continue;
            } catch (Poco::Exception&) {
                break;
            }

            if (n <= 0 || (flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) {
                break;
            }

            if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_TEXT) {
                std::string msg(buffer, n);
                processCommand(msg);
            }
        }

        entry->alive = false;
        WebSocketBroadcaster::instance().removeClient(entry);
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
            std::string idTag = "TESTIDTAG1";
            if (obj->has("idTag")) {
                idTag = obj->getValue<std::string>("idTag");
            }
            _sessionManager.requestStartCharge(idTag);
        } else if (command == "stop_charge") {
            _sessionManager.requestStopCharge();
        } else if (command == "trigger_error") {
            std::string errorType = "HardwareFault";
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
// WebSocketBroadcaster
// ---------------------------------------------------------------------------

WebSocketBroadcaster& WebSocketBroadcaster::instance()
{
    static WebSocketBroadcaster inst;
    return inst;
}

std::shared_ptr<WsClientEntry> WebSocketBroadcaster::addClient(Poco::Net::WebSocket* ws)
{
    auto entry = std::make_shared<WsClientEntry>(ws);
    Poco::Mutex::ScopedLock lock(_clientsMutex);
    _clients.push_back(entry);
    return entry;
}

void WebSocketBroadcaster::removeClient(const std::shared_ptr<WsClientEntry>& entry)
{
    Poco::Mutex::ScopedLock lock(_clientsMutex);
    _clients.erase(
        std::remove(_clients.begin(), _clients.end(), entry),
        _clients.end()
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
    obj.set("firmwareConnected", status.firmwareConnected);
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
    // Snapshot dei client sotto lock
    std::vector<std::shared_ptr<WsClientEntry>> snapshot;
    {
        Poco::Mutex::ScopedLock lock(_clientsMutex);
        snapshot = _clients;
    }

    // Invio senza tenere _clientsMutex
    for (auto& entry : snapshot) {
        if (!entry->alive) continue;
        Poco::Mutex::ScopedLock sendLock(entry->sendMutex);
        try {
            entry->ws->sendFrame(json.data(), static_cast<int>(json.size()),
                                 WebSocket::FRAME_TEXT);
        } catch (Poco::Exception&) {
            entry->alive = false;
        }
    }
}
