/**
 * IpcServer — Implementazione del server IPC su Unix Domain Socket.
 *
 * Il server crea un Unix Domain Socket al percorso configurato, attende
 * la connessione dell'Application_Layer (un solo client alla volta),
 * e scambia messaggi JSON delimitati da newline.
 *
 * Thread:
 *   - acceptThread: attende connessioni in ingresso
 *   - readThread:   legge messaggi dal client connesso
 *
 * Gestione errori:
 *   - JSON malformato → scartato, log Error
 *   - Tipo messaggio sconosciuto → ignorato, log Warning
 *
 * Requisiti validati: 1.2, 1.4
 */
#include "firmware/IpcServer.h"
#include "common/IpcMessage.h"

#include <Poco/Net/SocketAddress.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Exception.h>
#include <Poco/Logger.h>
#include <Poco/File.h>

#include <sstream>

using Poco::Logger;

// ---- AcceptRunnable ---------------------------------------------------------

IpcServer::AcceptRunnable::AcceptRunnable(IpcServer& server)
    : _server(server)
{
}

void IpcServer::AcceptRunnable::run()
{
    _server.acceptLoop();
}

// ---- ReadRunnable -----------------------------------------------------------

IpcServer::ReadRunnable::ReadRunnable(IpcServer& server)
    : _server(server)
{
}

void IpcServer::ReadRunnable::run()
{
    _server.readLoop();
}

// ---- IpcServer --------------------------------------------------------------

IpcServer::IpcServer(const std::string& socketPath)
    : _socketPath(socketPath)
    , _running(false)
    , _clientConnected(false)
    , _acceptRunnable(*this)
    , _readRunnable(*this)
{
}

IpcServer::~IpcServer()
{
    stop();
}

void IpcServer::start()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_running) return;

    removeSocketFile();

    Logger& logger = Logger::get("IpcServer");
    logger.information("Starting IPC server on %s", _socketPath);

    Poco::Net::SocketAddress addr(_socketPath);
    _serverSocket = Poco::Net::ServerSocket(addr);
    _running = true;

    _acceptThread.start(_acceptRunnable);
}

void IpcServer::stop()
{
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (!_running) return;
        _running = false;
    }

    Logger& logger = Logger::get("IpcServer");
    logger.information("Stopping IPC server");

    try { _serverSocket.close(); } catch (...) {}

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (_clientConnected) {
            try { _clientSocket.close(); } catch (...) {}
            _clientConnected = false;
        }
    }

    if (_acceptThread.isRunning()) {
        try { _acceptThread.join(2000); } catch (...) {}
    }
    if (_readThread.isRunning()) {
        try { _readThread.join(2000); } catch (...) {}
    }

    removeSocketFile();
}

void IpcServer::sendMessage(const Poco::JSON::Object& msg)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (!_clientConnected) return;

    std::ostringstream oss;
    msg.stringify(oss);
    std::string data = oss.str() + "\n";

    try {
        _clientSocket.sendBytes(data.data(), static_cast<int>(data.size()));
    } catch (Poco::Exception& e) {
        Logger& logger = Logger::get("IpcServer");
        logger.error("Failed to send IPC message: %s", e.displayText());
        _clientConnected = false;
    }
}

void IpcServer::setMessageCallback(MessageCallback cb)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _messageCallback = cb;
}

void IpcServer::acceptLoop()
{
    Logger& logger = Logger::get("IpcServer");

    while (_running) {
        try {
            Poco::Timespan timeout(1, 0); // 1 second
            if (_serverSocket.poll(timeout, Poco::Net::Socket::SELECT_READ)) {
                Poco::Net::StreamSocket client = _serverSocket.acceptConnection();
                {
                    Poco::Mutex::ScopedLock lock(_mutex);
                    // Close previous client if any
                    if (_clientConnected) {
                        try { _clientSocket.close(); } catch (...) {}
                        if (_readThread.isRunning()) {
                            try { _readThread.join(1000); } catch (...) {}
                        }
                    }
                    _clientSocket = client;
                    _clientConnected = true;
                }
                logger.information("Application_Layer connected via IPC");
                _readThread.start(_readRunnable);
            }
        } catch (Poco::Exception& e) {
            if (_running) {
                logger.error("Accept error: %s", e.displayText());
            }
        }
    }
}

void IpcServer::readLoop()
{
    Logger& logger = Logger::get("IpcServer");
    std::string buffer;
    char chunk[1024];

    while (_running) {
        bool connected;
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            connected = _clientConnected;
        }
        if (!connected) break;

        try {
            Poco::Timespan timeout(1, 0); // 1 second
            if (!_clientSocket.poll(timeout, Poco::Net::Socket::SELECT_READ))
                continue;

            int n = _clientSocket.receiveBytes(chunk, sizeof(chunk));
            if (n <= 0) {
                logger.information("Application_Layer disconnected");
                Poco::Mutex::ScopedLock lock(_mutex);
                _clientConnected = false;
                break;
            }

            buffer.append(chunk, n);

            // Process complete lines (newline-delimited JSON)
            std::string::size_type pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty()) {
                    processLine(line);
                }
            }
        } catch (Poco::Exception& e) {
            if (_running) {
                logger.error("Read error: %s", e.displayText());
                Poco::Mutex::ScopedLock lock(_mutex);
                _clientConnected = false;
            }
            break;
        }
    }
}

void IpcServer::processLine(const std::string& line)
{
    Logger& logger = Logger::get("IpcServer");
    logger.information("Command from Application_Layer: %s", line);

    // Parse JSON
    Poco::JSON::Object::Ptr obj;
    try {
        obj = IpcMessage::parseJson(line);
    } catch (Poco::Exception& e) {
        logger.error("Malformed IPC message (discarded): %s — %s", line, e.displayText());
        return;
    }

    // Check "type" field
    if (!obj->has("type")) {
        logger.error("IPC message missing 'type' field (discarded): %s", line);
        return;
    }

    std::string type = obj->getValue<std::string>("type");

    // Validate known types
    if (type != IpcMessage::TYPE_COMMAND &&
        type != IpcMessage::TYPE_CONNECTOR_STATE &&
        type != IpcMessage::TYPE_METER_VALUE &&
        type != IpcMessage::TYPE_ERROR &&
        type != IpcMessage::TYPE_ERROR_CLEARED)
    {
        logger.warning("Unknown IPC message type '%s' (ignored)", type);
        return;
    }

    // Deliver to callback
    MessageCallback cb;
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        cb = _messageCallback;
    }
    if (cb) {
        try {
            cb(*obj);
        } catch (Poco::Exception& e) {
            logger.error("Error in message callback: %s", e.displayText());
        }
    }
}

void IpcServer::removeSocketFile()
{
    try {
        Poco::File f(_socketPath);
        if (f.exists()) {
            f.remove();
        }
    } catch (...) {}
}
