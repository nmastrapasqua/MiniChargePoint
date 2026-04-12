/**
 * TestSessionManager — Unit test per SessionManager (architettura a code).
 *
 * Il SessionManager riceve SessionEvent dalla eventQueue e produce:
 *   - CentralSystemEvent nella csysQueue (messaggi OCPP)
 *   - stringhe JSON nella ipcQueue (comandi al firmware)
 *   - stringhe JSON nella uiQueue (aggiornamenti UI)
 *
 * I test pushano eventi, avviano il SessionManager, e verificano
 * cosa esce dalle code di output.
 *
 * Proprietà validate:
 *   P5:  Inoltro MeterValues al Central System durante sessione attiva
 *   P12: Authorize prima di StartTransaction
 *   P13: Gestione comandi remoti (RemoteStart/RemoteStop)
 *   Extra: Errore durante sessione attiva → StopTransaction con EmergencyStop
 */
#include "TestHarness.h"
#include "app/SessionManager.h"
#include "common/SessionEvent.h"
#include "common/CentralSystemEvent.h"
#include "common/ThreadSafeQueue.h"

#include <vector>
#include <string>
#include <chrono>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

// ============================================================================
// Helper: drena tutti i CentralSystemEvent dalla coda
// ============================================================================

static std::vector<CentralSystemEvent> drainCsys(ThreadSafeQueue<CentralSystemEvent>& q)
{
    std::vector<CentralSystemEvent> result;
    while (auto evt = q.try_pop()) {
        result.push_back(std::move(*evt));
    }
    return result;
}

// ============================================================================
// Helper: drena tutte le stringhe dalla coda IPC
// ============================================================================

static std::vector<std::string> drainIpc(ThreadSafeQueue<std::string>& q)
{
    std::vector<std::string> result;
    while (auto msg = q.try_pop()) {
        result.push_back(std::move(*msg));
    }
    return result;
}

// ============================================================================
// Helper: parsa un comando IPC JSON e restituisce l'action
// ============================================================================

static std::string getIpcAction(const std::string& json)
{
    Poco::JSON::Parser parser;
    auto obj = parser.parse(json).extract<Poco::JSON::Object::Ptr>();
    return obj->optValue<std::string>("action", "");
}

// ============================================================================
// Helper: filtra CentralSystemEvent per tipo
// ============================================================================

static std::vector<CentralSystemEvent> filterByType(
    const std::vector<CentralSystemEvent>& events, CentralSystemEvent::Type type)
{
    std::vector<CentralSystemEvent> result;
    for (const auto& e : events) {
        if (e.type == type) result.push_back(e);
    }
    return result;
}

// ============================================================================
// Helper: push un evento e attende che venga processato
// ============================================================================

static void pushAndWait(ThreadSafeQueue<SessionEvent>& q, SessionEvent evt)
{
    q.push(std::move(evt));
    Poco::Thread::sleep(100);
}

// ============================================================================
// Helper: simula una sessione attiva
// ============================================================================

struct TestQueues {
    ThreadSafeQueue<SessionEvent> eventQ;
    ThreadSafeQueue<std::string> uiQ;
    ThreadSafeQueue<std::string> ipcQ;
    ThreadSafeQueue<CentralSystemEvent> csysQ;
};

static void setupActiveSession(TestQueues& tq, SessionManager& sm,
                               const std::string& idTag = "TAG1", int txId = 100)
{
    sm.start();

    // Connettore → Preparing
    SessionEvent prepEvt;
    prepEvt.type = SessionEvent::Type::ConnectorStateChanged;
    prepEvt.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(prepEvt));

    // Start charge → Authorize
    SessionEvent startEvt;
    startEvt.type = SessionEvent::Type::WebStartCharge;
    startEvt.stringParam = idTag;
    pushAndWait(tq.eventQ, std::move(startEvt));

    // Authorize.conf Accepted
    SessionEvent authEvt;
    authEvt.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Accepted");
    authResp.set("idTagInfo", idTagInfo);
    authEvt.jsonParam = authResp;
    pushAndWait(tq.eventQ, std::move(authEvt));

    // Connettore → Charging
    SessionEvent chargingEvt;
    chargingEvt.type = SessionEvent::Type::ConnectorStateChanged;
    chargingEvt.stringParam = "Charging";
    pushAndWait(tq.eventQ, std::move(chargingEvt));

    // StartTransaction.conf
    SessionEvent txEvt;
    txEvt.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object txResp;
    txResp.set("action", "StartTransaction");
    txResp.set("transactionId", txId);
    txEvt.jsonParam = txResp;
    pushAndWait(tq.eventQ, std::move(txEvt));

    // Drain le code per partire puliti
    drainCsys(tq.csysQ);
    drainIpc(tq.ipcQ);
    while (tq.uiQ.try_pop().has_value()) {}
}

// ============================================================================
// P5: Inoltro MeterValues al Central System
// ============================================================================

static void testP5_MeterForwardedDuringActiveSession()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm);

    for (int v : {500, 1000, 1500}) {
        SessionEvent evt;
        evt.type = SessionEvent::Type::MeterValue;
        evt.intParam = v;
        pushAndWait(tq.eventQ, std::move(evt));
    }

    auto csEvents = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues);
    ASSERT_EQ(3, static_cast<int>(csEvents.size()));
    ASSERT_EQ(500,  csEvents[0].meterValue);
    ASSERT_EQ(1000, csEvents[1].meterValue);
    ASSERT_EQ(1500, csEvents[2].meterValue);
    ASSERT_EQ(100,  csEvents[0].transactionId);

    tq.eventQ.close();
    sm.stop();
}

static void testP5_MeterNotForwardedWhenNotCharging()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent evt;
    evt.type = SessionEvent::Type::MeterValue;
    evt.intParam = 500;
    pushAndWait(tq.eventQ, std::move(evt));

    auto csEvents = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues);
    ASSERT_EQ(0, static_cast<int>(csEvents.size()));

    tq.eventQ.close();
    sm.stop();
}

static void testP5_MeterNotForwardedAfterSessionEnds()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm);

    // Finishing → Available
    SessionEvent finEvt;
    finEvt.type = SessionEvent::Type::ConnectorStateChanged;
    finEvt.stringParam = "Finishing";
    pushAndWait(tq.eventQ, std::move(finEvt));

    SessionEvent availEvt;
    availEvt.type = SessionEvent::Type::ConnectorStateChanged;
    availEvt.stringParam = "Available";
    pushAndWait(tq.eventQ, std::move(availEvt));

    drainCsys(tq.csysQ); // drain StopTransaction etc.

    SessionEvent meterEvt;
    meterEvt.type = SessionEvent::Type::MeterValue;
    meterEvt.intParam = 200;
    pushAndWait(tq.eventQ, std::move(meterEvt));

    auto csEvents = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues);
    ASSERT_EQ(0, static_cast<int>(csEvents.size()));

    tq.eventQ.close();
    sm.stop();
}

// ============================================================================
// P12: Authorize prima di StartTransaction
// ============================================================================

static void testP12_AuthorizeSentBeforeStartTransaction()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    // Preparing
    SessionEvent prepEvt;
    prepEvt.type = SessionEvent::Type::ConnectorStateChanged;
    prepEvt.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(prepEvt));
    drainCsys(tq.csysQ);

    // Start charge
    SessionEvent startEvt;
    startEvt.type = SessionEvent::Type::WebStartCharge;
    startEvt.stringParam = "MYTAG";
    pushAndWait(tq.eventQ, std::move(startEvt));

    auto csEvents = drainCsys(tq.csysQ);
    auto auths = filterByType(csEvents, CentralSystemEvent::Type::Authorize);
    auto starts = filterByType(csEvents, CentralSystemEvent::Type::StartTransaction);

    ASSERT_EQ(1, static_cast<int>(auths.size()));
    ASSERT_EQ(std::string("MYTAG"), auths[0].idTag);
    ASSERT_EQ(0, static_cast<int>(starts.size()));

    tq.eventQ.close();
    sm.stop();
}

static void testP12_StartTransactionAfterAuthorizeAccepted()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent prepEvt;
    prepEvt.type = SessionEvent::Type::ConnectorStateChanged;
    prepEvt.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(prepEvt));

    SessionEvent startEvt;
    startEvt.type = SessionEvent::Type::WebStartCharge;
    startEvt.stringParam = "MYTAG";
    pushAndWait(tq.eventQ, std::move(startEvt));
    drainCsys(tq.csysQ);

    // Authorize Accepted
    SessionEvent authEvt;
    authEvt.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Accepted");
    authResp.set("idTagInfo", idTagInfo);
    authEvt.jsonParam = authResp;
    pushAndWait(tq.eventQ, std::move(authEvt));

    auto csEvents = drainCsys(tq.csysQ);
    auto starts = filterByType(csEvents, CentralSystemEvent::Type::StartTransaction);
    ASSERT_EQ(1, static_cast<int>(starts.size()));
    ASSERT_EQ(std::string("MYTAG"), starts[0].idTag);

    tq.eventQ.close();
    sm.stop();
}

static void testP12_NoStartTransactionIfAuthorizeRejected()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent prepEvt;
    prepEvt.type = SessionEvent::Type::ConnectorStateChanged;
    prepEvt.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(prepEvt));

    SessionEvent startEvt;
    startEvt.type = SessionEvent::Type::WebStartCharge;
    startEvt.stringParam = "BADTAG";
    pushAndWait(tq.eventQ, std::move(startEvt));
    drainCsys(tq.csysQ);

    SessionEvent authEvt;
    authEvt.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object authResp;
    Poco::JSON::Object::Ptr idTagInfo = new Poco::JSON::Object;
    idTagInfo->set("status", "Blocked");
    authResp.set("idTagInfo", idTagInfo);
    authEvt.jsonParam = authResp;
    pushAndWait(tq.eventQ, std::move(authEvt));

    auto starts = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::StartTransaction);
    ASSERT_EQ(0, static_cast<int>(starts.size()));

    tq.eventQ.close();
    sm.stop();
}

// ============================================================================
// P13: Gestione comandi remoti
// ============================================================================

static void testP13_RemoteStartAvailable()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent evt;
    evt.type = SessionEvent::Type::RemoteCommand;
    evt.stringParam = "RemoteStartTransaction";
    evt.stringParam2 = "uid-001";
    Poco::JSON::Object payload;
    payload.set("idTag", "REMOTETAG");
    payload.set("connectorId", 1);
    evt.jsonParam = payload;
    pushAndWait(tq.eventQ, std::move(evt));

    auto csEvents = drainCsys(tq.csysQ);

    // CallResult Accepted
    auto results = filterByType(csEvents, CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, static_cast<int>(results.size()));
    ASSERT_EQ(std::string("uid-001"), results[0].uniqueId);
    std::string st = results[0].payload.optValue<std::string>("status", "");
    ASSERT_EQ(std::string("Accepted"), st);

    // Authorize inviato
    auto auths = filterByType(csEvents, CentralSystemEvent::Type::Authorize);
    ASSERT_EQ(1, static_cast<int>(auths.size()));
    ASSERT_EQ(std::string("REMOTETAG"), auths[0].idTag);

    // plug_in inviato via IPC
    auto ipcMsgs = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipcMsgs.size() >= 1);
    ASSERT_EQ(std::string("plug_in"), getIpcAction(ipcMsgs[0]));

    tq.eventQ.close();
    sm.stop();
}

static void testP13_RemoteStartNotAvailable()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    // Porta a Preparing
    SessionEvent prepEvt;
    prepEvt.type = SessionEvent::Type::ConnectorStateChanged;
    prepEvt.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(prepEvt));
    drainCsys(tq.csysQ);
    drainIpc(tq.ipcQ);

    SessionEvent evt;
    evt.type = SessionEvent::Type::RemoteCommand;
    evt.stringParam = "RemoteStartTransaction";
    evt.stringParam2 = "uid-002";
    Poco::JSON::Object payload;
    payload.set("idTag", "REMOTETAG");
    evt.jsonParam = payload;
    pushAndWait(tq.eventQ, std::move(evt));

    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, static_cast<int>(results.size()));
    std::string st = results[0].payload.optValue<std::string>("status", "");
    ASSERT_EQ(std::string("Rejected"), st);

    ASSERT_EQ(0, static_cast<int>(drainIpc(tq.ipcQ).size()));

    tq.eventQ.close();
    sm.stop();
}

static void testP13_RemoteStopMatchingTxId()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm, "TAG1", 42);

    SessionEvent evt;
    evt.type = SessionEvent::Type::RemoteCommand;
    evt.stringParam = "RemoteStopTransaction";
    evt.stringParam2 = "uid-010";
    Poco::JSON::Object payload;
    payload.set("transactionId", 42);
    evt.jsonParam = payload;
    pushAndWait(tq.eventQ, std::move(evt));

    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, static_cast<int>(results.size()));
    std::string st = results[0].payload.optValue<std::string>("status", "");
    ASSERT_EQ(std::string("Accepted"), st);

    // stop_charge inviato subito
    auto ipcMsgs = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipcMsgs.size() >= 1);
    ASSERT_EQ(std::string("stop_charge"), getIpcAction(ipcMsgs[0]));

    // plug_out viene inviato dopo Finishing (con delay di 1s nel SessionManager)
    SessionEvent finEvt;
    finEvt.type = SessionEvent::Type::ConnectorStateChanged;
    finEvt.stringParam = "Finishing";
    tq.eventQ.push(std::move(finEvt));
    Poco::Thread::sleep(1500);  // attende delay + processing

    ipcMsgs = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipcMsgs.size() >= 1);
    ASSERT_EQ(std::string("plug_out"), getIpcAction(ipcMsgs[0]));

    tq.eventQ.close();
    sm.stop();
}

static void testP13_RemoteStopWrongTxId()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm, "TAG1", 42);

    SessionEvent evt;
    evt.type = SessionEvent::Type::RemoteCommand;
    evt.stringParam = "RemoteStopTransaction";
    evt.stringParam2 = "uid-011";
    Poco::JSON::Object payload;
    payload.set("transactionId", 999);
    evt.jsonParam = payload;
    pushAndWait(tq.eventQ, std::move(evt));

    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, static_cast<int>(results.size()));
    std::string st = results[0].payload.optValue<std::string>("status", "");
    ASSERT_EQ(std::string("Rejected"), st);

    ASSERT_EQ(0, static_cast<int>(drainIpc(tq.ipcQ).size()));

    tq.eventQ.close();
    sm.stop();
}

static void testP13_RemoteStopNoActiveSession()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent evt;
    evt.type = SessionEvent::Type::RemoteCommand;
    evt.stringParam = "RemoteStopTransaction";
    evt.stringParam2 = "uid-012";
    Poco::JSON::Object payload;
    payload.set("transactionId", 1);
    evt.jsonParam = payload;
    pushAndWait(tq.eventQ, std::move(evt));

    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, static_cast<int>(results.size()));
    std::string st = results[0].payload.optValue<std::string>("status", "");
    ASSERT_EQ(std::string("Rejected"), st);

    tq.eventQ.close();
    sm.stop();
}

// ============================================================================
// Extra: Errore durante sessione attiva → StopTransaction con EmergencyStop
// ============================================================================

static void testEmergencyStop_FaultDuringCharging()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm, "TAG1", 77);

    // Error
    SessionEvent errEvt;
    errEvt.type = SessionEvent::Type::Error;
    errEvt.stringParam = "HardwareFault";
    pushAndWait(tq.eventQ, std::move(errEvt));

    // Faulted
    SessionEvent faultEvt;
    faultEvt.type = SessionEvent::Type::ConnectorStateChanged;
    faultEvt.stringParam = "Faulted";
    faultEvt.stringParam2 = "HardwareFault";
    pushAndWait(tq.eventQ, std::move(faultEvt));

    auto csEvents = drainCsys(tq.csysQ);

    // StatusNotification Faulted
    auto statuses = filterByType(csEvents, CentralSystemEvent::Type::StatusNotification);
    bool foundFaulted = false;
    for (const auto& s : statuses) {
        if (s.status == "Faulted") { foundFaulted = true; break; }
    }
    ASSERT_TRUE(foundFaulted);

    // StopTransaction EmergencyStop
    auto stops = filterByType(csEvents, CentralSystemEvent::Type::StopTransaction);
    ASSERT_EQ(1, static_cast<int>(stops.size()));
    ASSERT_EQ(77, stops[0].transactionId);
    ASSERT_EQ(std::string("EmergencyStop"), stops[0].reason);

    tq.eventQ.close();
    sm.stop();
}

static void testEmergencyStop_FaultWhenNotCharging()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    sm.start();

    SessionEvent faultEvt;
    faultEvt.type = SessionEvent::Type::ConnectorStateChanged;
    faultEvt.stringParam = "Faulted";
    pushAndWait(tq.eventQ, std::move(faultEvt));

    auto csEvents = drainCsys(tq.csysQ);
    auto statuses = filterByType(csEvents, CentralSystemEvent::Type::StatusNotification);
    ASSERT_TRUE(statuses.size() >= 1);

    auto stops = filterByType(csEvents, CentralSystemEvent::Type::StopTransaction);
    ASSERT_EQ(0, static_cast<int>(stops.size()));

    tq.eventQ.close();
    sm.stop();
}

static void testEmergencyStop_FaultDuringChargingWithMeter()
{
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ);
    setupActiveSession(tq, sm, "TAG1", 55);

    // Meter values
    for (int v : {1000, 2000}) {
        SessionEvent mEvt;
        mEvt.type = SessionEvent::Type::MeterValue;
        mEvt.intParam = v;
        pushAndWait(tq.eventQ, std::move(mEvt));
    }
    drainCsys(tq.csysQ);

    // Fault
    SessionEvent errEvt;
    errEvt.type = SessionEvent::Type::Error;
    errEvt.stringParam = "TamperDetection";
    pushAndWait(tq.eventQ, std::move(errEvt));

    SessionEvent faultEvt;
    faultEvt.type = SessionEvent::Type::ConnectorStateChanged;
    faultEvt.stringParam = "Faulted";
    faultEvt.stringParam2 = "TamperDetection";
    pushAndWait(tq.eventQ, std::move(faultEvt));

    auto stops = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::StopTransaction);
    ASSERT_EQ(1, static_cast<int>(stops.size()));
    ASSERT_EQ(55, stops[0].transactionId);
    ASSERT_EQ(2000, stops[0].meterValue);
    ASSERT_EQ(std::string("EmergencyStop"), stops[0].reason);

    tq.eventQ.close();
    sm.stop();
}

// ============================================================================
// main
// ============================================================================

int main()
{
    std::cout << "=== TestSessionManager ===\n";

    runTest("P5: meter values inoltrati durante sessione attiva",
            testP5_MeterForwardedDuringActiveSession);
    runTest("P5: meter values NON inoltrati senza sessione",
            testP5_MeterNotForwardedWhenNotCharging);
    runTest("P5: meter values NON inoltrati dopo fine sessione",
            testP5_MeterNotForwardedAfterSessionEnds);

    runTest("P12: Authorize inviato prima di StartTransaction",
            testP12_AuthorizeSentBeforeStartTransaction);
    runTest("P12: StartTransaction dopo Authorize Accepted",
            testP12_StartTransactionAfterAuthorizeAccepted);
    runTest("P12: NO StartTransaction se Authorize Blocked",
            testP12_NoStartTransactionIfAuthorizeRejected);

    runTest("P13: RemoteStart con connettore Available → Accepted",
            testP13_RemoteStartAvailable);
    runTest("P13: RemoteStart con connettore non Available → Rejected",
            testP13_RemoteStartNotAvailable);
    runTest("P13: RemoteStop con txId corretto → Accepted + stop",
            testP13_RemoteStopMatchingTxId);
    runTest("P13: RemoteStop con txId errato → Rejected",
            testP13_RemoteStopWrongTxId);
    runTest("P13: RemoteStop senza sessione attiva → Rejected",
            testP13_RemoteStopNoActiveSession);

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
