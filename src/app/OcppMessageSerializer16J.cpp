/**
 * OcppMessageSerializer16J — Implementazione serializzazione/deserializzazione OCPP 1.6J.
 *
 * Formato OCPP 1.6J:
 *   CALL:       [2, "<uniqueId>", "<action>", {<payload>}]
 *   CALLRESULT: [3, "<uniqueId>", {<payload>}]
 *   CALLERROR:  [4, "<uniqueId>", "<errorCode>", "<errorDescription>", {<errorDetails>}]
 *
 * Requisiti validati: 4.1, 4.2, 4.3
 */
#include "app/OcppMessageSerializer16J.h"

#include <sstream>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Dynamic/Var.h>

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------
std::string OcppMessageSerializer16J::serialize(const OcppMessage& msg)
{
    Poco::JSON::Array arr;

    switch (msg.type) {
    case MessageType::CALL:
        // [2, uniqueId, action, payload]
        arr.add(static_cast<int>(MessageType::CALL));
        arr.add(msg.uniqueId);
        arr.add(msg.action);
        arr.add(msg.payload);
        break;

    case MessageType::CALLRESULT:
        // [3, uniqueId, payload]
        arr.add(static_cast<int>(MessageType::CALLRESULT));
        arr.add(msg.uniqueId);
        arr.add(msg.payload);
        break;

    case MessageType::CALLERROR:
        // [4, uniqueId, errorCode, errorDescription, errorDetails]
        arr.add(static_cast<int>(MessageType::CALLERROR));
        arr.add(msg.uniqueId);
        arr.add(msg.errorCode);
        arr.add(msg.errorDescription);
        arr.add(msg.payload);  // errorDetails
        break;
    }

    std::ostringstream oss;
    arr.stringify(oss);
    return oss.str();
}


// ---------------------------------------------------------------------------
// deserialize
// ---------------------------------------------------------------------------
OcppMessageSerializer16J::OcppMessage
OcppMessageSerializer16J::deserialize(const std::string& json)
{
    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result;

    try {
        result = parser.parse(json);
    } catch (const Poco::Exception&) {
        throw OcppParseError("Invalid JSON");
    }

    Poco::JSON::Array::Ptr arr = result.extract<Poco::JSON::Array::Ptr>();
    if (!arr || arr->size() < 3) {
        throw OcppParseError("OCPP message must be a JSON array with at least 3 elements");
    }

    int typeInt = 0;
    try {
        typeInt = arr->getElement<int>(0);
    } catch (...) {
        throw OcppParseError("First element must be an integer message type (2, 3, or 4)");
    }

    OcppMessage msg;

    switch (typeInt) {
    case 2: { // CALL
        if (arr->size() != 4) {
            throw OcppParseError("CALL message must have exactly 4 elements");
        }
        msg.type     = MessageType::CALL;
        msg.uniqueId = arr->getElement<std::string>(1);
        msg.action   = arr->getElement<std::string>(2);

        Poco::Dynamic::Var payloadVar = arr->get(3);
        if (payloadVar.type() == typeid(Poco::JSON::Object::Ptr)) {
            msg.payload = *payloadVar.extract<Poco::JSON::Object::Ptr>();
        } else if (payloadVar.type() == typeid(Poco::JSON::Object)) {
            msg.payload = payloadVar.extract<Poco::JSON::Object>();
        } else {
            throw OcppParseError("CALL payload must be a JSON object");
        }
        break;
    }
    case 3: { // CALLRESULT
        if (arr->size() != 3) {
            throw OcppParseError("CALLRESULT message must have exactly 3 elements");
        }
        msg.type     = MessageType::CALLRESULT;
        msg.uniqueId = arr->getElement<std::string>(1);

        Poco::Dynamic::Var payloadVar = arr->get(2);
        if (payloadVar.type() == typeid(Poco::JSON::Object::Ptr)) {
            msg.payload = *payloadVar.extract<Poco::JSON::Object::Ptr>();
        } else if (payloadVar.type() == typeid(Poco::JSON::Object)) {
            msg.payload = payloadVar.extract<Poco::JSON::Object>();
        } else {
            throw OcppParseError("CALLRESULT payload must be a JSON object");
        }
        break;
    }
    case 4: { // CALLERROR
        if (arr->size() != 5) {
            throw OcppParseError("CALLERROR message must have exactly 5 elements");
        }
        msg.type             = MessageType::CALLERROR;
        msg.uniqueId         = arr->getElement<std::string>(1);
        msg.errorCode        = arr->getElement<std::string>(2);
        msg.errorDescription = arr->getElement<std::string>(3);

        Poco::Dynamic::Var detailsVar = arr->get(4);
        if (detailsVar.type() == typeid(Poco::JSON::Object::Ptr)) {
            msg.payload = *detailsVar.extract<Poco::JSON::Object::Ptr>();
        } else if (detailsVar.type() == typeid(Poco::JSON::Object)) {
            msg.payload = detailsVar.extract<Poco::JSON::Object>();
        } else {
            throw OcppParseError("CALLERROR errorDetails must be a JSON object");
        }
        break;
    }
    default:
        throw OcppParseError("Unknown message type: " + std::to_string(typeInt) +
                             " (expected 2, 3, or 4)");
    }

    return msg;
}

// ---------------------------------------------------------------------------
// isValid
// ---------------------------------------------------------------------------
bool OcppMessageSerializer16J::isValid(const std::string& json)
{
    try {
        deserialize(json);
        return true;
    } catch (const OcppParseError&) {
        return false;
    } catch (const Poco::Exception&) {
        return false;
    } catch (...) {
        return false;
    }
}
