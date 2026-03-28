/**
 * ConnectorSimulator — Macchina a stati del connettore fisico simulato.
 *
 * Responsabilità:
 *   - Gestire gli stati del connettore: Available, Preparing, Charging, Finishing, Faulted
 *   - Validare le transizioni di stato (rifiutare quelle non valide)
 *   - Notificare i cambi di stato tramite callback
 *
 * Requisiti validati: 2.1, 2.4, 2.5
 */
#ifndef CONNECTORSIMULATOR_H
#define CONNECTORSIMULATOR_H

#include <string>
#include <functional>
#include <Poco/Mutex.h>

class ConnectorSimulator {
public:
    /// Stati possibili del connettore.
    enum class State { Available, Preparing, Charging, Finishing, Faulted };

    ConnectorSimulator();

    // -- Comandi --

    /// Available → Preparing
    void plugIn();

    /// Preparing → Charging
    void startCharging();

    /// Charging → Finishing → Available (stopCharging porta a Finishing)
    void stopCharging();

    /// Preparing/Finishing → Available
    void plugOut();

    /// Qualsiasi stato (tranne Faulted) → Faulted
    void fault();

    /// Faulted → Available
    void clearFault();

    // -- Query --

    /// Restituisce lo stato corrente (thread-safe).
    State getState() const;

    /// Restituisce lo stato corrente come stringa leggibile.
    std::string getStateString() const;

    // -- Callback --

    /// Callback invocata ad ogni transizione di stato valida.
    using StateCallback = std::function<void(State oldState, State newState)>;

    /// Imposta la callback per le notifiche di cambio stato.
    void setStateCallback(StateCallback cb);

private:
    State _state;
    StateCallback _callback;
    mutable Poco::Mutex _mutex;

    /**
     * Esegue la transizione allo stato indicato, se valida.
     * Se la transizione non è valida, lo stato non viene modificato.
     *
     * @param newState  Stato di destinazione
     * @return true se la transizione è avvenuta
     */
    bool transitionTo(State newState);

    /// Converte uno State in stringa.
    static std::string stateToString(State s);
};

#endif // CONNECTORSIMULATOR_H
