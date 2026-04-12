/**
 * WebSocketHandler — Gestisce la connessione WebSocket da un solo browser.
 * Questa implementazione è volutamente semplice e non gestisce
 * la connessione da più browser; gestire connessioni multiple
 * comporta un aumento della complessità che di fatto
 * sarebbe inutile in questo contesto.
 *
 * Requisiti validati: 6.2, 6.4, 6.5
 */
#ifndef WEBSOCKETHANDLER_H
#define WEBSOCKETHANDLER_H

#include "common/SessionEvent.h"
#include "common/ThreadSafeQueue.h"

#include <vector>
#include <string>
#include <atomic>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>


class WebSocketHandler : public Poco::Net::HTTPRequestHandler {
public:
    explicit WebSocketHandler(ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    ThreadSafeQueue<SessionEvent>* _eventQueue = nullptr;
    ThreadSafeQueue<std::string>* _uiQueue = nullptr;

    Poco::Logger& _logger;

    void processCommand(const std::string& json);

};



#endif // WEBSOCKETHANDLER_H
