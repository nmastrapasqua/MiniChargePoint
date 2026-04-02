#ifndef IPCCLIENT_H
#define IPCCLIENT_H

#include <string>
#include <atomic>

#include <Poco/Net/StreamSocket.h>
#include <Poco/Thread.h>
#include <Poco/Runnable.h>
#include <Poco/JSON/Object.h>

//#include "common/IIpcSender.h"
#include "common/ThreadSafeQueue.h"
#include "common/SessionEvent.h"

class IpcClient : /* public IIpcSender, */ public Poco::Runnable {
public:
    explicit IpcClient(const std::string& socketPath);
    ~IpcClient();

    void start();
    void stop();

    bool isConnected() const;

    //void sendMessage(const Poco::JSON::Object& msg) override;

    void setInQueue(ThreadSafeQueue<std::string>* q) { _inQueue = q; }

    void setOutQueue(ThreadSafeQueue<SessionEvent>* q) { _outQueue = q; }

    void run() override;

private:
    std::string _socketPath;

    Poco::Net::StreamSocket _socket;

    std::atomic<bool> _running{false};
    std::atomic<bool> _connected{false};

    Poco::Thread _thread;

    ThreadSafeQueue<std::string>* _inQueue = nullptr;

    ThreadSafeQueue<SessionEvent>* _outQueue = nullptr;

    // --- internal ---
    bool tryConnect();
    void closeSocket();
    void notifyConnectionStatus(bool connected);

    void handleRead();
    void handleWrite();
    void processLine(const std::string& line);

    std::string _buffer;
};

#endif
