/**
 * OcppClient16J — Implementazione di ProtocolAdapter per OCPP 1.6J su WebSocket.
 *
 * Responsabilità:
 *   - Stabilire e mantenere la connessione WebSocket con il Central_System
 *   - Serializzare e inviare messaggi OCPP (BootNotification, Heartbeat,
 *     Authorize, StatusNotification, StartTransaction, MeterValues, StopTransaction)
 *   - Ricevere e deserializzare risposte CALLRESULT/CALLERROR e CALL in ingresso
 *   - Gestire Heartbeat periodico dopo BootNotification Accepted
 *   - Riconnessione automatica ogni 10 secondi se connessione persa
 *   - Gestire comandi remoti (RemoteStartTransaction, RemoteStopTransaction)
 *
 * Requisiti validati: 3.1–3.15, 5.3, 9.3
 */
#ifndef OCPPCLIENT16J_H
#define OCPPCLIENT16J_H

#include "app/ProtocolAdapter.h"
#include "app/OcppMessageSerializer16J.h"

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <atomic>

#include <Poco/Net/WebSocket.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Thread.h>
#include <Poco/Timer.h>
#include <Poco/Mutex.h>
#include <Poco/Runnable.h>
#include <Poco/JSON/Object.h>

class OcppClient16J : public ProtocolAdapter {
public:
    /**
     * Costruttore.
     * @param centralSystemUrl  URL WebSocket del Central_System (es. ws://host:port/path)
     * @param chargePointId     Identificativo della colonnina
     */
    OcppClient16J(const std::string& centralSystemUrl,
                  const std::string& chargePointId);

    ~OcppClient16J() override;

    // --- ProtocolAdapter interface ---
    void connect() override;
    void disconnect() override;
    bool isConnected() const override;

    void sendBootNotification(
        const std::string& chargePointModel,
        const std::string& chargePointVendor) override;

    void sendHeartbeat() override;

    void sendAuthorize(const std::string& idTag) override;

    void sendStatusNotification(
        int connectorId,
        const std::string& status,
        const std::string& errorCode) override;

    void sendStartTransaction(
        int connectorId,
        const std::string& idTag,
        int meterStart) override;

    void sendMeterValues(
        int connectorId,
        int transactionId,
        int meterValue) override;

    void sendStopTransaction(
        int transactionId,
        int meterStop,
        const std::string& reason) override;

    void setResponseCallback(ResponseCallback cb) override;
    void setRemoteCommandCallback(RemoteCommandCallback cb) override;
    void setConnectionStatusCallback(ConnectionStatusCallback cb) override;

    void sendCallResult(
        const std::string& uniqueId,
        const Poco::JSON::Object& payload) override;

private:
    std::string _url;
    std::string _chargePointId;
    std::string _host;
    Poco::UInt16 _port;
    std::string _path;

    std::unique_ptr<Poco::Net::HTTPClientSession> _session;
    std::unique_ptr<Poco::Net::WebSocket> _ws;

    std::atomic<bool> _connected;
    std::atomic<bool> _running;

    ResponseCallback _responseCallback;
    RemoteCommandCallback _remoteCommandCallback;
    ConnectionStatusCallback _connectionStatusCallback;
    mutable Poco::Mutex _mutex;
    mutable Poco::Mutex _wsMutex;  // protects WebSocket send

    // Heartbeat timer
    Poco::Timer _heartbeatTimer;
    int _heartbeatInterval;  // seconds, from BootNotification.conf

    // Reconnect timer
    Poco::Timer _reconnectTimer;

    // Unique ID counter
    std::atomic<int> _uniqueIdCounter;

    // Pending CALL tracking: uniqueId → action name
    std::map<std::string, std::string> _pendingCalls;
    Poco::Mutex _pendingMutex;

    // --- Receive thread ---
    class ReceiveRunnable : public Poco::Runnable {
    public:
        explicit ReceiveRunnable(OcppClient16J& client);
        void run() override;
    private:
        OcppClient16J& _client;
    };

    ReceiveRunnable _receiveRunnable;
    Poco::Thread _receiveThread;

    // --- Internal methods ---

    /// Parsa l'URL WebSocket in host, port, path.
    void parseUrl(const std::string& url);

    /// Invia un messaggio CALL e registra il uniqueId come pending.
    void sendCall(const std::string& action, const Poco::JSON::Object& payload);

    /// Invia dati raw sul WebSocket (thread-safe).
    void sendRaw(const std::string& data);

    /// Loop di ricezione messaggi dal Central_System.
    void receiveLoop();

    /// Gestisce un CALLRESULT ricevuto.
    void handleCallResult(const std::string& uniqueId,
                          const Poco::JSON::Object& payload);

    /// Gestisce un CALLERROR ricevuto.
    void handleCallError(const std::string& uniqueId,
                         const std::string& errorCode,
                         const std::string& errorDescription);

    /// Gestisce un CALL in ingresso dal Central_System.
    void handleIncomingCall(const std::string& uniqueId,
                            const std::string& action,
                            const Poco::JSON::Object& payload);

    /// Genera un uniqueId univoco per i messaggi CALL.
    std::string generateUniqueId();

    /// Callback del timer Heartbeat.
    void onHeartbeatTimer(Poco::Timer& timer);

    /// Callback del timer di riconnessione.
    void onReconnectTimer(Poco::Timer& timer);

    /// Avvia il timer Heartbeat con l'intervallo specificato.
    void startHeartbeat(int intervalSeconds);

    /// Ferma il timer Heartbeat.
    void stopHeartbeat();

    /// Avvia il timer di riconnessione (ogni 10 secondi).
    void startReconnect();

    /// Notifica il cambio stato connessione ai listener.
    void notifyConnectionStatus(bool connected);
};

#endif // OCPPCLIENT16J_H
