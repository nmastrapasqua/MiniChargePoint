/**
 * IpcServer — Server IPC su Unix Domain Socket per il Firmware_Layer.
 *
 * Responsabilità:
 *   - Creare un Unix Domain Socket e attendere la connessione dell'Application_Layer
 *   - Inviare messaggi JSON (delimitati da newline) all'Application_Layer
 *   - Ricevere messaggi JSON dall'Application_Layer e notificarli tramite callback
 *   - Gestire errori: JSON malformato → scartato con log Error;
 *     tipo sconosciuto → ignorato con log Warning
 *
 * Thread separati per accept e lettura.
 *
 * Requisiti validati: 1.2, 1.4
 */
#ifndef IPCSERVER_H
#define IPCSERVER_H

#include <string>
#include <functional>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Thread.h>
#include <Poco/Mutex.h>
#include <Poco/JSON/Object.h>
#include <Poco/Runnable.h>

class IpcServer {
public:
    /**
     * Costruttore.
     * @param socketPath  Percorso del file Unix Domain Socket.
     */
    explicit IpcServer(const std::string& socketPath);

    ~IpcServer();

    /// Avvia il server: crea il socket, avvia il thread di accept.
    void start();

    /// Ferma il server: chiude le connessioni e i thread.
    void stop();

    /**
     * Invia un messaggio JSON all'Application_Layer connesso.
     * Il messaggio viene serializzato e terminato con newline.
     * Se nessun client è connesso, il messaggio viene scartato.
     */
    void sendMessage(const Poco::JSON::Object& msg);

    /// Callback invocata alla ricezione di un messaggio JSON valido.
    using MessageCallback = std::function<void(const Poco::JSON::Object&)>;

    /// Imposta la callback per i messaggi ricevuti.
    void setMessageCallback(MessageCallback cb);

private:
    std::string _socketPath;
    Poco::Net::ServerSocket _serverSocket;
    Poco::Net::StreamSocket _clientSocket;
    bool _running;
    bool _clientConnected;
    MessageCallback _messageCallback;
    mutable Poco::Mutex _mutex;

    // --- Thread accept ---
    class AcceptRunnable : public Poco::Runnable {
    public:
        explicit AcceptRunnable(IpcServer& server);
        void run() override;
    private:
        IpcServer& _server;
    };

    // --- Thread lettura ---
    class ReadRunnable : public Poco::Runnable {
    public:
        explicit ReadRunnable(IpcServer& server);
        void run() override;
    private:
        IpcServer& _server;
    };

    AcceptRunnable _acceptRunnable;
    ReadRunnable   _readRunnable;
    Poco::Thread   _acceptThread;
    Poco::Thread   _readThread;

    /// Loop di accept: attende una connessione client.
    void acceptLoop();

    /// Loop di lettura: legge messaggi JSON dal client connesso.
    void readLoop();

    /// Processa una singola riga JSON ricevuta.
    void processLine(const std::string& line);

    /// Rimuove il file socket se esiste.
    void removeSocketFile();
};

#endif // IPCSERVER_H
