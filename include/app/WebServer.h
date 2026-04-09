/**
 * WebServer — Server HTTP che serve file statici e gestisce WebSocket per i browser.
 *
 * Responsabilità:
 *   - Avviare un Poco::Net::HTTPServer sulla porta configurata
 *   - Servire file statici dalla directory web/ (index.html, style.css, app.js)
 *   - Effettuare l'upgrade a WebSocket per le richieste appropriate
 *   - Collegare il SessionManager al WebSocketBroadcaster per gli aggiornamenti UI
 *
 * Requisiti validati: 6.1, 6.5
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "common/SessionEvent.h"
#include "common/ThreadSafeQueue.h"

#include <string>
#include <memory>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

/**
 * Handler per le richieste HTTP di file statici.
 */
class StaticFileHandler : public Poco::Net::HTTPRequestHandler {
public:
    explicit StaticFileHandler(const std::string& webRoot);

    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;

private:
    std::string _webRoot;

    /// Restituisce il MIME type in base all'estensione del file.
    std::string getMimeType(const std::string& path) const;
};

/**
 * Factory che crea l'handler appropriato (WebSocket o file statico)
 * in base alla richiesta HTTP.
 */
class RequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    RequestHandlerFactory(const std::string& webRoot, ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq);

    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;


private:
    std::string _webRoot;
    ThreadSafeQueue<SessionEvent>* _eventQueue = nullptr;
    ThreadSafeQueue<std::string>* _uiQueue = nullptr;
};

/**
 * Server HTTP principale.
 */
class WebServer {
public:
    /**
     * Costruttore.
     * @param port            Porta HTTP su cui ascoltare
     * @param webRoot         Percorso della directory con i file statici (web/)
     * @param sessionManager  Riferimento al SessionManager per i comandi e lo stato
     */
    WebServer(int port, const std::string& webRoot, ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq);

    ~WebServer();

    /// Avvia il server HTTP e collega il SessionManager al broadcaster.
    void start();

    /// Ferma il server HTTP.
    void stop();

private:
    int _port;
    std::string _webRoot;
    std::unique_ptr<Poco::Net::HTTPServer> _httpServer;
    ThreadSafeQueue<SessionEvent>* _eventQueue = nullptr;
    ThreadSafeQueue<std::string>* _uiQueue = nullptr;
};

#endif // WEBSERVER_H
