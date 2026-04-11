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

#include "common/ThreadSafeQueue.h"
#include "common/SessionEvent.h"
#include "common/CentralSystemEvent.h"
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
#include <Poco/Runnable.h>
#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>

using Poco::Logger;

class OcppClient16J : public ProtocolAdapter, public Poco::Runnable  {
public:
    /**
     * Costruttore.
     * @param centralSystemUrl  URL WebSocket del Central_System (es. ws://host:port/path)
     * @param chargePointId     Identificativo della colonnina
     */
    OcppClient16J(const std::string& centralSystemUrl,
                  const std::string& chargePointId,
				  ThreadSafeQueue<SessionEvent>* eventQ,
				  ThreadSafeQueue<CentralSystemEvent>* csysQueue);

    ~OcppClient16J() override;

    void start() override;
    void stop() override;
    void run() override;

private:
    std::string _url;
    std::string _chargePointId;
    std::string _host;
    Poco::UInt16 _port;
    std::string _path;
    std::unique_ptr<Poco::Net::WebSocket> _ws;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _running{false};
    Logger& _logger = Logger::get("OcppClient");
    // Heartbeat timer
    Poco::Timer _heartbeatTimer;
    bool _heartbeatActive ;
    // Unique ID counter
    std::atomic<int> _uniqueIdCounter;
    // Pending CALL tracking: uniqueId → action name
    std::map<std::string, std::string> _pendingCalls;
    Poco::Thread _thread;
    ThreadSafeQueue<SessionEvent>* _eventQueue;
    ThreadSafeQueue<CentralSystemEvent>* _csysQueue;

    // --- Internal methods ---
    bool tryConnect();

    void closeSocket();

    void eventLoop();

    void handleDisconnect();

    /// Parsa l'URL WebSocket in host, port, path.
    void parseUrl(const std::string& url);

    /// Invia un messaggio CALL e registra il uniqueId come pending.
    void sendCall(const std::string& action, const Poco::JSON::Object& payload);

    /// Invia dati raw sul WebSocket (thread-safe).
    void sendRaw(const std::string& data);

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

    /// Avvia il timer Heartbeat con l'intervallo specificato.
    void startHeartbeat(int intervalSeconds);

    /// Ferma il timer Heartbeat.
    void stopHeartbeat();

    /// Notifica il cambio stato connessione ai listener.
    void notifyConnectionStatus(bool connected);

    void sendBootNotification(
        const std::string& chargePointModel,
        const std::string& chargePointVendor);

    void sendHeartbeat();

    void sendAuthorize(const std::string& idTag);

    void sendStatusNotification(
        int connectorId,
        const std::string& status,
        const std::string& errorCode);

    void sendStartTransaction(
        int connectorId,
        const std::string& idTag,
        int meterStart);

    void sendMeterValues(
        int connectorId,
        int transactionId,
        int meterValue);

    void sendStopTransaction(
        int transactionId,
        int meterStop,
        const std::string& reason);

    void sendCallResult(
        const std::string& uniqueId,
        const Poco::JSON::Object& payload);
};

#endif // OCPPCLIENT16J_H
