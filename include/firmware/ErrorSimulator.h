/**
 * ErrorSimulator — Simulazione di condizioni di errore hardware.
 *
 * Responsabilità:
 *   - Iniettare errori (HardwareFault, TamperDetection) nel ConnectorSimulator
 *   - Ripristinare lo stato normale dopo la risoluzione dell'errore
 *   - Mappare i tipi di errore interni ai codici OCPP corrispondenti
 *
 * Requisiti validati: 7.1, 7.2, 7.6
 */
#ifndef ERRORSIMULATOR_H
#define ERRORSIMULATOR_H

#include <string>
#include <Poco/Mutex.h>

class ConnectorSimulator;

class ErrorSimulator {
public:
    /// Tipi di errore simulabili.
    enum class ErrorType { None, HardwareFault, TamperDetection };

    /**
     * @param connector  Riferimento al ConnectorSimulator su cui iniettare gli errori.
     */
    explicit ErrorSimulator(ConnectorSimulator& connector);

    /**
     * Inietta un errore: imposta il connettore a Faulted.
     * Se un errore è già attivo viene sovrascritto.
     *
     * @param type  Tipo di errore da simulare (diverso da None).
     */
    void triggerError(ErrorType type);

    /**
     * Risolve l'errore corrente: ripristina il connettore ad Available.
     * Se nessun errore è attivo, non fa nulla.
     */
    void clearError();

    /// Restituisce il tipo di errore attualmente attivo (thread-safe).
    ErrorType getCurrentError() const;

    /**
     * Mappa l'errore corrente al codice OCPP corrispondente.
     *   HardwareFault   → "InternalError"
     *   TamperDetection → "OtherError"
     *   None            → "NoError"
     */
    std::string getErrorCodeOcpp() const;

private:
    ConnectorSimulator& _connector;
    ErrorType _currentError;
    mutable Poco::Mutex _mutex;
};

#endif // ERRORSIMULATOR_H
