/**
 * IpcClient — Implementazione del client IPC su Unix Domain Socket.
 *
 * Il client si connette al Unix Domain Socket creato dal Firmware_Layer,
 * scambia messaggi JSON delimitati da newline, e tenta la riconnessione
 * automatica ogni 5 secondi se la connessione viene persa.
 *
 * Thread:
 *   - readThread: legge messaggi dal Firmware_Layer
 *
 * Timer:
 *   - reconnectTimer: tenta la riconnessione ogni 5 secondi
 *
 * Gestione errori:
 *   - JSON malformato → scartato, log Error
 *   - Tipo messaggio sconosciuto → ignorato, log Warning
 *   - Connessione persa → log Warning, avvio riconnessione automatica
 *
 * Requisiti validati: 1.3, 1.4, 1.5
 */
#include "app/IpcClient.h"
#include "common/IpcMessage.h"

#include <Poco/Net/SocketAddress.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Exception.h>
#include <Poco/Logger.h>
#include <Poco/Delegate.h>

#include <sstream>

using Poco::Logger;

// ---- ReadRunnable -----------------------------------------------------------

IpcClient::ReadRunnable::ReadRunnable(IpcClient& client)
    : _client(client)
{
}

void IpcClient::ReadRunnable::run()
{
    _client.readLoop();
}

// ---- IpcClient --------------------------------------------------------------

IpcClient::IpcClient(const std::string& socketPath)
    : _socketPath(socketPath)
    , _connected(false)
    , _running(false)
    , _readRunnable(*this)
    , _reconnectTimer(0, 0)  // inactive until started
{
}

IpcClient::~IpcClient()
{
    disconnect();
}

void IpcClient::connect()
{
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (_running) return;
        _running = true;
    }

    Logger& logger = Logger::get("IpcClient");
    logger.information("Connecting to IPC server at %s", _socketPath);

    if (tryConnect()) {
        startReadThread();
    } else {
        // Start reconnection timer (5 second interval)
        logger.warning("IPC connection failed, will retry every 5 seconds");
        _reconnectTimer.setStartInterval(5000);
        _reconnectTimer.setPeriodicInterval(5000);
        Poco::TimerCallback<IpcClient> cb(*this, &IpcClient::onReconnect);
        _reconnectTimer.start(cb);
    }
}

void IpcClient::disconnect()
{
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (!_running) return;
        _running = false;
    }

    Logger& logger = Logger::get("IpcClient");
    logger.information("Disconnecting IPC client");

    // Stop reconnection timer
    try { _reconnectTimer.restart(0); } catch (...) {}

    // Close socket
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (_connected) {
            try { _socket.close(); } catch (...) {}
            _connected = false;
        }
    }

    stopReadThread();
}

bool IpcClient::isConnected() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return _connected;
}

void IpcClient::sendMessage(const Poco::JSON::Object& msg)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (!_connected) return;

    std::ostringstream oss;
    msg.stringify(oss);
    std::string data = oss.str() + "\n";

    try {
        _socket.sendBytes(data.data(), static_cast<int>(data.size()));
    } catch (Poco::Exception& e) {
        Logger& logger = Logger::get("IpcClient");
        logger.error("Failed to send IPC message: %s", e.displayText());
        _connected = false;
    }
}

void IpcClient::setMessageCallback(MessageCallback cb)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _messageCallback = cb;
}

bool IpcClient::tryConnect()
{
    Logger& logger = Logger::get("IpcClient");
    try {
        Poco::Net::SocketAddress addr(_socketPath);
        _socket = Poco::Net::StreamSocket();
        _socket.connect(addr);
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            _connected = true;
        }
        logger.information("Connected to IPC server at %s", _socketPath);
        return true;
    } catch (Poco::Exception& e) {
        logger.debug("IPC connection attempt failed: %s", e.displayText());
        return false;
    }
}

void IpcClient::startReadThread()
{
    if (!_readThread.isRunning()) {
        _readThread.start(_readRunnable);
    }
}

void IpcClient::stopReadThread()
{
    if (_readThread.isRunning()) {
        try { _readThread.join(2000); } catch (...) {}
    }
}

void IpcClient::onReconnect(Poco::Timer& /*timer*/)
{
    bool running;
    bool connected;
    {
        Poco::Mutex::ScopedLock lock(_mutex);
        running = _running;
        connected = _connected;
    }
    if (!running || connected) return;

    Logger& logger = Logger::get("IpcClient");
    logger.warning("Attempting IPC reconnection to %s", _socketPath);

    if (tryConnect()) {
        // Stop the reconnection timer
        try { _reconnectTimer.restart(0); } catch (...) {}
        startReadThread();
    }
}

void IpcClient::readLoop()
{
    Logger& logger = Logger::get("IpcClient");
    std::string buffer;
    char chunk[1024];

    while (true) {
        bool running;
        bool connected;
        {
            Poco::Mutex::ScopedLock lock(_mutex);
            running = _running;
            connected = _connected;
        }
        if (!running || !connected) break;

        try {
            Poco::Timespan timeout(1, 0); // 1 second
            if (!_socket.poll(timeout, Poco::Net::Socket::SELECT_READ))
                continue;

            int n = _socket.receiveBytes(chunk, sizeof(chunk));
            if (n <= 0) {
                logger.warning("IPC connection lost (Firmware_Layer disconnected)");
                {
                    Poco::Mutex::ScopedLock lock(_mutex);
                    _connected = false;
                }
                // Start reconnection timer if still running
                bool stillRunning;
                {
                    Poco::Mutex::ScopedLock lock(_mutex);
                    stillRunning = _running;
                }
                if (stillRunning) {
                    _reconnectTimer.setStartInterval(5000);
                    _reconnectTimer.setPeriodicInterval(5000);
                    Poco::TimerCallback<IpcClient> cb(*this, &IpcClient::onReconnect);
                    _reconnectTimer.start(cb);
                }
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
            bool stillRunning;
            {
                Poco::Mutex::ScopedLock lock(_mutex);
                stillRunning = _running;
                _connected = false;
            }
            if (stillRunning) {
                logger.warning("IPC read error: %s — will attempt reconnection", e.displayText());
                _reconnectTimer.setStartInterval(5000);
                _reconnectTimer.setPeriodicInterval(5000);
                Poco::TimerCallback<IpcClient> cb(*this, &IpcClient::onReconnect);
                _reconnectTimer.start(cb);
            }
            break;
        }
    }
}

void IpcClient::processLine(const std::string& line)
{
    Logger& logger = Logger::get("IpcClient");

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
