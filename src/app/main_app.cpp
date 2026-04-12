/**
 * main_app.cpp — Entry point del processo charge_point_app (Application_Layer).
 *
 * Istanzia ConfigManager, IpcClient, OcppClient16J (come ProtocolAdapter),
 * SessionManager e WebServer. Collega tutti i componenti
 * tramite code sincronizzate.
 *
 * Requisiti validati: 1.3, 5.4, 9.1, 9.2, 9.5
 */
#include "app/ConfigManager.h"
#include "app/IpcClient.h"
#include "app/OcppClient16J.h"
#include "app/SessionManager.h"
#include "app/WebServer.h"
#include "app/WebSocketHandler.h"
#include "common/IpcMessage.h"
#include "common/AsciiArt.h"

#include <Poco/Logger.h>
#include <Poco/AutoPtr.h>
#include <Poco/ConsoleChannel.h>
#include <Poco/FileChannel.h>
#include <Poco/SplitterChannel.h>
#include <Poco/FormattingChannel.h>
#include <Poco/PatternFormatter.h>
#include <Poco/Thread.h>
#include <Poco/JSON/Object.h>

#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <stdexcept>

static std::atomic<bool> g_running(true);

static void signalHandler(int)
{
    g_running = false;
}

/// Converte una stringa di livello log nel valore numerico Poco.
static int parseLogLevel(const std::string& level)
{
    if (level == "error")       return Poco::Message::PRIO_ERROR;
    if (level == "warning")     return Poco::Message::PRIO_WARNING;
    if (level == "information") return Poco::Message::PRIO_INFORMATION;
    if (level == "debug")       return Poco::Message::PRIO_DEBUG;
    return Poco::Message::PRIO_INFORMATION;
}

/// Configura il logging Poco: console + file, livello da configurazione.
static void setupLogging(const ConfigManager::Config& cfg)
{
    Poco::AutoPtr<Poco::PatternFormatter> formatter(new Poco::PatternFormatter);
    formatter->setProperty("pattern", cfg.logFormat);

    Poco::AutoPtr<Poco::ConsoleChannel> consoleChannel(new Poco::ConsoleChannel);
    Poco::AutoPtr<Poco::FileChannel> fileChannel(new Poco::FileChannel);
    fileChannel->setProperty("path", cfg.logFile);
    fileChannel->setProperty("rotation", "10 M");

    Poco::AutoPtr<Poco::SplitterChannel> splitter(new Poco::SplitterChannel);
    splitter->addChannel(consoleChannel);
    splitter->addChannel(fileChannel);

    Poco::AutoPtr<Poco::FormattingChannel> formattingChannel(
        new Poco::FormattingChannel(formatter, splitter));

    Poco::Logger::root().setChannel(formattingChannel);
    Poco::Logger::root().setLevel(parseLogLevel(cfg.logLevel));
}

/// Crea il ProtocolAdapter in base al campo "protocol" della configurazione.
/// Per ora supporta solo "ocpp1.6j"; altri protocolli possono essere aggiunti qui.
static std::unique_ptr<ProtocolAdapter> createProtocolAdapter(
    const ConfigManager::Config& cfg,
	ThreadSafeQueue<SessionEvent>* eventQ,
	ThreadSafeQueue<CentralSystemEvent>* csysQueue)
{
    if (cfg.protocol == "ocpp1.6j") {
        return std::unique_ptr<ProtocolAdapter>(
            new OcppClient16J(cfg.centralSystemUrl, cfg.chargePointId, eventQ, csysQueue));
    }
    // Aggiungere qui eventuali altri protocolli, es.:
    // if (cfg.protocol == "ocpp2.0.1") {
    //     return std::unique_ptr<ProtocolAdapter>(
    //         new OcppClient201(cfg.centralSystemUrl, cfg.chargePointId));
    // }
    throw std::runtime_error("Unknown protocol: " + cfg.protocol);
}

int main(int argc, char* argv[])
{
	printBanner();

    // --- Configurazione ---
    std::string configPath = "config.json";
    if (argc > 1) {
        configPath = argv[1];
    }

    ConfigManager::Config cfg = ConfigManager::load(configPath);
    setupLogging(cfg);

    Poco::Logger& logger = Poco::Logger::get("AppMain");
    logger.debug("charge_point_app starting");

    // --- Code ---
    ThreadSafeQueue<SessionEvent> eventQueue;
    ThreadSafeQueue<std::string> uiQueue;
    ThreadSafeQueue<std::string> ipcQueue;
    ThreadSafeQueue<CentralSystemEvent> csysQueue;

    // --- Componenti ---
    IpcClient ipcClient(cfg.socketPath, &ipcQueue, &eventQueue);
    std::unique_ptr<ProtocolAdapter> protocol = createProtocolAdapter(cfg, &eventQueue, &csysQueue);
    SessionManager sessionManager(&eventQueue, &uiQueue, &ipcQueue, &csysQueue);
    WebServer webServer(cfg.httpPort, "web", &eventQueue, &uiQueue);

    // --- Gestione segnali ---
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // --- Avvio componenti ---
    sessionManager.start();
    ipcClient.start();
    protocol->start();
    webServer.start();

    logger.information("charge_point_app running. Press Ctrl+C to stop.");

    // --- Loop principale ---
    while (g_running) {
        Poco::Thread::sleep(500);
    }

    // --- Shutdown ---
    logger.information("charge_point_app shutting down...");
    eventQueue.close();
    uiQueue.close();
    ipcQueue.close();
    csysQueue.close();

    webServer.stop();
    protocol->stop();
    ipcClient.stop();
    sessionManager.stop();
    logger.information("charge_point_app stopped.");

    return 0;
}
