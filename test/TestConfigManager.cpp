/**
 * TestConfigManager — Unit test per ConfigManager (senza framework esterno).
 *
 * Harness minimale basato su assert + macro di utilità.
 * Ogni test è una funzione void; il fallimento è segnalato da assert/throw.
 *
 * Proprietà validate: P8 (round-trip config), P9 (rifiuto config non valida)
 * Requisiti: 8.1, 8.2, 8.3, 8.4, 11.1, 11.2
 */
#include "app/ConfigManager.h"

#include <Poco/TemporaryFile.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <functional>
#include <vector>

// ---------------------------------------------------------------------------
// Harness minimale
// ---------------------------------------------------------------------------
static int g_passed = 0;
static int g_failed = 0;

static void runTest(const std::string& name, std::function<void()> fn)
{
    try {
        fn();
        std::cout << "  [PASS] " << name << "\n";
        ++g_passed;
    } catch (const std::exception& ex) {
        std::cerr << "  [FAIL] " << name << " — " << ex.what() << "\n";
        ++g_failed;
    } catch (...) {
        std::cerr << "  [FAIL] " << name << " — unknown exception\n";
        ++g_failed;
    }
}

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string(#a " != " #b " → \"") + std::to_string_helper(a) + \
        "\" vs \"" + std::to_string_helper(b) + "\""); } while(0)

// Overload helpers per ASSERT_EQ
static std::string std_to_string_helper(const std::string& s) { return s; }
static std::string std_to_string_helper(int v) { return std::to_string(v); }

// Ridefinisco la macro usando gli helper corretti
#undef ASSERT_EQ
#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) throw std::runtime_error( \
        std::string(#a " != " #b " → \"") + std_to_string_helper(a) + \
        "\" vs \"" + std_to_string_helper(b) + "\""); } while(0)

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error(std::string("assertion failed: " #expr)); } while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) throw std::runtime_error(std::string("expected false: " #expr)); } while(0)

// Helper: scrive una stringa in un file temporaneo e restituisce il path
static std::string writeToTempFile(const std::string& content)
{
    Poco::TemporaryFile tmpFile;
    std::string path = tmpFile.path();
    tmpFile.keepUntilExit();
    {
        std::ofstream ofs(path);
        ofs << content;
    }
    return path;
}

// ============================================================================
// Feature: mini-charge-point, Property 8: Round-trip configurazione JSON
// Validates: Requirements 8.1, 8.2
// ============================================================================
static void testRoundTrip()
{
    ConfigManager::Config original;
    original.centralSystemUrl = "ws://example.com:9000/ocpp";
    original.chargePointId    = "CP_TEST_42";
    original.socketPath       = "/var/run/test.sock";
    original.httpPort         = 9090;
    original.meterInterval    = 30;
    original.defaultIdTag     = "ROUNDTRIPTAG";
    original.logLevel         = "debug";
    original.logFile          = "roundtrip.log";

    std::string json = ConfigManager::toJson(original);
    std::string path = writeToTempFile(json);
    ConfigManager::Config loaded = ConfigManager::load(path);

    ASSERT_EQ(original.centralSystemUrl, loaded.centralSystemUrl);
    ASSERT_EQ(original.chargePointId,    loaded.chargePointId);
    ASSERT_EQ(original.socketPath,       loaded.socketPath);
    ASSERT_EQ(original.httpPort,         loaded.httpPort);
    ASSERT_EQ(original.meterInterval,    loaded.meterInterval);
    ASSERT_EQ(original.defaultIdTag,     loaded.defaultIdTag);
    ASSERT_EQ(original.logLevel,         loaded.logLevel);
    ASSERT_EQ(original.logFile,          loaded.logFile);
}

// ============================================================================
// Feature: mini-charge-point, Property 9: Rifiuto configurazione non valida
// Validates: Requirements 8.4
// ============================================================================
static void testRejectNegativePort()
{
    ConfigManager::Config cfg;
    cfg.httpPort = -1;
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
    ASSERT_FALSE(err.empty());
}

static void testRejectZeroPort()
{
    ConfigManager::Config cfg;
    cfg.httpPort = 0;
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
}

static void testRejectEmptyUrl()
{
    ConfigManager::Config cfg;
    cfg.centralSystemUrl = "";
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
}

static void testRejectZeroInterval()
{
    ConfigManager::Config cfg;
    cfg.meterInterval = 0;
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
}

static void testRejectNegativeInterval()
{
    ConfigManager::Config cfg;
    cfg.meterInterval = -5;
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
}

static void testRejectEmptySocketPath()
{
    ConfigManager::Config cfg;
    cfg.socketPath = "";
    std::string err;
    ASSERT_FALSE(ConfigManager::validate(cfg, err));
}

static void testValidConfigAccepted()
{
    ConfigManager::Config cfg; // default → valida
    std::string err;
    ASSERT_TRUE(ConfigManager::validate(cfg, err));
    ASSERT_TRUE(err.empty());
}

// ============================================================================
// Test aggiuntivo: file non trovato → valori di default
// Validates: Requirement 8.3
// ============================================================================
static void testDefaultsOnMissingFile()
{
    ConfigManager::Config cfg = ConfigManager::load("/tmp/__nonexistent_config_12345__.json");
    ConfigManager::Config def;

    ASSERT_EQ(def.centralSystemUrl, cfg.centralSystemUrl);
    ASSERT_EQ(def.chargePointId,    cfg.chargePointId);
    ASSERT_EQ(def.socketPath,       cfg.socketPath);
    ASSERT_EQ(def.httpPort,         cfg.httpPort);
    ASSERT_EQ(def.meterInterval,    cfg.meterInterval);
    ASSERT_EQ(def.defaultIdTag,     cfg.defaultIdTag);
    ASSERT_EQ(def.logLevel,         cfg.logLevel);
    ASSERT_EQ(def.logFile,          cfg.logFile);
}

// ============================================================================
// Test aggiuntivo: tutti i campi presenti nel parsing
// Validates: Requirement 8.2
// ============================================================================
static void testAllFieldsParsed()
{
    std::string json =
        "{\n"
        "  \"centralSystemUrl\": \"ws://custom:1234/ocpp\",\n"
        "  \"chargePointId\": \"CUSTOM_CP\",\n"
        "  \"socketPath\": \"/custom/path.sock\",\n"
        "  \"httpPort\": 3000,\n"
        "  \"meterInterval\": 60,\n"
        "  \"defaultIdTag\": \"CUSTOMTAG\",\n"
        "  \"logLevel\": \"debug\",\n"
        "  \"logFile\": \"custom.log\"\n"
        "}";

    std::string path = writeToTempFile(json);
    ConfigManager::Config cfg = ConfigManager::load(path);

    ASSERT_EQ(std::string("ws://custom:1234/ocpp"), cfg.centralSystemUrl);
    ASSERT_EQ(std::string("CUSTOM_CP"),             cfg.chargePointId);
    ASSERT_EQ(std::string("/custom/path.sock"),     cfg.socketPath);
    ASSERT_EQ(3000,                                 cfg.httpPort);
    ASSERT_EQ(60,                                   cfg.meterInterval);
    ASSERT_EQ(std::string("CUSTOMTAG"),             cfg.defaultIdTag);
    ASSERT_EQ(std::string("debug"),                 cfg.logLevel);
    ASSERT_EQ(std::string("custom.log"),            cfg.logFile);
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestConfigManager ===\n";

    runTest("P8: Round-trip configurazione JSON",       testRoundTrip);
    runTest("P9: Reject porta negativa",                testRejectNegativePort);
    runTest("P9: Reject porta zero",                    testRejectZeroPort);
    runTest("P9: Reject URL vuoto",                     testRejectEmptyUrl);
    runTest("P9: Reject intervallo zero",               testRejectZeroInterval);
    runTest("P9: Reject intervallo negativo",           testRejectNegativeInterval);
    runTest("P9: Reject socketPath vuoto",              testRejectEmptySocketPath);
    runTest("P9: Config valida accettata",              testValidConfigAccepted);
    runTest("R8.3: Default su file mancante",           testDefaultsOnMissingFile);
    runTest("R8.2: Tutti i campi parsati correttamente", testAllFieldsParsed);

    std::cout << "\nRisultati: " << g_passed << " passed, "
              << g_failed << " failed\n";

    return g_failed > 0 ? 1 : 0;
}
