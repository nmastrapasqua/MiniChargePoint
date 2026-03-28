/**
 * ConnectorSimulator — Implementazione.
 *
 * Macchina a stati del connettore con validazione delle transizioni.
 * Transizioni valide:
 *   Available  → Preparing   (plugIn)
 *   Preparing  → Charging    (startCharging)
 *   Preparing  → Available   (plugOut)
 *   Charging   → Finishing   (stopCharging)
 *   Finishing  → Available   (plugOut)
 *   Qualsiasi  → Faulted     (fault)       — tranne se già Faulted
 *   Faulted    → Available   (clearFault)
 *
 * Requisiti validati: 2.1, 2.4, 2.5
 */
#include "firmware/ConnectorSimulator.h"

#include <Poco/Logger.h>

// ---------------------------------------------------------------------------
// Costruttore
// ---------------------------------------------------------------------------
ConnectorSimulator::ConnectorSimulator()
    : _state(State::Available)
{
}

// ---------------------------------------------------------------------------
// Comandi pubblici
// ---------------------------------------------------------------------------

void ConnectorSimulator::plugIn()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state != State::Available) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("plugIn rejected: current state is %s", stateToString(_state));
        return;
    }
    transitionTo(State::Preparing);
}

void ConnectorSimulator::startCharging()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state != State::Preparing) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("startCharging rejected: current state is %s", stateToString(_state));
        return;
    }
    transitionTo(State::Charging);
}

void ConnectorSimulator::stopCharging()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state != State::Charging) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("stopCharging rejected: current state is %s", stateToString(_state));
        return;
    }
    transitionTo(State::Finishing);
}

void ConnectorSimulator::plugOut()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state != State::Preparing && _state != State::Finishing) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("plugOut rejected: current state is %s", stateToString(_state));
        return;
    }
    transitionTo(State::Available);
}

void ConnectorSimulator::fault()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state == State::Faulted) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("fault rejected: already in Faulted state");
        return;
    }
    transitionTo(State::Faulted);
}

void ConnectorSimulator::clearFault()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_state != State::Faulted) {
        Poco::Logger::get("ConnectorSimulator")
            .warning("clearFault rejected: current state is %s", stateToString(_state));
        return;
    }
    transitionTo(State::Available);
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

ConnectorSimulator::State ConnectorSimulator::getState() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return _state;
}

std::string ConnectorSimulator::getStateString() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return stateToString(_state);
}

// ---------------------------------------------------------------------------
// Callback
// ---------------------------------------------------------------------------

void ConnectorSimulator::setStateCallback(StateCallback cb)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _callback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Transizione interna (chiamata con mutex già acquisito)
// ---------------------------------------------------------------------------

bool ConnectorSimulator::transitionTo(State newState)
{
    State oldState = _state;
    _state = newState;

    Poco::Logger::get("ConnectorSimulator")
        .information("State transition: %s → %s",
                     stateToString(oldState), stateToString(newState));

    if (_callback) {
        _callback(oldState, newState);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

std::string ConnectorSimulator::stateToString(State s)
{
    switch (s) {
        case State::Available:  return "Available";
        case State::Preparing:  return "Preparing";
        case State::Charging:   return "Charging";
        case State::Finishing:  return "Finishing";
        case State::Faulted:    return "Faulted";
    }
    return "Unknown";
}
