/**
 * IpcMessage — Implementazione serializzazione/deserializzazione messaggi IPC.
 *
 * Utilizza Poco::JSON::Parser e Poco::JSON::Object per il handling JSON.
 * I messaggi sono oggetti JSON compatti (una riga), delimitati da newline.
 *
 * Requisiti validati: 1.4
 */
#include "common/IpcMessage.h"

#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Exception.h>

#include <sstream>

namespace IpcMessage {

// ---- Costanti tipo messaggio ------------------------------------------------

const std::string TYPE_CONNECTOR_STATE = "connector_state";
const std::string TYPE_METER_VALUE     = "meter_value";
const std::string TYPE_ERROR           = "error";
const std::string TYPE_ERROR_CLEARED   = "error_cleared";
const std::string TYPE_COMMAND         = "command";

// ---- Helper interni ---------------------------------------------------------

std::string toJsonString(const Poco::JSON::Object& obj)
{
    std::ostringstream oss;
    obj.stringify(oss);
    return oss.str();
}

Poco::JSON::Object::Ptr parseJson(const std::string& json)
{
    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result = parser.parse(json);
    Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();
    if (!obj) {
        throw Poco::DataFormatException("JSON root is not an object");
    }
    return obj;
}

// ---- Serializzazione --------------------------------------------------------

std::string serialize(const ConnectorStateMsg& msg)
{
    Poco::JSON::Object obj;
    obj.set("type",      TYPE_CONNECTOR_STATE);
    obj.set("state",     msg.state);
    obj.set("timestamp", msg.timestamp);
    return toJsonString(obj);
}

std::string serialize(const MeterValueMsg& msg)
{
    Poco::JSON::Object obj;
    obj.set("type",      TYPE_METER_VALUE);
    obj.set("value",     msg.value);
    obj.set("unit",      msg.unit);
    obj.set("timestamp", msg.timestamp);
    return toJsonString(obj);
}

std::string serialize(const ErrorMsg& msg)
{
    Poco::JSON::Object obj;
    obj.set("type",        TYPE_ERROR);
    obj.set("errorType",   msg.errorType);
    obj.set("description", msg.description);
    obj.set("timestamp",   msg.timestamp);
    return toJsonString(obj);
}

std::string serialize(const ErrorClearedMsg& msg)
{
    Poco::JSON::Object obj;
    obj.set("type",      TYPE_ERROR_CLEARED);
    obj.set("timestamp", msg.timestamp);
    return toJsonString(obj);
}

std::string serialize(const CommandMsg& msg)
{
    Poco::JSON::Object obj;
    obj.set("type",   TYPE_COMMAND);
    obj.set("action", msg.action);
    if (!msg.errorType.empty()) {
        obj.set("errorType", msg.errorType);
    }
    return toJsonString(obj);
}

// ---- Deserializzazione ------------------------------------------------------

std::string getType(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    if (!obj->has("type")) {
        throw Poco::DataFormatException("IPC message missing 'type' field");
    }
    return obj->getValue<std::string>("type");
}

ConnectorStateMsg deserializeConnectorState(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    ConnectorStateMsg msg;
    if (!obj->has("state")) {
        throw Poco::DataFormatException("connector_state message missing 'state' field");
    }
    msg.state     = obj->getValue<std::string>("state");
    msg.timestamp = obj->optValue<std::string>("timestamp", "");
    return msg;
}

MeterValueMsg deserializeMeterValue(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    MeterValueMsg msg;
    if (!obj->has("value")) {
        throw Poco::DataFormatException("meter_value message missing 'value' field");
    }
    msg.value     = obj->getValue<int>("value");
    msg.unit      = obj->optValue<std::string>("unit", "Wh");
    msg.timestamp = obj->optValue<std::string>("timestamp", "");
    return msg;
}

ErrorMsg deserializeError(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    ErrorMsg msg;
    if (!obj->has("errorType")) {
        throw Poco::DataFormatException("error message missing 'errorType' field");
    }
    msg.errorType   = obj->getValue<std::string>("errorType");
    msg.description = obj->optValue<std::string>("description", "");
    msg.timestamp   = obj->optValue<std::string>("timestamp", "");
    return msg;
}

ErrorClearedMsg deserializeErrorCleared(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    ErrorClearedMsg msg;
    msg.timestamp = obj->optValue<std::string>("timestamp", "");
    return msg;
}

CommandMsg deserializeCommand(const std::string& json)
{
    Poco::JSON::Object::Ptr obj = parseJson(json);
    CommandMsg msg;
    if (!obj->has("action")) {
        throw Poco::DataFormatException("command message missing 'action' field");
    }
    msg.action    = obj->getValue<std::string>("action");
    msg.errorType = obj->optValue<std::string>("errorType", "");
    return msg;
}

} // namespace IpcMessage
