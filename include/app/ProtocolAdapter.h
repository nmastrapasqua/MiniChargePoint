#ifndef PROTOCOL_ADAPTER_H
#define PROTOCOL_ADAPTER_H

// ProtocolAdapter — Interfaccia astratta che disaccoppia la logica applicativa
// dal protocollo di comunicazione specifico (OCPP 1.6J). Permette di sostituire
// il protocollo senza modificare il resto del sistema.
// Requisiti: 5.1, 5.2

#include <string>
#include <functional>
#include <Poco/JSON/Object.h>

class ProtocolAdapter {
public:
    virtual ~ProtocolAdapter() = default;

    // Gestione connessione
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // Messaggi OCPP
    virtual void sendBootNotification(
        const std::string& chargePointModel,
        const std::string& chargePointVendor) = 0;

    virtual void sendHeartbeat() = 0;

    virtual void sendAuthorize(const std::string& idTag) = 0;

    virtual void sendStatusNotification(
        int connectorId,
        const std::string& status,
        const std::string& errorCode) = 0;

    virtual void sendStartTransaction(
        int connectorId,
        const std::string& idTag,
        int meterStart) = 0;

    virtual void sendMeterValues(
        int connectorId,
        int transactionId,
        int meterValue) = 0;

    virtual void sendStopTransaction(
        int transactionId,
        int meterStop,
        const std::string& reason) = 0;

    // Callback per cambio stato connessione (true = connesso, false = disconnesso)
    using ConnectionStatusCallback = std::function<void(bool connected)>;
    virtual void setConnectionStatusCallback(ConnectionStatusCallback cb) = 0;

    // Callback per risposte dal Central_System
    using ResponseCallback = std::function<void(const Poco::JSON::Object&)>;
    virtual void setResponseCallback(ResponseCallback cb) = 0;

    // Callback per comandi remoti dal Central_System (CALL in ingresso)
    using RemoteCommandCallback = std::function<void(
        const std::string& action,
        const Poco::JSON::Object& payload,
        const std::string& uniqueId)>;
    virtual void setRemoteCommandCallback(RemoteCommandCallback cb) = 0;

    // Risposta a un comando remoto
    virtual void sendCallResult(
        const std::string& uniqueId,
        const Poco::JSON::Object& payload) = 0;
};

#endif // PROTOCOL_ADAPTER_H
