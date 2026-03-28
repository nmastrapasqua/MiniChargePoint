/**
 * TestSessionManager — Unit test per SessionManager.
 *
 * Proprietà validate:
 *   P5:  Inoltro MeterValues al Central System durante sessione attiva
 *   P12: Authorize prima di StartTransaction
 *   P13: Gestione comandi remoti (RemoteStart/RemoteStop)
 *   Extra: Errore durante sessione attiva → StopTransaction con EmergencyStop
 *
 * Utilizza mock di ProtocolAdapter e IpcClient per verificare le chiamate.
 *
 * Requisiti: 3.6, 3.10, 3.11, 3.12, 3.13, 3.14, 7.4, 11.1, 11.2
 */
#include "TestHarness.h"
#include "app/SessionManager.h"
#include "app/ProtocolAdapter.h"
#include "common/IIpcSender.h"

#include <vector>
#include <string>
#include <Poco/JSON/Object.h>

// ============================================================================
// Mock ProtocolAdapter
// ============================================================================

class MockProtocolAdapter : public ProtocolAdapter {
public:
    struct AuthorizeCall   { std::string idTag; };
    struct StartTxCall     { int connId; std::string idTag; int meterStart; };
    struct StopTxCall      { int txId; int meterStop; std::string reason; };
    struct MeterValCall    { int connId; int txId; int value; };
    struct StatusNotifCall { int connId; std::string status; std::string errorCode; };
    struct CallResultCall  { std::string uniqueId; std::string status; };

    std::vector<AuthorizeCall>   authorizeCalls;
    std::vector<StartTxCall>     startTxCalls;
    std::vector<StopTxCall>      stopTxCalls;
    std::vector<MeterValCall>    meterValCalls;
    std::vector<StatusNotifCall> statusNotifCalls;
    std::vector<CallResultCall>  callResultCalls;

    void connect() override {}
    void disconnect() override {}
    bool isConnected() const override { return true; }
    void sendBootNotification(const std::string&, const std::string&) override {}
    void sendHeartbeat() override {}

    void sendAuthorize(const std::string& idTag) override {
        authorizeCalls.push_back({idTag});
    }
    void sendStatusNotification(int connId, const std::string& status,
                                const std::string& errorCode) override {
        statusNotifCalls.push_back({connId, status, errorCode});
    }
    void sendStartTransaction(int connId, const std::string& idTag,
                              int meterStart) override {
        startTxCalls.push_back({connId, idTag, meterStart});
    }
    void sendMeterValues(int connId, int txId, int value) override {
        meterValCalls.push_back({connId, txId, value});
    }
    void sendStopTransaction(int txId, int meterStop,
                             const std::string& reason) override {
        stopTxCalls.push_back({txId, meterStop, reason});
    }
    void setResponseCallback(ResponseCallback) override {}
    void setRemoteCommandCallback(RemoteCommandCallback) override {}
    void sendCallResult(const std::string& uniqueId,
                        const Poco::JSON::Object& payload) override {
        std::string st;
        if (payload.has("status")) st = payload.getValue<std::string>("status");
        callResultCalls.push_back({uniqueId, st});
    }

    void reset() {
        authorizeCalls.clear();
        startTxCalls.clear();
        stopTxCalls.clear();
        meterValCalls.clear();
        statusNotifCalls.clear();
        callResultCalls.clear();
    }
};

// ============================================================================
// Mock IpcClient — overrides virtual sendMessage to record commands
// ============================================================================

class MockIpcClient : public IIpcSender {
public:
    struct SentCmd { std::string action; std::string errorType; };
    std::vector<SentCmd> sentCmds;

    MockIpcClient() = default;
    ~MockIpcClient() override = default;

    void sendMessage(const Poco::JSON::Object& msg) override {
        SentCmd c;
        if (msg.has("action"))    c.action    = msg.getValue<std::string>("action");
        if (msg.has("errorType")) c.errorType = msg.getValue<std::string>("errorType");
        sentCmds.push_back(c);
    }

    void reset() { sentCmds.clear(); }
};

// ============================================================================
// Helper: simula una sessione attiva (connettore Charging, transactionId assegnato)
// ============================================================================

static void setupActiveSession(SessionManager& sm, MockProtocolAdapter& proto,
                               const std::string& idTag = "TAG1", int txId = 100)
{
    // Connettore → Preparing
    sm.onConnectorStateChanged("Preparing");
    // Richiesta start charge → invia Authorize
    sm.requestStartCharge(idTag);
    // Simula Authorize.conf Accepted
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Accepted");
    authResp.set("idTagInfo", idTagInfo);
    sm.onProtocolResponse(authResp);
    // Connettore → Charging
    sm.onConnectorStateChanged("Charging");
    // Simula StartTransaction.conf con transactionId
    Poco::JSON::Object startResp;
    startResp.set("transactionId", txId);
    sm.onProtocolResponse(startResp);
    // Pulisci le chiamate registrate per partire da zero nei test
    proto.reset();
}

// ============================================================================
// Proprietà 5: Inoltro MeterValues al Central System
// Requisito 3.6
// ============================================================================

static void testP5_MeterForwardedDuringActiveSession()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto);

    // Invio meter values durante sessione attiva
    sm.onMeterValue(500);
    sm.onMeterValue(1000);
    sm.onMeterValue(1500);

    ASSERT_EQ(3, static_cast<int>(proto.meterValCalls.size()));
    ASSERT_EQ(500,  proto.meterValCalls[0].value);
    ASSERT_EQ(1000, proto.meterValCalls[1].value);
    ASSERT_EQ(1500, proto.meterValCalls[2].value);
    // Tutti con connectorId=1 e transactionId=100
    ASSERT_EQ(1,   proto.meterValCalls[0].connId);
    ASSERT_EQ(100, proto.meterValCalls[0].txId);
}

static void testP5_MeterNotForwardedWhenNotCharging()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    // Connettore Available, nessuna sessione attiva
    sm.onMeterValue(500);

    ASSERT_EQ(0, static_cast<int>(proto.meterValCalls.size()));
}

static void testP5_MeterNotForwardedAfterSessionEnds()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto);

    // Sessione termina: Finishing → Available
    sm.onConnectorStateChanged("Finishing");
    sm.onConnectorStateChanged("Available");
    proto.reset();

    // Meter value dopo la fine della sessione
    sm.onMeterValue(200);
    ASSERT_EQ(0, static_cast<int>(proto.meterValCalls.size()));
}

// ============================================================================
// Proprietà 12: Authorize prima di StartTransaction
// Requisiti 3.10, 3.11
// ============================================================================

static void testP12_AuthorizeSentBeforeStartTransaction()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    sm.onConnectorStateChanged("Preparing");
    proto.reset();

    sm.requestStartCharge("MYTAG");

    // Authorize deve essere inviato
    ASSERT_EQ(1, static_cast<int>(proto.authorizeCalls.size()));
    ASSERT_EQ(std::string("MYTAG"), proto.authorizeCalls[0].idTag);
    // StartTransaction NON ancora inviato (in attesa di Authorize.conf)
    ASSERT_EQ(0, static_cast<int>(proto.startTxCalls.size()));
}

static void testP12_StartTransactionAfterAuthorizeAccepted()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    sm.onConnectorStateChanged("Preparing");
    sm.requestStartCharge("MYTAG");
    proto.reset();

    // Simula Authorize.conf Accepted
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Accepted");
    authResp.set("idTagInfo", idTagInfo);
    sm.onProtocolResponse(authResp);

    // Ora StartTransaction deve essere stato inviato
    ASSERT_EQ(1, static_cast<int>(proto.startTxCalls.size()));
    ASSERT_EQ(std::string("MYTAG"), proto.startTxCalls[0].idTag);
    ASSERT_EQ(1, proto.startTxCalls[0].connId);
}

static void testP12_NoStartTransactionIfAuthorizeRejected()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    sm.onConnectorStateChanged("Preparing");
    sm.requestStartCharge("BADTAG");
    proto.reset();

    // Simula Authorize.conf Blocked
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Blocked");
    authResp.set("idTagInfo", idTagInfo);
    sm.onProtocolResponse(authResp);

    // StartTransaction NON deve essere inviato
    ASSERT_EQ(0, static_cast<int>(proto.startTxCalls.size()));
}

static void testP12_NoStartTransactionIfAuthorizeInvalid()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    sm.onConnectorStateChanged("Preparing");
    sm.requestStartCharge("EXPTAG");
    proto.reset();

    // Simula Authorize.conf Expired
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Expired");
    authResp.set("idTagInfo", idTagInfo);
    sm.onProtocolResponse(authResp);

    ASSERT_EQ(0, static_cast<int>(proto.startTxCalls.size()));
}

// ============================================================================
// Proprietà 13: Gestione comandi remoti
// Requisiti 3.12, 3.13, 3.14
// ============================================================================

static void testP13_RemoteStartAvailable()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    // Connettore Available (stato iniziale)
    Poco::JSON::Object payload;
    payload.set("idTag", "REMOTETAG");
    payload.set("connectorId", 1);

    sm.onRemoteCommand("RemoteStartTransaction", payload, "uid-001");

    // Deve rispondere Accepted
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("uid-001"), proto.callResultCalls[0].uniqueId);
    ASSERT_EQ(std::string("Accepted"), proto.callResultCalls[0].status);

    // Deve inviare plug_in via IPC
    ASSERT_TRUE(ipc.sentCmds.size() >= 1);
    ASSERT_EQ(std::string("plug_in"), ipc.sentCmds[0].action);

    // Deve inviare Authorize con l'idTag dal payload
    ASSERT_EQ(1, static_cast<int>(proto.authorizeCalls.size()));
    ASSERT_EQ(std::string("REMOTETAG"), proto.authorizeCalls[0].idTag);
}

static void testP13_RemoteStartNotAvailable()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    // Porta il connettore a Preparing
    sm.onConnectorStateChanged("Preparing");
    proto.reset();
    ipc.reset();

    Poco::JSON::Object payload;
    payload.set("idTag", "REMOTETAG");
    payload.set("connectorId", 1);

    sm.onRemoteCommand("RemoteStartTransaction", payload, "uid-002");

    // Deve rispondere Rejected
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("Rejected"), proto.callResultCalls[0].status);

    // NON deve inviare plug_in né Authorize
    ASSERT_EQ(0, static_cast<int>(ipc.sentCmds.size()));
    ASSERT_EQ(0, static_cast<int>(proto.authorizeCalls.size()));
}

static void testP13_RemoteStartFromCharging()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto);
    ipc.reset();

    Poco::JSON::Object payload;
    payload.set("idTag", "REMOTETAG");

    sm.onRemoteCommand("RemoteStartTransaction", payload, "uid-003");

    // Deve rispondere Rejected (connettore Charging)
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("Rejected"), proto.callResultCalls[0].status);
}

static void testP13_RemoteStopMatchingTxId()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto, "TAG1", 42);
    ipc.reset();

    Poco::JSON::Object payload;
    payload.set("transactionId", 42);

    sm.onRemoteCommand("RemoteStopTransaction", payload, "uid-010");

    // Deve rispondere Accepted
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("Accepted"), proto.callResultCalls[0].status);

    // Deve inviare stop_charge e plug_out via IPC
    ASSERT_TRUE(ipc.sentCmds.size() >= 2);
    ASSERT_EQ(std::string("stop_charge"), ipc.sentCmds[0].action);
    ASSERT_EQ(std::string("plug_out"),    ipc.sentCmds[1].action);
}

static void testP13_RemoteStopWrongTxId()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto, "TAG1", 42);
    ipc.reset();

    Poco::JSON::Object payload;
    payload.set("transactionId", 999); // non corrisponde

    sm.onRemoteCommand("RemoteStopTransaction", payload, "uid-011");

    // Deve rispondere Rejected
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("Rejected"), proto.callResultCalls[0].status);

    // NON deve inviare comandi IPC
    ASSERT_EQ(0, static_cast<int>(ipc.sentCmds.size()));
}

static void testP13_RemoteStopNoActiveSession()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    // Nessuna sessione attiva (transactionId = -1)
    Poco::JSON::Object payload;
    payload.set("transactionId", 1);

    sm.onRemoteCommand("RemoteStopTransaction", payload, "uid-012");

    // Deve rispondere Rejected
    ASSERT_EQ(1, static_cast<int>(proto.callResultCalls.size()));
    ASSERT_EQ(std::string("Rejected"), proto.callResultCalls[0].status);
}

// ============================================================================
// Extra: Errore durante sessione attiva → StopTransaction con EmergencyStop
// Requisito 7.4
// ============================================================================

static void testEmergencyStop_FaultDuringCharging()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto, "TAG1", 77);

    // Simula errore hardware: prima onError, poi cambio stato a Faulted
    sm.onError("HardwareFault", "InternalError");
    sm.onConnectorStateChanged("Faulted");

    // Deve inviare StatusNotification con Faulted
    ASSERT_TRUE(proto.statusNotifCalls.size() >= 1);
    bool foundFaulted = false;
    for (const auto& sn : proto.statusNotifCalls) {
        if (sn.status == "Faulted") { foundFaulted = true; break; }
    }
    ASSERT_TRUE(foundFaulted);

    // Deve inviare StopTransaction con reason "EmergencyStop"
    ASSERT_EQ(1, static_cast<int>(proto.stopTxCalls.size()));
    ASSERT_EQ(77, proto.stopTxCalls[0].txId);
    ASSERT_EQ(std::string("EmergencyStop"), proto.stopTxCalls[0].reason);
}

static void testEmergencyStop_FaultWhenNotCharging()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    // Connettore Available, nessuna sessione
    sm.onConnectorStateChanged("Faulted");

    // StatusNotification inviata, ma nessun StopTransaction
    ASSERT_TRUE(proto.statusNotifCalls.size() >= 1);
    ASSERT_EQ(0, static_cast<int>(proto.stopTxCalls.size()));
}

static void testEmergencyStop_FaultDuringChargingWithMeter()
{
    MockProtocolAdapter proto;
    MockIpcClient ipc;
    SessionManager sm(proto, ipc);

    setupActiveSession(sm, proto, "TAG1", 55);

    // Accumula meter values
    sm.onMeterValue(1000);
    sm.onMeterValue(2000);
    proto.reset();

    // Fault durante ricarica
    sm.onError("TamperDetection", "OtherError");
    sm.onConnectorStateChanged("Faulted");

    // StopTransaction con meterStop = ultimo valore (2000)
    ASSERT_EQ(1, static_cast<int>(proto.stopTxCalls.size()));
    ASSERT_EQ(55, proto.stopTxCalls[0].txId);
    ASSERT_EQ(2000, proto.stopTxCalls[0].meterStop);
    ASSERT_EQ(std::string("EmergencyStop"), proto.stopTxCalls[0].reason);
}

// ============================================================================
// main
// ============================================================================

int main()
{
    std::cout << "=== TestSessionManager ===\n";

    // P5: Inoltro MeterValues
    runTest("P5: meter values inoltrati durante sessione attiva",
            testP5_MeterForwardedDuringActiveSession);
    runTest("P5: meter values NON inoltrati senza sessione",
            testP5_MeterNotForwardedWhenNotCharging);
    runTest("P5: meter values NON inoltrati dopo fine sessione",
            testP5_MeterNotForwardedAfterSessionEnds);

    // P12: Authorize prima di StartTransaction
    runTest("P12: Authorize inviato prima di StartTransaction",
            testP12_AuthorizeSentBeforeStartTransaction);
    runTest("P12: StartTransaction dopo Authorize Accepted",
            testP12_StartTransactionAfterAuthorizeAccepted);
    runTest("P12: NO StartTransaction se Authorize Blocked",
            testP12_NoStartTransactionIfAuthorizeRejected);
    runTest("P12: NO StartTransaction se Authorize Expired",
            testP12_NoStartTransactionIfAuthorizeInvalid);

    // P13: Comandi remoti
    runTest("P13: RemoteStart con connettore Available → Accepted",
            testP13_RemoteStartAvailable);
    runTest("P13: RemoteStart con connettore non Available → Rejected",
            testP13_RemoteStartNotAvailable);
    runTest("P13: RemoteStart durante Charging → Rejected",
            testP13_RemoteStartFromCharging);
    runTest("P13: RemoteStop con txId corretto → Accepted + stop",
            testP13_RemoteStopMatchingTxId);
    runTest("P13: RemoteStop con txId errato → Rejected",
            testP13_RemoteStopWrongTxId);
    runTest("P13: RemoteStop senza sessione attiva → Rejected",
            testP13_RemoteStopNoActiveSession);

    // Extra: EmergencyStop
    runTest("Extra: fault durante ricarica → StopTransaction EmergencyStop",
            testEmergencyStop_FaultDuringCharging);
    runTest("Extra: fault senza sessione → nessun StopTransaction",
            testEmergencyStop_FaultWhenNotCharging);
    runTest("Extra: fault durante ricarica con meter → meterStop corretto",
            testEmergencyStop_FaultDuringChargingWithMeter);

    std::cout << "\nRisultati: " << g_passed << " passed, "
              << g_failed << " failed\n";

    return g_failed > 0 ? 1 : 0;
}
