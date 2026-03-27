/**
 * IpcMessage — Tipi comuni e helper per i messaggi IPC tra Firmware_Layer e Application_Layer.
 *
 * I messaggi IPC sono oggetti JSON delimitati da newline (\n), trasmessi su
 * Unix Domain Socket. Ogni messaggio ha un campo "type" obbligatorio.
 *
 * Tipi di messaggio:
 *   Firmware → Application: connector_state, meter_value, error, error_cleared
 *   Application → Firmware: command
 *
 * Requisiti validati: 1.4
 */
#ifndef IPCMESSAGE_H
#define IPCMESSAGE_H

#include <string>
#include <Poco/JSON/Object.h>

namespace IpcMessage {

// ---- Costanti tipo messaggio ------------------------------------------------

extern const std::string TYPE_CONNECTOR_STATE;
extern const std::string TYPE_METER_VALUE;
extern const std::string TYPE_ERROR;
extern const std::string TYPE_ERROR_CLEARED;
extern const std::string TYPE_COMMAND;

// ---- Strutture messaggio ----------------------------------------------------

/// Notifica cambio stato connettore (Firmware → Application)
struct ConnectorStateMsg {
    std::string state;
    std::string timestamp;
};

/// Valore meter energetico (Firmware → Application)
struct MeterValueMsg {
    int         value;      // Wh
    std::string unit;       // "Wh"
    std::string timestamp;
};

/// Notifica errore (Firmware → Application)
struct ErrorMsg {
    std::string errorType;    // "HardwareFault", "TamperDetection"
    std::string description;
    std::string timestamp;
};

/// Errore risolto (Firmware → Application)
struct ErrorClearedMsg {
    std::string timestamp;
};

/// Comando (Application → Firmware)
struct CommandMsg {
    std::string action;       // plug_in, plug_out, start_charge, stop_charge, trigger_error, clear_error
    std::string errorType;    // solo per action "trigger_error"
};

// ---- Serializzazione (struct → JSON string) ---------------------------------

/**
 * Serializza un ConnectorStateMsg in stringa JSON (senza newline finale).
 */
std::string serialize(const ConnectorStateMsg& msg);

/**
 * Serializza un MeterValueMsg in stringa JSON.
 */
std::string serialize(const MeterValueMsg& msg);

/**
 * Serializza un ErrorMsg in stringa JSON.
 */
std::string serialize(const ErrorMsg& msg);

/**
 * Serializza un ErrorClearedMsg in stringa JSON.
 */
std::string serialize(const ErrorClearedMsg& msg);

/**
 * Serializza un CommandMsg in stringa JSON.
 */
std::string serialize(const CommandMsg& msg);

// ---- Deserializzazione (JSON string → struct) -------------------------------

/**
 * Estrae il campo "type" da una stringa JSON.
 * Lancia Poco::Exception se il JSON è malformato o manca il campo "type".
 */
std::string getType(const std::string& json);

/**
 * Deserializza una stringa JSON in ConnectorStateMsg.
 * Lancia Poco::Exception se i campi obbligatori mancano.
 */
ConnectorStateMsg deserializeConnectorState(const std::string& json);

/**
 * Deserializza una stringa JSON in MeterValueMsg.
 */
MeterValueMsg deserializeMeterValue(const std::string& json);

/**
 * Deserializza una stringa JSON in ErrorMsg.
 */
ErrorMsg deserializeError(const std::string& json);

/**
 * Deserializza una stringa JSON in ErrorClearedMsg.
 */
ErrorClearedMsg deserializeErrorCleared(const std::string& json);

/**
 * Deserializza una stringa JSON in CommandMsg.
 */
CommandMsg deserializeCommand(const std::string& json);

// ---- Helper Poco::JSON::Object ----------------------------------------------

/**
 * Converte un Poco::JSON::Object in stringa JSON compatta (senza newline).
 */
std::string toJsonString(const Poco::JSON::Object& obj);

/**
 * Parsa una stringa JSON in un Poco::JSON::Object::Ptr.
 * Lancia Poco::Exception se il JSON è malformato.
 */
Poco::JSON::Object::Ptr parseJson(const std::string& json);

} // namespace IpcMessage

#endif // IPCMESSAGE_H
