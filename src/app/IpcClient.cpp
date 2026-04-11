#include "app/IpcClient.h"
#include "common/IpcMessage.h"

#include <Poco/Net/SocketAddress.h>
#include <Poco/JSON/Parser.h>

#include <sstream>
#include <thread>

// ------------------------------------------------------------

IpcClient::IpcClient(const std::string& socketPath,
		ThreadSafeQueue<std::string>* iq,
		ThreadSafeQueue<SessionEvent>* oq)
    : _socketPath(socketPath)
	, _inQueue(iq)
	, _outQueue(oq)
{
	 if (!_inQueue) {
		 throw std::invalid_argument("inQueue non può essere nullptr");
	 }

	 if (!_outQueue) {
		 throw std::invalid_argument("outQueue non può essere nullptr");
	 }

}

IpcClient::~IpcClient() {
    stop();
}

// ------------------------------------------------------------

void IpcClient::start() {
    if (_running) return;
    _running = true;
    _thread.start(*this);
}

void IpcClient::stop() {
    if (!_running) return;

    _running = false;

    closeSocket();

    try { _thread.join(); } catch (...) {}
}

// ------------------------------------------------------------

bool IpcClient::isConnected() const {
    return _connected;
}

// ------------------------------------------------------------

void IpcClient::run() {

    while (_running) {

        // --- CONNECT ---
        if (!_connected) {
            if (tryConnect()) {
                _logger.information("IPC connected");
                notifyConnectionStatus(true);
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }

        try {
            // --- READ ---
            if (_socket.poll(Poco::Timespan(0, 100000), Poco::Net::Socket::SELECT_READ)) {
                handleRead();
            }

            // --- WRITE ---
            handleWrite();

        } catch (Poco::Exception& e) {
            _logger.warning("IPC error: %s", e.displayText());

            closeSocket();
            _connected = false;
            notifyConnectionStatus(false);

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}

// ------------------------------------------------------------

bool IpcClient::tryConnect() {

    try {
        Poco::Net::SocketAddress addr(_socketPath);
        _socket = Poco::Net::StreamSocket();
        _socket.connect(addr);

        _connected = true;
        return true;

    } catch (Poco::Exception& e) {
        _logger.debug("Connect failed: %s", e.displayText());
        return false;
    }
}

// ------------------------------------------------------------

void IpcClient::closeSocket() {
    try { _socket.close(); } catch (...) {}
}

// ------------------------------------------------------------

void IpcClient::handleRead() {
    char chunk[1024];

    int n = _socket.receiveBytes(chunk, sizeof(chunk));

    if (n <= 0) {
        throw Poco::IOException("IPC disconnected");
    }

    _buffer.append(chunk, n);

    size_t pos;
    while ((pos = _buffer.find('\n')) != std::string::npos) {
        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 1);

        if (!line.empty())
            processLine(line);
    }
}

// ------------------------------------------------------------

void IpcClient::handleWrite() {

    while (auto msg = _inQueue->try_pop()) {
    	_logger.information("Command from SessionManager: %s", msg.value());
    	std::string& message = msg.value();
    	message += '\n';
        _socket.sendBytes(message.data(), (int)message.size());
    }
}

// ------------------------------------------------------------

void IpcClient::notifyConnectionStatus(bool connected) {

    SessionEvent evt;
    evt.type = SessionEvent::Type::FirmwareConnection;
    evt.boolParam = connected;

    _outQueue->push(std::move(evt));
}

// ------------------------------------------------------------

void IpcClient::processLine(const std::string& line) {

    _logger.information("Message from firmware: %s",line);

    Poco::JSON::Object::Ptr obj;

    try {
        obj = IpcMessage::parseJson(line);
    } catch (...) {
        _logger.error("Invalid JSON: %s", line);
        return;
    }

    if (!obj->has("type")) return;

    std::string type = obj->getValue<std::string>("type");

    SessionEvent evt;

    if (type == IpcMessage::TYPE_CONNECTOR_STATE) {
        evt.type = SessionEvent::Type::ConnectorStateChanged;
        evt.stringParam = obj->getValue<std::string>("state");

    } else if (type == IpcMessage::TYPE_METER_VALUE) {
        evt.type = SessionEvent::Type::MeterValue;
        evt.intParam = obj->getValue<int>("value");

    } else if (type == IpcMessage::TYPE_ERROR) {
        evt.type = SessionEvent::Type::Error;
        evt.stringParam = obj->getValue<std::string>("errorType");

    } else if (type == IpcMessage::TYPE_ERROR_CLEARED) {
        evt.type = SessionEvent::Type::ErrorCleared;

    } else {
        return;
    }

    _outQueue->push(std::move(evt));
}
