/**
 * OcppMessageSerializer16J — Serializzazione e deserializzazione messaggi OCPP 1.6J.
 *
 * Responsabilità:
 *   - Serializzare un OcppMessage in stringa JSON array conforme OCPP 1.6J
 *   - Deserializzare una stringa JSON array in OcppMessage con validazione
 *   - Verificare la conformità di una stringa JSON alla specifica OCPP 1.6J
 *
 * Formato OCPP 1.6J:
 *   CALL:       [2, "<uniqueId>", "<action>", {<payload>}]
 *   CALLRESULT: [3, "<uniqueId>", {<payload>}]
 *   CALLERROR:  [4, "<uniqueId>", "<errorCode>", "<errorDescription>", {<errorDetails>}]
 *
 * Requisiti validati: 4.1, 4.2, 4.3
 */
#ifndef OCPPMESSAGESERIALIZER16J_H
#define OCPPMESSAGESERIALIZER16J_H

#include <string>
#include <stdexcept>
#include <Poco/JSON/Object.h>

class OcppMessageSerializer16J {
public:
    /// Tipi di messaggio OCPP 1.6J
    enum class MessageType { CALL = 2, CALLRESULT = 3, CALLERROR = 4 };

    /// Struttura interna che rappresenta un messaggio OCPP
    struct OcppMessage {
        MessageType type;
        std::string uniqueId;
        std::string action;           // Solo per CALL
        Poco::JSON::Object payload;
        std::string errorCode;        // Solo per CALLERROR
        std::string errorDescription; // Solo per CALLERROR
    };

    /// Eccezione lanciata per messaggi OCPP non validi
    class OcppParseError : public std::runtime_error {
    public:
        explicit OcppParseError(const std::string& msg)
            : std::runtime_error(msg) {}
    };

    /**
     * Serializza un OcppMessage in stringa JSON array conforme OCPP 1.6J.
     *
     * @param msg  Messaggio da serializzare
     * @return Stringa JSON array
     */
    static std::string serialize(const OcppMessage& msg);

    /**
     * Deserializza una stringa JSON array in OcppMessage.
     * Lancia OcppParseError se la stringa non è conforme alla specifica.
     *
     * @param json  Stringa JSON da deserializzare
     * @return OcppMessage deserializzato
     */
    static OcppMessage deserialize(const std::string& json);

    /**
     * Verifica se una stringa JSON è conforme alla specifica OCPP 1.6J.
     *
     * @param json  Stringa JSON da verificare
     * @return true se il messaggio è valido
     */
    static bool isValid(const std::string& json);
};

#endif // OCPPMESSAGESERIALIZER16J_H
