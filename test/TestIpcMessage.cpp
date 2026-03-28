/**
 * TestIpcMessage — Unit test per i messaggi IPC.
 *
 * Proprietà validata: P10 (round-trip trasporto IPC in formato JSON)
 * Requisiti: 1.4, 11.1
 */
#include "TestHarness.h"
#include "common/IpcMessage.h"

#include <vector>

// ============================================================================
// Feature: mini-charge-point, Property 10: Trasporto IPC in formato JSON
// Validates: Requirements 1.4, 11.1
// ============================================================================

static void testRoundTripConnectorState()
{
    IpcMessage::ConnectorStateMsg original;
    original.state     = "Preparing";
    original.timestamp = "2024-01-15T10:30:00Z";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::ConnectorStateMsg restored = IpcMessage::deserializeConnectorState(json);

    ASSERT_EQ(original.state,     restored.state);
    ASSERT_EQ(original.timestamp, restored.timestamp);
}

static void testRoundTripMeterValue()
{
    IpcMessage::MeterValueMsg original;
    original.value     = 1500;
    original.unit      = "Wh";
    original.timestamp = "2024-01-15T10:30:10Z";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::MeterValueMsg restored = IpcMessage::deserializeMeterValue(json);

    ASSERT_EQ(original.value,     restored.value);
    ASSERT_EQ(original.unit,      restored.unit);
    ASSERT_EQ(original.timestamp, restored.timestamp);
}

static void testRoundTripError()
{
    IpcMessage::ErrorMsg original;
    original.errorType   = "HardwareFault";
    original.description = "Connector hardware malfunction";
    original.timestamp   = "2024-01-15T10:31:00Z";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::ErrorMsg restored = IpcMessage::deserializeError(json);

    ASSERT_EQ(original.errorType,   restored.errorType);
    ASSERT_EQ(original.description, restored.description);
    ASSERT_EQ(original.timestamp,   restored.timestamp);
}

static void testRoundTripErrorCleared()
{
    IpcMessage::ErrorClearedMsg original;
    original.timestamp = "2024-01-15T10:32:00Z";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::ErrorClearedMsg restored = IpcMessage::deserializeErrorCleared(json);

    ASSERT_EQ(original.timestamp, restored.timestamp);
}

static void testRoundTripCommandNoErrorType()
{
    IpcMessage::CommandMsg original;
    original.action    = "plug_in";
    original.errorType = "";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::CommandMsg restored = IpcMessage::deserializeCommand(json);

    ASSERT_EQ(original.action,    restored.action);
    ASSERT_EQ(original.errorType, restored.errorType);
}

static void testRoundTripCommandWithErrorType()
{
    IpcMessage::CommandMsg original;
    original.action    = "trigger_error";
    original.errorType = "HardwareFault";

    std::string json = IpcMessage::serialize(original);
    IpcMessage::CommandMsg restored = IpcMessage::deserializeCommand(json);

    ASSERT_EQ(original.action,    restored.action);
    ASSERT_EQ(original.errorType, restored.errorType);
}

static void testGetTypeConnectorState()
{
    IpcMessage::ConnectorStateMsg msg;
    msg.state = "Available";
    msg.timestamp = "2024-01-15T10:00:00Z";
    std::string json = IpcMessage::serialize(msg);
    ASSERT_EQ(IpcMessage::TYPE_CONNECTOR_STATE, IpcMessage::getType(json));
}

static void testGetTypeMeterValue()
{
    IpcMessage::MeterValueMsg msg;
    msg.value = 0;
    msg.unit = "Wh";
    msg.timestamp = "2024-01-15T10:00:00Z";
    std::string json = IpcMessage::serialize(msg);
    ASSERT_EQ(IpcMessage::TYPE_METER_VALUE, IpcMessage::getType(json));
}

static void testGetTypeError()
{
    IpcMessage::ErrorMsg msg;
    msg.errorType = "TamperDetection";
    msg.description = "Tamper detected";
    msg.timestamp = "2024-01-15T10:00:00Z";
    std::string json = IpcMessage::serialize(msg);
    ASSERT_EQ(IpcMessage::TYPE_ERROR, IpcMessage::getType(json));
}

static void testGetTypeErrorCleared()
{
    IpcMessage::ErrorClearedMsg msg;
    msg.timestamp = "2024-01-15T10:00:00Z";
    std::string json = IpcMessage::serialize(msg);
    ASSERT_EQ(IpcMessage::TYPE_ERROR_CLEARED, IpcMessage::getType(json));
}

static void testGetTypeCommand()
{
    IpcMessage::CommandMsg msg;
    msg.action = "start_charge";
    msg.errorType = "";
    std::string json = IpcMessage::serialize(msg);
    ASSERT_EQ(IpcMessage::TYPE_COMMAND, IpcMessage::getType(json));
}

static void testAllCommandActions()
{
    const std::vector<std::string> actions = {
        "plug_in", "plug_out", "start_charge",
        "stop_charge", "trigger_error", "clear_error"
    };

    for (const auto& action : actions) {
        IpcMessage::CommandMsg original;
        original.action = action;
        original.errorType = (action == "trigger_error") ? "TamperDetection" : "";

        std::string json = IpcMessage::serialize(original);
        IpcMessage::CommandMsg restored = IpcMessage::deserializeCommand(json);

        ASSERT_EQ(original.action,    restored.action);
        ASSERT_EQ(original.errorType, restored.errorType);
    }
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestIpcMessage ===\n";

    runTest("P10: Round-trip ConnectorStateMsg",              testRoundTripConnectorState);
    runTest("P10: Round-trip MeterValueMsg",                  testRoundTripMeterValue);
    runTest("P10: Round-trip ErrorMsg",                       testRoundTripError);
    runTest("P10: Round-trip ErrorClearedMsg",                testRoundTripErrorCleared);
    runTest("P10: Round-trip CommandMsg (senza errorType)",   testRoundTripCommandNoErrorType);
    runTest("P10: Round-trip CommandMsg (con errorType)",     testRoundTripCommandWithErrorType);
    runTest("P10: getType() → connector_state",              testGetTypeConnectorState);
    runTest("P10: getType() → meter_value",                  testGetTypeMeterValue);
    runTest("P10: getType() → error",                        testGetTypeError);
    runTest("P10: getType() → error_cleared",                testGetTypeErrorCleared);
    runTest("P10: getType() → command",                      testGetTypeCommand);
    runTest("P10: Round-trip tutte le azioni di comando",    testAllCommandActions);

    std::cout << "\nRisultati: " << g_passed << " passed, "
              << g_failed << " failed\n";

    return g_failed > 0 ? 1 : 0;
}
