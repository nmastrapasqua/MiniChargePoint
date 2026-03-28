/**
 * IpcClient — Client IPC su Unix Domain Socket per l'Application_Layer.
 *
 * Responsabilità:
 *   - Connettersi al Unix Domain Socket esposto dal Firmware_Layer
 *   - Inviare messaggi JSON (delimitati da newline) al Firmware_Layer
 *   - Ricevere messaggi JSON dal Firmware_Layer e notificarli tramite callback
 *   - Riconnessione automatica ogni 5 secondi se connessione persa (log Warning)
 *
 * Thread dedicato per lettura; messaggi JSON delimitati da newline.
 *
 * Requisiti validati: 1.3, 1.4, 1.5
 */
#ifndef IPCCLIENT_H
#define IPCCLIENT_H

#include <string>
#include <functional>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Thread.h>
#include <Poco/Timer.h>
#include <Poco/Mutex.h>
#include <Poco/JSON/Object.h>
#include <Poco/Runnable.h>
#include "common/IIpcSender.h"

class IpcClient : public IIpcSender {
public:
    /**
     * Costruttore.
     * @param socketPath  Percorso del file Unix Domain Socket del Firmware_Layer.
     */
    explicit IpcClient(const std::string& socketPath);

    virtual ~IpcClient();

    /// Connette al Unix Domain Socket del Firmware_Layer.
    void connect();

    /// Disconnette e ferma i thread.
    void disconnect();

    /// Restituisce true se la connessione è attiva.
    bool isConnected() const;

    /**
     * Invia un messaggio JSON al Firmware_Layer.
     * Il messaggio viene serializzato e terminato con newline.
     * Se non connesso, il messaggio viene scartato.
     * Virtuale per consentire il mocking nei test.
     */
    virtual void sendMessage(const Poco::JSON::Object& msg) override;

    /// Callback invocata alla ricezione di un messaggio JSON valido.
    using MessageCallback = std::function<void(const Poco::JSON::Object&)>;

    /// Imposta la callback per i messaggi ricevuti.
    void setMessageCallback(MessageCallback cb);

    /// Callback invocata quando lo stato della connessione IPC cambia.
    using ConnectionStatusCallback = std::function<void(bool connected)>;

    /// Imposta la callback per il cambio stato connessione.
    void setConnectionStatusCallback(ConnectionStatusCallback cb);

private:
    std::string _socketPath;
    Poco::Net::StreamSocket _socket;
    bool _connected;
    bool _running;
    bool _reconnectTimerActive;
    MessageCallback _messageCallback;
    ConnectionStatusCallback _connectionStatusCallback;
    mutable Poco::Mutex _mutex;

    // --- Thread lettura ---
    class ReadRunnable : public Poco::Runnable {
    public:
        explicit ReadRunnable(IpcClient& client);
        void run() override;
    private:
        IpcClient& _client;
    };

    ReadRunnable _readRunnable;
    Poco::Thread _readThread;

    // --- Timer riconnessione ---
    Poco::Timer _reconnectTimer;

    /// Loop di lettura: legge messaggi JSON dal Firmware_Layer.
    void readLoop();

    /// Processa una singola riga JSON ricevuta.
    void processLine(const std::string& line);

    /// Callback del timer di riconnessione (ogni 5 secondi).
    void onReconnect(Poco::Timer& timer);

    /// Tenta la connessione al socket. Restituisce true se riuscita.
    bool tryConnect();

    /// Avvia il thread di lettura.
    void startReadThread();

    /// Ferma il thread di lettura.
    void stopReadThread();

    /// Avvia il timer di riconnessione (ogni 5 secondi), se non già attivo.
    void scheduleReconnect();

    /// Ferma il timer di riconnessione.
    void stopReconnectTimer();

    /// Notifica il cambio stato connessione ai listener.
    void notifyConnectionStatus(bool connected);
};

#endif // IPCCLIENT_H
