/**
 * WebSocketHandler — Gestisce connessioni WebSocket dai browser.
 *
 * Responsabilità:
 *   - Accettare connessioni WebSocket dai browser (upgrade HTTP → WebSocket)
 *   - Ricevere comandi JSON dal browser e inoltrarli al SessionManager
 *   - Inviare aggiornamenti di stato e log eventi ai browser connessi
 *   - Gestire la lista dei client WebSocket connessi (thread-safe)
 *
 * Requisiti validati: 6.2, 6.4, 6.5
 */
#ifndef WEBSOCKETHANDLER_H
#define WEBSOCKETHANDLER_H

#include "app/SessionManager.h"

#include <vector>
#include <string>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Mutex.h>
#include <Poco/JSON/Object.h>

/**
 * Gestisce una singola connessione WebSocket dal browser.
 * Creata da WebSocketHandlerFactory per ogni richiesta di upgrade WebSocket.
 */
class WebSocketHandler : public Poco::Net::HTTPRequestHandler {
public:
    explicit WebSocketHandler(SessionManager& sessionManager);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    SessionManager& _sessionManager;

    /// Parsing e dispatch di un comando JSON ricevuto dal browser.
    void processCommand(const std::string& json);
};

/**
 * Gestisce la lista globale dei WebSocket connessi e l'invio broadcast
 * degli aggiornamenti di stato.
 */
class WebSocketBroadcaster {
public:
    static WebSocketBroadcaster& instance();

    /// Registra un WebSocket connesso.
    void addSocket(Poco::Net::WebSocket* ws);

    /// Rimuove un WebSocket disconnesso.
    void removeSocket(Poco::Net::WebSocket* ws);

    /// Invia un aggiornamento di stato a tutti i browser connessi.
    void sendStatusUpdate(const SessionManager::ChargePointStatus& status);

    /// Invia un evento di log a tutti i browser connessi.
    void sendLogEvent(const std::string& level, const std::string& message);

private:
    WebSocketBroadcaster() = default;

    std::vector<Poco::Net::WebSocket*> _sockets;
    mutable Poco::Mutex _mutex;

    /// Invia una stringa JSON a tutti i client connessi, rimuovendo quelli disconnessi.
    void broadcast(const std::string& json);
};

#endif // WEBSOCKETHANDLER_H
