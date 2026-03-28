/**
 * IIpcSender — Interfaccia minimale per l'invio di messaggi IPC.
 *
 * Disaccoppia SessionManager dall'implementazione concreta di IpcClient,
 * permettendo il mocking nei test senza istanziare socket reali.
 */
#ifndef IIPCSENDER_H
#define IIPCSENDER_H

#include <Poco/JSON/Object.h>

class IIpcSender {
public:
    virtual ~IIpcSender() = default;
    virtual void sendMessage(const Poco::JSON::Object& msg) = 0;
};

#endif // IIPCSENDER_H
