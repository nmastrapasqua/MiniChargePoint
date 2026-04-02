/**
 * SessionEvent — Evento accodato per il processing serializzato.
 *
 * Estratto da SessionManager.h per evitare dipendenze circolari:
 * sia OcppClient16J che IpcClient devono conoscere SessionEvent
 * per pushare eventi nella coda di SessionManager.
 */
#ifndef SESSIONEVENT_H
#define SESSIONEVENT_H

#include <string>
#include <Poco/JSON/Object.h>

struct SessionEvent {
    enum class Type {
        ConnectorStateChanged,
        MeterValue,
        Error,
        ErrorCleared,
        CentralSystemConnection,
        FirmwareConnection,
        RemoteCommand,
        ProtocolResponse,
        WebPlugIn,
        WebPlugOut,
        WebStartCharge,
        WebStopCharge,
        WebTriggerError,
        WebClearError
    };

    Type type;
    std::string stringParam;      // state, errorType, idTag, action
    std::string stringParam2;     // errorCode, uniqueId
    int intParam = 0;             // meterValue
    bool boolParam = false;       // connected
    Poco::JSON::Object jsonParam; // payload, response
};

#endif // SESSIONEVENT_H
