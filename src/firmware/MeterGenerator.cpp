/**
 * MeterGenerator — Implementazione.
 *
 * Genera valori di misurazione energetica incrementali con Poco::Timer.
 * Simula un consumo di ~7 kW: incremento di ~194 Wh ogni tick (default 10 s).
 * Il valore è cumulativo per sessione e viene azzerato con reset().
 *
 * Requisiti validati: 2.2, 2.3
 */
#include "firmware/MeterGenerator.h"

#include <Poco/Logger.h>

// ---------------------------------------------------------------------------
// Costruttore / Distruttore
// ---------------------------------------------------------------------------

MeterGenerator::MeterGenerator(int intervalSeconds)
    : _intervalMs(intervalSeconds * 1000)
    , _meterValueWh(0)
    , _incrementWh(194)   // ~7 kW → 7000 W / 3600 s * 100 s ≈ 194 Wh per 10 s tick
    , _running(false)
    , _timer(nullptr)
{
}

MeterGenerator::~MeterGenerator()
{
    stop();
    delete _timer;
}

// ---------------------------------------------------------------------------
// Controllo generazione
// ---------------------------------------------------------------------------

void MeterGenerator::start()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_running) {
        Poco::Logger::get("MeterGenerator")
            .warning("start() called but already running");
        return;
    }
    _running = true;

    delete _timer;
    _timer = new Poco::Timer(_intervalMs, _intervalMs);
    Poco::TimerCallback<MeterGenerator> cb(*this, &MeterGenerator::onTimer);
    _timer->start(cb);

    Poco::Logger::get("MeterGenerator")
        .information("Meter generation started (interval %d ms)", _intervalMs);
}

void MeterGenerator::stop()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (!_running) {
        return;
    }
    _running = false;
    if (_timer) {
        _timer->stop();
    }

    Poco::Logger::get("MeterGenerator")
        .information("Meter generation stopped (current value: %d Wh)", _meterValueWh);
}

void MeterGenerator::reset()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _meterValueWh = 0;

    Poco::Logger::get("MeterGenerator")
        .information("Meter value reset to 0");
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

int MeterGenerator::getCurrentMeterValue() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return _meterValueWh;
}

// ---------------------------------------------------------------------------
// Callback
// ---------------------------------------------------------------------------

void MeterGenerator::setMeterCallback(MeterCallback cb)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    _callback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Timer handler
// ---------------------------------------------------------------------------

void MeterGenerator::onTimer(Poco::Timer& /* timer */)
{
    MeterCallback cb;
    int newValue;

    {
        Poco::Mutex::ScopedLock lock(_mutex);
        if (!_running) {
            return;
        }
        _meterValueWh += _incrementWh;
        newValue = _meterValueWh;
        cb = _callback;
    }

    Poco::Logger::get("MeterGenerator")
        .debug("Meter tick: %d Wh (+%d)", newValue, _incrementWh);

    if (cb) {
        cb(newValue);
    }
}
