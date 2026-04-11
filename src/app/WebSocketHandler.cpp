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
#include <Poco/Timestamp.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>

#include <sstream>

using Poco::Net::WebSocket;

// ---------------------------------------------------------------------------
// WebSocketHandler
// ---------------------------------------------------------------------------

WebSocketHandler::WebSocketHandler(ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq)
	: _eventQueue(q)
	, _uiQueue(uq)
{
	 if (!_eventQueue) {
		 throw std::invalid_argument("eventQueue non può essere nullptr");
	 }

	 if (!_uiQueue) {
		 throw std::invalid_argument("uiQueue non può essere nullptr");
	 }
}

void WebSocketHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                     Poco::Net::HTTPServerResponse& response)
{
    try {
        WebSocket ws(request, response);
        ws.setReceiveTimeout(Poco::Timespan(0, 100000)); // 100 ms

        _logger.information("Browser WebSocket connected");

        // Richiede l'aggiornamento dello stato
        SessionEvent evt;
        evt.type = SessionEvent::Type::NotifyStatusUpdate;
        _eventQueue->push(std::move(evt));

        char buffer[4096];
        int flags = 0;
        int n = 0;

        while (true) {

        	if (_uiQueue->isClosed()) break;

        	// Riceve l'aggiornameto dello stato
        	while (auto msg = _uiQueue->try_pop()) {
        		_logger.debug("Status update %s", msg.value());
        		ws.sendFrame(msg->data(), (int)msg->size());
        	}

            try {
            	// Riceve i comandi dalla web ui
                n = ws.receiveFrame(buffer, sizeof(buffer), flags);

                if (n <= 0 || (flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE) {
                	break;
                }

                if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_TEXT) {
                	std::string msg(buffer, n);
                    processCommand(msg);
                }
            } catch (Poco::TimeoutException&) {
                continue;
            }
        }

        _logger.information("Browser WebSocket disconnected");

    } catch (Poco::Net::WebSocketException& ex) {
        _logger.error("WebSocket error: %s", ex.displayText());
    } catch (Poco::Exception& ex) {
        _logger.error("WebSocket handler error: %s", ex.displayText());
    }
}

void WebSocketHandler::processCommand(const std::string& json)
{

    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(json);
        Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();

        if (!obj->has("command")) {
            _logger.warning("WebSocket command missing 'command' field: %s", json);
            return;
        }

        std::string command = obj->getValue<std::string>("command");
        _logger.debug("Browser command received: %s", command);

        if (command == "plug_in") {
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebPlugIn;
            _eventQueue->push(std::move(evt));
        } else if (command == "plug_out") {
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebPlugOut;
            _eventQueue->push(std::move(evt));
        } else if (command == "start_charge") {
            std::string idTag = "TESTIDTAG1";
            if (obj->has("idTag")) {
                idTag = obj->getValue<std::string>("idTag");
            }
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebStartCharge;
            evt.stringParam = idTag;
            _eventQueue->push(std::move(evt));
        } else if (command == "stop_charge") {
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebStopCharge;
            _eventQueue->push(std::move(evt));
        } else if (command == "trigger_error") {
            std::string errorType = "HardwareFault";
            if (obj->has("errorType")) {
                errorType = obj->getValue<std::string>("errorType");
            }
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebTriggerError;
            evt.stringParam = errorType;
            _eventQueue->push(std::move(evt));
        } else if (command == "clear_error") {
            SessionEvent evt;
            evt.type = SessionEvent::Type::WebClearError;
            _eventQueue->push(std::move(evt));
        } else {
            _logger.warning("Unknown WebSocket command: %s", command);
        }

    } catch (Poco::Exception& ex) {
        _logger.error("Failed to parse WebSocket command: %s", ex.displayText());
    }
}


