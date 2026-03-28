/**
 * MeterGenerator — Generatore di valori di misurazione energetica simulati.
 *
 * Responsabilità:
 *   - Generare Meter_Value incrementali durante lo stato Charging
 *   - Simulare un consumo di ~7 kW (incremento ~194 Wh ogni tick)
 *   - Azzerare il contatore quando il connettore torna ad Available
 *   - Notificare ogni nuovo valore tramite callback
 *
 * Requisiti validati: 2.2, 2.3
 */
#ifndef METERGENERATOR_H
#define METERGENERATOR_H

#include <functional>
#include <Poco/Mutex.h>
#include <Poco/Timer.h>

class MeterGenerator {
public:
    /// @param intervalSeconds  Intervallo tra i tick in secondi (default 10).
    explicit MeterGenerator(int intervalSeconds = 10);
    ~MeterGenerator();

    /// Avvia la generazione periodica di meter values (stato Charging).
    void start();

    /// Ferma la generazione periodica.
    void stop();

    /// Azzera il contatore energetico (fine sessione).
    void reset();

    /// Restituisce il valore cumulativo corrente in Wh (thread-safe).
    int getCurrentMeterValue() const;

    /// Callback invocata ad ogni tick con il nuovo valore cumulativo (Wh).
    using MeterCallback = std::function<void(int meterValueWh)>;

    /// Imposta la callback per le notifiche di meter value.
    void setMeterCallback(MeterCallback cb);

private:
    int _intervalMs;          ///< Intervallo del timer in millisecondi
    int _meterValueWh;        ///< Valore cumulativo corrente (Wh)
    int _incrementWh;         ///< Incremento per tick (~194 Wh ≈ 7 kW)
    bool _running;            ///< Flag di esecuzione
    MeterCallback _callback;
    mutable Poco::Mutex _mutex;
    Poco::Timer* _timer;     ///< Heap-allocated: intervallo impostato nel costruttore

    /// Handler del timer periodico.
    void onTimer(Poco::Timer& timer);
};

#endif // METERGENERATOR_H
