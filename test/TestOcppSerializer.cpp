/**
 * TestOcppSerializer — Unit test per OcppMessageSerializer16J.
 *
 * Proprietà validate:
 *   P1: Round-trip serializzazione/deserializzazione OCPP
 *   P6: Rifiuto messaggi OCPP non validi
 *
 * Requisiti: 4.1, 4.2, 4.3, 4.4, 11.1, 11.2, 11.4
 */
#include "TestHarness.h"
#include "app/OcppMessageSerializer16J.h"

#include <Poco/JSON/Object.h>

using Ser  = OcppMessageSerializer16J;
using Msg  = Ser::OcppMessage;
using Type = Ser::MessageType;

// ---------------------------------------------------------------------------
// Helper: confronta due OcppMessage
// ---------------------------------------------------------------------------
static void assertMessagesEqual(const Msg& a, const Msg& b)
{
    ASSERT_EQ(static_cast<int>(a.type), static_cast<int>(b.type));
    ASSERT_EQ(a.uniqueId, b.uniqueId);
    ASSERT_EQ(a.action,   b.action);
    ASSERT_EQ(a.errorCode,        b.errorCode);
    ASSERT_EQ(a.errorDescription, b.errorDescription);

    // Confronto payload tramite ri-serializzazione JSON
    std::ostringstream ossA, ossB;
    a.payload.stringify(ossA);
    b.payload.stringify(ossB);
    ASSERT_EQ(ossA.str(), ossB.str());
}

// ---------------------------------------------------------------------------
// Helper: round-trip generico
// ---------------------------------------------------------------------------
static void roundTrip(const Msg& original)
{
    std::string json = Ser::serialize(original);
    Msg restored     = Ser::deserialize(json);
    assertMessagesEqual(original, restored);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — BootNotification
// Validates: Requirements 4.1, 4.2, 4.4
// ============================================================================
static void testRoundTripCallBootNotification()
{
    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "boot-001";
    msg.action   = "BootNotification";
    msg.payload.set("chargePointModel",  "MiniChargePoint");
    msg.payload.set("chargePointVendor", "MiniCP");
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — StartTransaction
// ============================================================================
static void testRoundTripCallStartTransaction()
{
    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "tx-start-001";
    msg.action   = "StartTransaction";
    msg.payload.set("connectorId", 1);
    msg.payload.set("idTag",       "TESTIDTAG1");
    msg.payload.set("meterStart",  0);
    msg.payload.set("timestamp",   "2024-01-15T10:30:05.000Z");
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — MeterValues
// ============================================================================
static void testRoundTripCallMeterValues()
{
    Poco::JSON::Object sampledValue;
    sampledValue.set("value",     "194");
    sampledValue.set("measurand", "Energy.Active.Import.Register");
    sampledValue.set("unit",      "Wh");

    Poco::JSON::Array sampledArr;
    sampledArr.add(sampledValue);

    Poco::JSON::Object meterEntry;
    meterEntry.set("timestamp",    "2024-01-15T10:30:15.000Z");
    meterEntry.set("sampledValue", sampledArr);

    Poco::JSON::Array meterArr;
    meterArr.add(meterEntry);

    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "mv-001";
    msg.action   = "MeterValues";
    msg.payload.set("connectorId",   1);
    msg.payload.set("transactionId", 12345);
    msg.payload.set("meterValue",    meterArr);
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — StopTransaction
// ============================================================================
static void testRoundTripCallStopTransaction()
{
    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "tx-stop-001";
    msg.action   = "StopTransaction";
    msg.payload.set("transactionId", 12345);
    msg.payload.set("meterStop",     5000);
    msg.payload.set("timestamp",     "2024-01-15T10:45:00.000Z");
    msg.payload.set("reason",        "Local");
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — Heartbeat (payload vuoto)
// ============================================================================
static void testRoundTripCallHeartbeat()
{
    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "hb-001";
    msg.action   = "Heartbeat";
    // payload vuoto
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALL — StatusNotification
// ============================================================================
static void testRoundTripCallStatusNotification()
{
    Msg msg;
    msg.type     = Type::CALL;
    msg.uniqueId = "sn-001";
    msg.action   = "StatusNotification";
    msg.payload.set("connectorId", 1);
    msg.payload.set("status",      "Preparing");
    msg.payload.set("errorCode",   "NoError");
    msg.payload.set("timestamp",   "2024-01-15T10:30:00.000Z");
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALLRESULT
// ============================================================================
static void testRoundTripCallResult()
{
    Msg msg;
    msg.type     = Type::CALLRESULT;
    msg.uniqueId = "boot-001";
    msg.payload.set("status",      "Accepted");
    msg.payload.set("currentTime", "2024-01-15T10:00:00.000Z");
    msg.payload.set("interval",    300);
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALLERROR
// ============================================================================
static void testRoundTripCallError()
{
    Msg msg;
    msg.type             = Type::CALLERROR;
    msg.uniqueId         = "err-001";
    msg.errorCode        = "GenericError";
    msg.errorDescription = "Something went wrong";
    msg.payload.set("detail", "extra info");
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: Round-trip CALLRESULT con payload vuoto
// ============================================================================
static void testRoundTripCallResultEmptyPayload()
{
    Msg msg;
    msg.type     = Type::CALLRESULT;
    msg.uniqueId = "hb-resp-001";
    // payload vuoto (es. Heartbeat.conf senza campi extra)
    roundTrip(msg);
}

// ============================================================================
// Proprietà 1: isValid restituisce true per messaggi validi
// ============================================================================
static void testIsValidAcceptsValidMessages()
{
    ASSERT_TRUE(Ser::isValid("[2,\"id1\",\"Heartbeat\",{}]"));
    ASSERT_TRUE(Ser::isValid("[3,\"id1\",{}]"));
    ASSERT_TRUE(Ser::isValid("[4,\"id1\",\"GenericError\",\"desc\",{}]"));
}

// ============================================================================
// Proprietà 6: Rifiuto JSON vuoto
// Validates: Requirement 4.3
// ============================================================================
static void testRejectEmptyString()
{
    ASSERT_FALSE(Ser::isValid(""));
    bool threw = false;
    try { Ser::deserialize(""); } catch (const Ser::OcppParseError&) { threw = true; }
    ASSERT_TRUE(threw);
}

// ============================================================================
// Proprietà 6: Rifiuto JSON non-array
// ============================================================================
static void testRejectNonArray()
{
    ASSERT_FALSE(Ser::isValid("{\"type\":2}"));
    bool threw = false;
    try { Ser::deserialize("{\"type\":2}"); } catch (...) { threw = true; }
    ASSERT_TRUE(threw);
}

// ============================================================================
// Proprietà 6: Rifiuto array con lunghezza errata (troppo corto)
// ============================================================================
static void testRejectTooShortArray()
{
    ASSERT_FALSE(Ser::isValid("[2,\"id1\"]"));
}

// ============================================================================
// Proprietà 6: Rifiuto CALL con lunghezza errata (5 elementi)
// ============================================================================
static void testRejectCallWrongLength()
{
    ASSERT_FALSE(Ser::isValid("[2,\"id1\",\"Action\",{},\"extra\"]"));
}

// ============================================================================
// Proprietà 6: Rifiuto CALLRESULT con lunghezza errata
// ============================================================================
static void testRejectCallResultWrongLength()
{
    ASSERT_FALSE(Ser::isValid("[3,\"id1\",{},\"extra\"]"));
}

// ============================================================================
// Proprietà 6: Rifiuto CALLERROR con lunghezza errata
// ============================================================================
static void testRejectCallErrorWrongLength()
{
    ASSERT_FALSE(Ser::isValid("[4,\"id1\",\"err\",\"desc\"]"));
}

// ============================================================================
// Proprietà 6: Rifiuto tipo messaggio non valido
// ============================================================================
static void testRejectInvalidMessageType()
{
    ASSERT_FALSE(Ser::isValid("[5,\"id1\",\"Action\",{}]"));
    ASSERT_FALSE(Ser::isValid("[0,\"id1\",\"Action\",{}]"));
    ASSERT_FALSE(Ser::isValid("[1,\"id1\",\"Action\",{}]"));
}

// ============================================================================
// Proprietà 6: Rifiuto JSON malformato
// ============================================================================
static void testRejectMalformedJson()
{
    ASSERT_FALSE(Ser::isValid("[2, \"id1\", "));
    ASSERT_FALSE(Ser::isValid("not json at all"));
}

// ============================================================================
// Proprietà 6: Rifiuto CALL con payload non-oggetto
// ============================================================================
static void testRejectCallNonObjectPayload()
{
    ASSERT_FALSE(Ser::isValid("[2,\"id1\",\"Action\",\"notAnObject\"]"));
}

// ============================================================================
// main
// ============================================================================
int main()
{
    std::cout << "=== TestOcppSerializer ===\n";

    // P1: Round-trip serializzazione/deserializzazione OCPP
    runTest("P1: Round-trip CALL BootNotification",   testRoundTripCallBootNotification);
    runTest("P1: Round-trip CALL StartTransaction",   testRoundTripCallStartTransaction);
    runTest("P1: Round-trip CALL MeterValues",        testRoundTripCallMeterValues);
    runTest("P1: Round-trip CALL StopTransaction",    testRoundTripCallStopTransaction);
    runTest("P1: Round-trip CALL Heartbeat",          testRoundTripCallHeartbeat);
    runTest("P1: Round-trip CALL StatusNotification", testRoundTripCallStatusNotification);
    runTest("P1: Round-trip CALLRESULT",              testRoundTripCallResult);
    runTest("P1: Round-trip CALLERROR",               testRoundTripCallError);
    runTest("P1: Round-trip CALLRESULT payload vuoto",testRoundTripCallResultEmptyPayload);
    runTest("P1: isValid accetta messaggi validi",    testIsValidAcceptsValidMessages);

    // P6: Rifiuto messaggi OCPP non validi
    runTest("P6: Rifiuto stringa vuota",              testRejectEmptyString);
    runTest("P6: Rifiuto JSON non-array",             testRejectNonArray);
    runTest("P6: Rifiuto array troppo corto",         testRejectTooShortArray);
    runTest("P6: Rifiuto CALL lunghezza errata",      testRejectCallWrongLength);
    runTest("P6: Rifiuto CALLRESULT lunghezza errata",testRejectCallResultWrongLength);
    runTest("P6: Rifiuto CALLERROR lunghezza errata", testRejectCallErrorWrongLength);
    runTest("P6: Rifiuto tipo messaggio non valido",  testRejectInvalidMessageType);
    runTest("P6: Rifiuto JSON malformato",            testRejectMalformedJson);
    runTest("P6: Rifiuto CALL payload non-oggetto",   testRejectCallNonObjectPayload);

    std::cout << "\nRisultati: " << g_passed << " passed, "
              << g_failed << " failed\n";

    return g_failed > 0 ? 1 : 0;
}
