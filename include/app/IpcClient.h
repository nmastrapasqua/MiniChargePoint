#ifndef IPCCLIENT_H
#define IPCCLIENT_H

#include "common/ThreadSafeQueue.h"
#include "common/SessionEvent.h"

#include <Poco/Net/StreamSocket.h>
#include <Poco/Thread.h>
#include <Poco/Runnable.h>
#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>

#include <string>
#include <atomic>

class IpcClient : public Poco::Runnable {
public:
    explicit IpcClient(const std::string& socketPath,
    		ThreadSafeQueue<std::string>* iq,
    		ThreadSafeQueue<SessionEvent>* oq);
    ~IpcClient();

    void start();
    void stop();

    bool isConnected() const;

    void run() override;

private:
    std::string _socketPath;

    Poco::Net::StreamSocket _socket;

    std::atomic<bool> _running{false};
    std::atomic<bool> _connected{false};

    Poco::Thread _thread;

    ThreadSafeQueue<std::string>* _inQueue = nullptr;

    ThreadSafeQueue<SessionEvent>* _outQueue = nullptr;

    std::string _buffer;

    Poco::Logger& _logger;

    // --- internal ---
    bool tryConnect();
    void closeSocket();
    void notifyConnectionStatus(bool connected);

    void handleRead();
    void handleWrite();
    void processLine(const std::string& line);

};

#endif
