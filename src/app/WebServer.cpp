/**
 * WebServer — Implementazione del server HTTP con supporto file statici e WebSocket.
 *
 * La RequestHandlerFactory decide per ogni richiesta se creare:
 *   - Un WebSocketHandler (se la richiesta contiene l'header Upgrade: websocket)
 *   - Un StaticFileHandler (per tutte le altre richieste HTTP)
 *
 */
#include "app/WebServer.h"
#include "app/WebSocketHandler.h"

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Path.h>
#include <Poco/File.h>
#include <Poco/StreamCopier.h>
#include <Poco/Logger.h>

#include <fstream>

using Poco::Logger;

// ---------------------------------------------------------------------------
// StaticFileHandler
// ---------------------------------------------------------------------------

StaticFileHandler::StaticFileHandler(const std::string& webRoot)
    : _webRoot(webRoot)
{
}

void StaticFileHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                      Poco::Net::HTTPServerResponse& response)
{
    Logger& logger = Logger::get("WebServer");

    std::string uri = request.getURI();
    if (uri == "/") {
        uri = "/index.html";
    }

    // Prevenire path traversal
    if (uri.find("..") != std::string::npos) {
        response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
        response.send() << "Forbidden";
        return;
    }

    Poco::Path filePath(_webRoot);
    filePath.append(uri);
    std::string fullPath = filePath.toString();

    Poco::File file(fullPath);
    if (!file.exists() || !file.isFile()) {
        logger.warning("File not found: %s", fullPath);
        response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        response.send() << "Not Found";
        return;
    }

    std::string mimeType = getMimeType(fullPath);
    response.setContentType(mimeType);
    response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);

    std::ifstream ifs(fullPath, std::ios::binary);
    Poco::StreamCopier::copyStream(ifs, response.send());
}

std::string StaticFileHandler::getMimeType(const std::string& path) const
{
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css")  return "text/css";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js")   return "application/javascript";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png")  return "image/png";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")  return "image/svg+xml";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

// ---------------------------------------------------------------------------
// RequestHandlerFactory
// ---------------------------------------------------------------------------

RequestHandlerFactory::RequestHandlerFactory(const std::string& webRoot, ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq)
    : _webRoot(webRoot)
	, _eventQueue(q)
	, _uiQueue(uq)
{
}

Poco::Net::HTTPRequestHandler* RequestHandlerFactory::createRequestHandler(
    const Poco::Net::HTTPServerRequest& request)
{
    // Se la richiesta contiene l'header Upgrade: websocket → WebSocketHandler
    if (request.find("Upgrade") != request.end() &&
        Poco::icompare(request["Upgrade"], "websocket") == 0) {
        return new WebSocketHandler(_eventQueue, _uiQueue);
    }

    // Altrimenti → file statico
    return new StaticFileHandler(_webRoot);
}

// ---------------------------------------------------------------------------
// WebServer
// ---------------------------------------------------------------------------

WebServer::WebServer(int port, const std::string& webRoot, ThreadSafeQueue<SessionEvent>* q, ThreadSafeQueue<std::string>* uq)
    : _port(port)
    , _webRoot(webRoot)
	, _eventQueue(q)
	, _uiQueue(uq)
{
}

WebServer::~WebServer()
{
    stop();
}

void WebServer::start()
{
    Logger& logger = Logger::get("WebServer");

    try {
        Poco::Net::ServerSocket svs(_port);
        auto* params = new Poco::Net::HTTPServerParams;
        params->setMaxQueued(16);
        params->setMaxThreads(4);

        _httpServer = std::make_unique<Poco::Net::HTTPServer>(
            new RequestHandlerFactory(_webRoot, _eventQueue, _uiQueue),
            svs,
            params
        );

        _httpServer->start();
        logger.debug("WebServer started on port %d, serving files from %s",
                           _port, _webRoot);

    } catch (Poco::Exception& ex) {
        logger.error("Failed to start WebServer on port %d: %s", _port, ex.displayText());
        throw;
    }
}

void WebServer::stop()
{
    if (_httpServer) {
        _httpServer->stop();
        Logger& logger = Logger::get("WebServer");
        logger.debug("WebServer stopped");
    }
}
