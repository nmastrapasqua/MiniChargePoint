/**
 * ConfigManager — Caricamento e validazione della configurazione da file JSON.
 *
 * Responsabilità:
 *   - Caricare la configurazione da un file JSON (con fallback ai default)
 *   - Validare i valori di configurazione
 *   - Serializzare la configurazione in formato JSON
 *
 * Requisiti validati: 8.1, 8.2, 8.3, 8.4
 */
#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <string>

class ConfigManager {
public:
    /// Struttura con tutti i parametri di configurazione e relativi default.
    struct Config {
        std::string centralSystemUrl = "ws://localhost:8180/steve/websocket/CentralSystemService";
        std::string chargePointId    = "MiniCP001";
        std::string socketPath       = "/tmp/minichargepoint.sock";
        int         httpPort         = 8080;
        int         meterInterval    = 10;        // secondi
        std::string defaultIdTag     = "TESTIDTAG1";
        std::string logLevel         = "information";
        std::string logFile          = "minichargepoint.log";
    };

    /**
     * Carica la configurazione dal file JSON indicato.
     *
     * - Se il file non esiste → restituisce i valori di default (log Warning).
     * - Se il JSON è malformato o contiene valori non validi → log Error e
     *   terminazione del processo (std::exit(1)).
     *
     * @param filePath  Percorso del file config.json
     * @return Config   Struttura con i valori caricati
     */
    static Config load(const std::string& filePath);

    /**
     * Valida una configurazione.
     *
     * Controlli: httpPort > 0, centralSystemUrl non vuoto,
     *            meterInterval > 0, socketPath non vuoto.
     *
     * @param config    Configurazione da validare
     * @param errorMsg  Messaggio di errore (output) se non valida
     * @return true se la configurazione è valida
     */
    static bool validate(const Config& config, std::string& errorMsg);

    /**
     * Serializza la configurazione in una stringa JSON.
     *
     * @param config  Configurazione da serializzare
     * @return Stringa JSON formattata
     */
    static std::string toJson(const Config& config);

private:
    static Config defaults();
};

#endif // CONFIGMANAGER_H
