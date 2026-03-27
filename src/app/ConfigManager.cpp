/**
 * ConfigManager — Implementazione.
 *
 * Utilizza Poco::JSON::Parser per il parsing e Poco::Logger per il logging.
 * Requisiti validati: 8.1, 8.2, 8.3, 8.4
 */
#include "app/ConfigManager.h"

#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Logger.h>
#include <Poco/File.h>

#include <fstream>
#include <sstream>
#include <cstdlib>

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------
ConfigManager::Config ConfigManager::defaults()
{
    return Config();
}

// ---------------------------------------------------------------------------
// load  (Requisiti 8.1, 8.2, 8.3, 8.4)
// ---------------------------------------------------------------------------
ConfigManager::Config ConfigManager::load(const std::string& filePath)
{
    Poco::Logger& logger = Poco::Logger::get("ConfigManager");

    // Req 8.3 — file non trovato → default + warning
    if (!Poco::File(filePath).exists()) {
        logger.warning("Config file not found: %s — using defaults", filePath);
        return defaults();
    }

    // Leggere il contenuto del file
    std::ifstream ifs(filePath);
    if (!ifs.good()) {
        logger.warning("Cannot open config file: %s — using defaults", filePath);
        return defaults();
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    // Parsing JSON — Req 8.4 (JSON malformato → errore + terminazione)
    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result;
    try {
        result = parser.parse(content);
    } catch (const Poco::Exception& ex) {
        logger.error("Malformed JSON in config file: %s — %s", filePath, ex.displayText());
        std::exit(1);
    }

    Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();
    if (!obj) {
        logger.error("Config file root is not a JSON object: %s", filePath);
        std::exit(1);
    }

    // Popolare la struct Config — Req 8.2
    Config cfg = defaults();

    if (obj->has("centralSystemUrl"))  cfg.centralSystemUrl = obj->getValue<std::string>("centralSystemUrl");
    if (obj->has("chargePointId"))     cfg.chargePointId    = obj->getValue<std::string>("chargePointId");
    if (obj->has("socketPath"))        cfg.socketPath       = obj->getValue<std::string>("socketPath");
    if (obj->has("httpPort"))          cfg.httpPort          = obj->getValue<int>("httpPort");
    if (obj->has("meterInterval"))     cfg.meterInterval     = obj->getValue<int>("meterInterval");
    if (obj->has("defaultIdTag"))      cfg.defaultIdTag      = obj->getValue<std::string>("defaultIdTag");
    if (obj->has("logLevel"))          cfg.logLevel          = obj->getValue<std::string>("logLevel");
    if (obj->has("logFile"))           cfg.logFile           = obj->getValue<std::string>("logFile");

    // Validazione — Req 8.4 (valori non validi → errore + terminazione)
    std::string errorMsg;
    if (!validate(cfg, errorMsg)) {
        logger.error("Invalid configuration: %s", errorMsg);
        std::exit(1);
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// validate  (Requisito 8.4)
// ---------------------------------------------------------------------------
bool ConfigManager::validate(const Config& config, std::string& errorMsg)
{
    if (config.centralSystemUrl.empty()) {
        errorMsg = "centralSystemUrl must not be empty";
        return false;
    }
    if (config.socketPath.empty()) {
        errorMsg = "socketPath must not be empty";
        return false;
    }
    if (config.httpPort <= 0) {
        errorMsg = "httpPort must be > 0 (got " + std::to_string(config.httpPort) + ")";
        return false;
    }
    if (config.meterInterval <= 0) {
        errorMsg = "meterInterval must be > 0 (got " + std::to_string(config.meterInterval) + ")";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// toJson  (Requisito 8.1)
// ---------------------------------------------------------------------------
std::string ConfigManager::toJson(const Config& config)
{
    Poco::JSON::Object obj;
    obj.set("centralSystemUrl", config.centralSystemUrl);
    obj.set("chargePointId",    config.chargePointId);
    obj.set("socketPath",       config.socketPath);
    obj.set("httpPort",         config.httpPort);
    obj.set("meterInterval",    config.meterInterval);
    obj.set("defaultIdTag",     config.defaultIdTag);
    obj.set("logLevel",         config.logLevel);
    obj.set("logFile",          config.logFile);

    std::ostringstream oss;
    obj.stringify(oss, 4); // indentazione 4 spazi
    return oss.str();
}
