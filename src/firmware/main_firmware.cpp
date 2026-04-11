/**
 * main_firmware.cpp — Entry point del processo firmware_simulator.
 *
 * Istanzia i componenti del Firmware_Layer (ConnectorSimulator, MeterGenerator,
 * ErrorSimulator, IpcServer), collega le callback per la comunicazione IPC
 * e gestisce i comandi ricevuti dall'Application_Layer.
 *
 * Requisiti validati: 1.2, 2.4, 2.5, 9.1, 9.2, 9.4, 9.5
 */
#include "app/ConfigManager.h"
#include "firmware/ConnectorSimulator.h"
#include "firmware/MeterGenerator.h"
#include "firmware/ErrorSimulator.h"
#include "firmware/IpcServer.h"
#include "common/IpcMessage.h"

#include <Poco/Logger.h>
#include <Poco/AutoPtr.h>
#include <Poco/ConsoleChannel.h>
#include <Poco/FileChannel.h>
#include <Poco/SplitterChannel.h>
#include <Poco/FormattingChannel.h>
#include <Poco/PatternFormatter.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Timestamp.h>
#include <Poco/JSON/Object.h>
#include <Poco/Util/ServerApplication.h>

#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running(true);

static void signalHandler(int)
{
    g_running = false;
}

/// Restituisce il timestamp corrente in formato ISO 8601.
static std::string nowTimestamp()
{
    return Poco::DateTimeFormatter::format(
        Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FRAC_FORMAT);
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
    formatter->setProperty("pattern", "%Y-%m-%d %H:%M:%S.%i [%p] %s: %t");

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

int main(int argc, char* argv[])
{
    // --- Configurazione ---
    std::string configPath = "config.json";
    if (argc > 1) {
        configPath = argv[1];
    }

    ConfigManager::Config cfg = ConfigManager::load(configPath);
    setupLogging(cfg);

    Poco::Logger& logger = Poco::Logger::get("FirmwareMain");
    logger.information("firmware_simulator starting (socket: %s)", cfg.socketPath);

    // --- Componenti ---
    ConnectorSimulator connector;
    MeterGenerator meter(cfg.meterInterval);
    ErrorSimulator errorSim(connector);
    IpcServer ipcServer(cfg.socketPath);

    // --- Callback: cambio stato connettore → messaggio IPC ---
    connector.setStateCallback(
        [&](ConnectorSimulator::State /* oldState */, ConnectorSimulator::State newState) {
            std::string stateStr;
            switch (newState) {
                case ConnectorSimulator::State::Available:  stateStr = "Available";  break;
                case ConnectorSimulator::State::Preparing:  stateStr = "Preparing";  break;
                case ConnectorSimulator::State::Charging:   stateStr = "Charging";   break;
                case ConnectorSimulator::State::Finishing:  stateStr = "Finishing";  break;
                case ConnectorSimulator::State::Faulted:    stateStr = "Faulted";    break;
            }

            // Avvia/ferma/azzera il MeterGenerator in base allo stato
            if (newState == ConnectorSimulator::State::Charging) {
                meter.start();
            } else if (newState == ConnectorSimulator::State::Finishing) {
                meter.stop();
            } else if (newState == ConnectorSimulator::State::Available) {
                meter.stop();
                meter.reset();
            } else if (newState == ConnectorSimulator::State::Faulted) {
                meter.stop();
            }

            // Invia notifica IPC
            Poco::JSON::Object msg;
            msg.set("type",      IpcMessage::TYPE_CONNECTOR_STATE);
            msg.set("state",     stateStr);
            msg.set("errorType", errorSim.getErrorType());
            msg.set("timestamp", nowTimestamp());
            ipcServer.sendMessage(msg);
        });

    // --- Callback: meter value → messaggio IPC ---
    meter.setMeterCallback(
        [&](int meterValueWh) {
            Poco::JSON::Object msg;
            msg.set("type",      IpcMessage::TYPE_METER_VALUE);
            msg.set("value",     meterValueWh);
            msg.set("unit",      std::string("Wh"));
            msg.set("timestamp", nowTimestamp());
            ipcServer.sendMessage(msg);
        });

    // --- Callback: comandi ricevuti via IPC ---
    ipcServer.setMessageCallback(
        [&](const Poco::JSON::Object& obj) {
            if (!obj.has("type")) return;
            std::string type = obj.getValue<std::string>("type");

            if (type != IpcMessage::TYPE_COMMAND) return;

            if (!obj.has("action")) {
                logger.warning("IPC command missing 'action' field");
                return;
            }
            std::string action = obj.getValue<std::string>("action");

            logger.information("Received IPC command: %s", action);

            if (action == "plug_in") {
                connector.plugIn();
            } else if (action == "plug_out") {
                connector.plugOut();
            } else if (action == "start_charge") {
                connector.startCharging();
            } else if (action == "stop_charge") {
                connector.stopCharging();
            } else if (action == "trigger_error") {
                std::string errorType = obj.optValue<std::string>("errorType", "HardwareFault");
                ErrorSimulator::ErrorType et = ErrorSimulator::ErrorType::HardwareFault;
                if (errorType == "TamperDetection") {
                    et = ErrorSimulator::ErrorType::TamperDetection;
                }
                errorSim.triggerError(et);

                // Invia notifica errore via IPC
                Poco::JSON::Object errMsg;
                errMsg.set("type",        IpcMessage::TYPE_ERROR);
                errMsg.set("errorType",   errorType);
                errMsg.set("description", errorType == "TamperDetection"
                    ? "Tamper detection triggered" : "Hardware fault detected");
                errMsg.set("timestamp",   nowTimestamp());
                ipcServer.sendMessage(errMsg);
            } else if (action == "clear_error") {
                errorSim.clearError();

                // Invia notifica errore risolto via IPC
                Poco::JSON::Object clrMsg;
                clrMsg.set("type",      IpcMessage::TYPE_ERROR_CLEARED);
                clrMsg.set("timestamp", nowTimestamp());
                ipcServer.sendMessage(clrMsg);
            } else {
                logger.warning("Unknown IPC command action: %s", action);
            }
        });

    // --- Gestione segnali ---
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // --- Avvio IPC Server ---
    ipcServer.start();
    logger.information("firmware_simulator running. Press Ctrl+C to stop.");

    // --- Loop principale ---
    while (g_running) {
        Poco::Thread::sleep(500);
    }

    // --- Shutdown ---
    logger.information("firmware_simulator shutting down...");
    meter.stop();
    ipcServer.stop();
    logger.information("firmware_simulator stopped.");

    return 0;
}
