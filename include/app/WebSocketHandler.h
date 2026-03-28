/**
 * WebSocketHandler — Gestisce connessioni WebSocket dai browser.
 *
 * Requisiti validati: 6.2, 6.4, 6.5
 */
#ifndef WEBSOCKETHANDLER_H
#define WEBSOCKETHANDLER_H

#include "app/SessionManager.h"

#include <vector>
#include <string>
#include <atomic>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Mutex.h>
#include <Poco/JSON/Object.h>

/**
 * Entry per un client WebSocket connesso.
 * Il sendMutex protegge le operazioni di invio sul WebSocket.
 */
struct WsClientEntry {
    Poco::Net::WebSocket* ws;
    Poco::Mutex sendMutex;
    std::atomic<bool> alive;

    explicit WsClientEntry(Poco::Net::WebSocket* s) : ws(s), alive(true) {}
};

class WebSocketHandler : public Poco::Net::HTTPRequestHandler {
public:
    explicit WebSocketHandler(SessionManager& sessionManager);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    SessionManager& _sessionManager;
    void processCommand(const std::string& json);
};

class WebSocketBroadcaster {
public:
    static WebSocketBroadcaster& instance();

    std::shared_ptr<WsClientEntry> addClient(Poco::Net::WebSocket* ws);
    void removeClient(const std::shared_ptr<WsClientEntry>& entry);

    void sendStatusUpdate(const SessionManager::ChargePointStatus& status);
    void sendLogEvent(const std::string& level, const std::string& message);

private:
    WebSocketBroadcaster() = default;

    std::vector<std::shared_ptr<WsClientEntry>> _clients;
    Poco::Mutex _clientsMutex;

    void broadcast(const std::string& json);
};

#endif // WEBSOCKETHANDLER_H
