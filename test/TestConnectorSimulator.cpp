/**
 * TestConnectorSimulator — Unit test per ConnectorSimulator, MeterGenerator, ErrorSimulator.
 *
 * Proprietà validate:
 *   P11: Transizioni di stato valide del connettore
 *   P2:  Monotonicità dei Meter Value
 *   P3:  Azzeramento meter alla fine della sessione
 *   P4:  Notifica per ogni transizione di stato
 *   P7:  Round-trip fault/clearFault del connettore
 *
 * Requisiti: 2.1, 2.2, 2.3, 2.5, 7.2, 7.6, 11.1, 11.2
 */
#include "TestHarness.h"
#include "firmware/ConnectorSimulator.h"
#include "firmware/MeterGenerator.h"
#include "firmware/ErrorSimulator.h"

#include <vector>
#include <utility>
#include <Poco/Thread.h>

using State = ConnectorSimulator::State;

// ============================================================================
// Property 11: Transizioni di stato valide del connettore
// Validates: Requirements 2.1
// ============================================================================

static void testValidTransition_PlugIn()
{
    ConnectorSimulator cs;
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
    cs.plugIn();
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(cs.getState()));
}

static void testValidTransition_StartCharging()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    ASSERT_EQ(static_cast<int>(State::Charging), static_cast<int>(cs.getState()));
}

static void testValidTransition_StopCharging()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.stopCharging();
    ASSERT_EQ(static_cast<int>(State::Finishing), static_cast<int>(cs.getState()));
}

static void testValidTransition_PlugOutFromPreparing()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.plugOut();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testValidTransition_PlugOutFromFinishing()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.stopCharging();
    cs.plugOut();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testValidTransition_FullCycle()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.stopCharging();
    cs.plugOut();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

// Invalid transitions: command rejected, state unchanged

static void testInvalid_PlugInFromPreparing()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.plugIn(); // should be rejected
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(cs.getState()));
}

static void testInvalid_PlugInFromCharging()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.plugIn();
    ASSERT_EQ(static_cast<int>(State::Charging), static_cast<int>(cs.getState()));
}

static void testInvalid_StartChargingFromAvailable()
{
    ConnectorSimulator cs;
    cs.startCharging();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testInvalid_StartChargingFromCharging()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.startCharging();
    ASSERT_EQ(static_cast<int>(State::Charging), static_cast<int>(cs.getState()));
}

static void testInvalid_StopChargingFromAvailable()
{
    ConnectorSimulator cs;
    cs.stopCharging();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testInvalid_StopChargingFromPreparing()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.stopCharging();
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(cs.getState()));
}

static void testInvalid_PlugOutFromAvailable()
{
    ConnectorSimulator cs;
    cs.plugOut();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testInvalid_PlugOutFromCharging()
{
    ConnectorSimulator cs;
    cs.plugIn();
    cs.startCharging();
    cs.plugOut();
    ASSERT_EQ(static_cast<int>(State::Charging), static_cast<int>(cs.getState()));
}

static void testInvalid_ClearFaultFromAvailable()
{
    ConnectorSimulator cs;
    cs.clearFault();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testInvalid_FaultFromFaulted()
{
    ConnectorSimulator cs;
    cs.fault();
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));
    cs.fault(); // should be rejected
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));
}

// ============================================================================
// Property 4: Notifica per ogni transizione di stato
// Validates: Requirements 2.5
// ============================================================================

static void testCallback_PlugIn()
{
    ConnectorSimulator cs;
    std::vector<std::pair<State, State>> transitions;
    cs.setStateCallback([&](State oldS, State newS) {
        transitions.push_back({oldS, newS});
    });

    cs.plugIn();
    ASSERT_EQ(1, static_cast<int>(transitions.size()));
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(transitions[0].first));
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(transitions[0].second));
}

static void testCallback_FullCycle()
{
    ConnectorSimulator cs;
    std::vector<std::pair<State, State>> transitions;
    cs.setStateCallback([&](State oldS, State newS) {
        transitions.push_back({oldS, newS});
    });

    cs.plugIn();
    cs.startCharging();
    cs.stopCharging();
    cs.plugOut();

    ASSERT_EQ(4, static_cast<int>(transitions.size()));
    // Available → Preparing
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(transitions[0].first));
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(transitions[0].second));
    // Preparing → Charging
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(transitions[1].first));
    ASSERT_EQ(static_cast<int>(State::Charging),  static_cast<int>(transitions[1].second));
    // Charging → Finishing
    ASSERT_EQ(static_cast<int>(State::Charging),  static_cast<int>(transitions[2].first));
    ASSERT_EQ(static_cast<int>(State::Finishing),  static_cast<int>(transitions[2].second));
    // Finishing → Available
    ASSERT_EQ(static_cast<int>(State::Finishing),  static_cast<int>(transitions[3].first));
    ASSERT_EQ(static_cast<int>(State::Available),  static_cast<int>(transitions[3].second));
}

static void testCallback_InvalidTransitionNoNotification()
{
    ConnectorSimulator cs;
    int callbackCount = 0;
    cs.setStateCallback([&](State, State) {
        ++callbackCount;
    });

    // All invalid from Available
    cs.startCharging();
    cs.stopCharging();
    cs.plugOut();
    cs.clearFault();

    ASSERT_EQ(0, callbackCount);
}

static void testCallback_FaultAndClear()
{
    ConnectorSimulator cs;
    std::vector<std::pair<State, State>> transitions;
    cs.setStateCallback([&](State oldS, State newS) {
        transitions.push_back({oldS, newS});
    });

    cs.fault();
    cs.clearFault();

    ASSERT_EQ(2, static_cast<int>(transitions.size()));
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(transitions[0].first));
    ASSERT_EQ(static_cast<int>(State::Faulted),   static_cast<int>(transitions[0].second));
    ASSERT_EQ(static_cast<int>(State::Faulted),   static_cast<int>(transitions[1].first));
    ASSERT_EQ(static_cast<int>(State::Available),  static_cast<int>(transitions[1].second));
}

// ============================================================================
// Property 2: Monotonicità dei Meter Value
// Validates: Requirements 2.2
// ============================================================================

static void testMeterMonotonicity()
{
    // Use a very short interval so the timer fires quickly
    MeterGenerator mg(1); // 1 second interval
    std::vector<int> values;
    Poco::Mutex valuesMutex;

    mg.setMeterCallback([&](int v) {
        Poco::Mutex::ScopedLock lock(valuesMutex);
        values.push_back(v);
    });

    mg.start();
    // Wait enough for several ticks
    Poco::Thread::sleep(3500);
    mg.stop();

    Poco::Mutex::ScopedLock lock(valuesMutex);
    ASSERT_TRUE(values.size() >= 2);

    for (size_t i = 1; i < values.size(); ++i) {
        ASSERT_TRUE(values[i] > values[i - 1]);
    }
}

static void testMeterIncrementsFromZero()
{
    MeterGenerator mg(1);
    ASSERT_EQ(0, mg.getCurrentMeterValue());

    std::vector<int> values;
    Poco::Mutex valuesMutex;

    mg.setMeterCallback([&](int v) {
        Poco::Mutex::ScopedLock lock(valuesMutex);
        values.push_back(v);
    });

    mg.start();
    Poco::Thread::sleep(1500);
    mg.stop();

    Poco::Mutex::ScopedLock lock(valuesMutex);
    ASSERT_TRUE(values.size() >= 1);
    ASSERT_TRUE(values[0] > 0);
}

// ============================================================================
// Property 3: Azzeramento meter alla fine della sessione
// Validates: Requirements 2.3
// ============================================================================

static void testMeterResetAfterSession()
{
    MeterGenerator mg(1);

    mg.start();
    Poco::Thread::sleep(1500);
    mg.stop();

    ASSERT_TRUE(mg.getCurrentMeterValue() > 0);

    mg.reset();
    ASSERT_EQ(0, mg.getCurrentMeterValue());
}

static void testMeterResetAndRestart()
{
    MeterGenerator mg(1);

    // First session
    mg.start();
    Poco::Thread::sleep(1500);
    mg.stop();
    int firstSessionValue = mg.getCurrentMeterValue();
    ASSERT_TRUE(firstSessionValue > 0);

    // Reset (connector back to Available)
    mg.reset();
    ASSERT_EQ(0, mg.getCurrentMeterValue());

    // Second session starts fresh
    mg.start();
    Poco::Thread::sleep(1500);
    mg.stop();

    // Value should be positive but independent of first session
    int secondSessionValue = mg.getCurrentMeterValue();
    ASSERT_TRUE(secondSessionValue > 0);
}

// ============================================================================
// Property 7: Round-trip fault/clearFault del connettore
// Validates: Requirements 7.2, 7.6
// ============================================================================

static void testFaultClear_FromAvailable()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    es.triggerError(ErrorSimulator::ErrorType::HardwareFault);
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));
    ASSERT_EQ(std::string("InternalError"), es.getErrorCodeOcpp());

    es.clearError();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
    ASSERT_EQ(std::string("NoError"), es.getErrorCodeOcpp());
}

static void testFaultClear_FromPreparing()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    cs.plugIn();
    ASSERT_EQ(static_cast<int>(State::Preparing), static_cast<int>(cs.getState()));

    es.triggerError(ErrorSimulator::ErrorType::TamperDetection);
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));
    ASSERT_EQ(std::string("OtherError"), es.getErrorCodeOcpp());

    es.clearError();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testFaultClear_FromCharging()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    cs.plugIn();
    cs.startCharging();
    ASSERT_EQ(static_cast<int>(State::Charging), static_cast<int>(cs.getState()));

    es.triggerError(ErrorSimulator::ErrorType::HardwareFault);
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));

    es.clearError();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testFaultClear_FromFinishing()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    cs.plugIn();
    cs.startCharging();
    cs.stopCharging();
    ASSERT_EQ(static_cast<int>(State::Finishing), static_cast<int>(cs.getState()));

    es.triggerError(ErrorSimulator::ErrorType::TamperDetection);
    ASSERT_EQ(static_cast<int>(State::Faulted), static_cast<int>(cs.getState()));

    es.clearError();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
}

static void testErrorCodeMapping()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    ASSERT_EQ(std::string("NoError"), es.getErrorCodeOcpp());

    es.triggerError(ErrorSimulator::ErrorType::HardwareFault);
    ASSERT_EQ(std::string("InternalError"), es.getErrorCodeOcpp());

    es.clearError();
    es.triggerError(ErrorSimulator::ErrorType::TamperDetection);
    ASSERT_EQ(std::string("OtherError"), es.getErrorCodeOcpp());
}

static void testClearErrorWhenNoneActive()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    // clearError with no active error should be a no-op
    es.clearError();
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
    ASSERT_EQ(std::string("NoError"), es.getErrorCodeOcpp());
}

static void testTriggerErrorNoneType()
{
    ConnectorSimulator cs;
    ErrorSimulator es(cs);

    // Triggering ErrorType::None should be a no-op
    es.triggerError(ErrorSimulator::ErrorType::None);
    ASSERT_EQ(static_cast<int>(State::Available), static_cast<int>(cs.getState()));
    ASSERT_EQ(std::string("NoError"), es.getErrorCodeOcpp());
}

// ============================================================================
// Utility: getStateString
// ============================================================================

static void testGetStateString()
{
    ConnectorSimulator cs;
    ASSERT_EQ(std::string("Available"), cs.getStateString());
    cs.plugIn();
    ASSERT_EQ(std::string("Preparing"), cs.getStateString());
    cs.startCharging();
    ASSERT_EQ(std::string("Charging"), cs.getStateString());
    cs.stopCharging();
    ASSERT_EQ(std::string("Finishing"), cs.getStateString());
    cs.plugOut();
    ASSERT_EQ(std::string("Available"), cs.getStateString());
    cs.fault();
    ASSERT_EQ(std::string("Faulted"), cs.getStateString());
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestConnectorSimulator ===\n";

    // P11: Transizioni di stato valide
    runTest("P11: plugIn Available→Preparing",              testValidTransition_PlugIn);
    runTest("P11: startCharging Preparing→Charging",        testValidTransition_StartCharging);
    runTest("P11: stopCharging Charging→Finishing",         testValidTransition_StopCharging);
    runTest("P11: plugOut Preparing→Available",             testValidTransition_PlugOutFromPreparing);
    runTest("P11: plugOut Finishing→Available",             testValidTransition_PlugOutFromFinishing);
    runTest("P11: ciclo completo",                         testValidTransition_FullCycle);
    runTest("P11: reject plugIn da Preparing",             testInvalid_PlugInFromPreparing);
    runTest("P11: reject plugIn da Charging",              testInvalid_PlugInFromCharging);
    runTest("P11: reject startCharging da Available",      testInvalid_StartChargingFromAvailable);
    runTest("P11: reject startCharging da Charging",       testInvalid_StartChargingFromCharging);
    runTest("P11: reject stopCharging da Available",       testInvalid_StopChargingFromAvailable);
    runTest("P11: reject stopCharging da Preparing",       testInvalid_StopChargingFromPreparing);
    runTest("P11: reject plugOut da Available",            testInvalid_PlugOutFromAvailable);
    runTest("P11: reject plugOut da Charging",             testInvalid_PlugOutFromCharging);
    runTest("P11: reject clearFault da Available",         testInvalid_ClearFaultFromAvailable);
    runTest("P11: reject fault da Faulted",                testInvalid_FaultFromFaulted);

    // P4: Notifica per ogni transizione
    runTest("P4: callback plugIn",                         testCallback_PlugIn);
    runTest("P4: callback ciclo completo",                 testCallback_FullCycle);
    runTest("P4: nessuna callback su transizione invalida", testCallback_InvalidTransitionNoNotification);
    runTest("P4: callback fault e clearFault",             testCallback_FaultAndClear);

    // P2: Monotonicità meter values
    runTest("P2: meter values monotonicamente crescenti",  testMeterMonotonicity);
    runTest("P2: meter incrementa da zero",                testMeterIncrementsFromZero);

    // P3: Azzeramento meter
    runTest("P3: reset meter dopo sessione",               testMeterResetAfterSession);
    runTest("P3: reset e riavvio sessione indipendente",   testMeterResetAndRestart);

    // P7: Round-trip fault/clearFault
    runTest("P7: fault/clear da Available",                testFaultClear_FromAvailable);
    runTest("P7: fault/clear da Preparing",                testFaultClear_FromPreparing);
    runTest("P7: fault/clear da Charging",                 testFaultClear_FromCharging);
    runTest("P7: fault/clear da Finishing",                testFaultClear_FromFinishing);
    runTest("P7: mappatura codici errore OCPP",            testErrorCodeMapping);
    runTest("P7: clearError senza errore attivo",          testClearErrorWhenNoneActive);
    runTest("P7: triggerError con tipo None",              testTriggerErrorNoneType);

    // Utility
    runTest("Utility: getStateString per tutti gli stati", testGetStateString);

    std::cout << "\nRisultati: " << g_passed << " passed, "
              << g_failed << " failed\n";

    return g_failed > 0 ? 1 : 0;
}
