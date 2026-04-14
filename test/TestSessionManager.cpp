/**
 * TestSessionManager — Unit test per SessionManager (architettura a code).
 *
 * Il SessionManager riceve SessionEvent dalla eventQueue e produce:
 *   - CentralSystemEvent nella csysQueue (messaggi OCPP)
 *   - stringhe JSON nella ipcQueue (comandi al firmware)
 *   - stringhe JSON nella uiQueue (aggiornamenti UI)
 *
 * I test usano remoteDelayMs=0 per evitare delay nei flussi remoti.
 */
#include "TestHarness.h"
#include "app/SessionManager.h"
#include "common/SessionEvent.h"
#include "common/CentralSystemEvent.h"
#include "common/ThreadSafeQueue.h"

#include <vector>
#include <string>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

// ============================================================================
// Helpers
// ============================================================================

static std::vector<CentralSystemEvent> drainCsys(ThreadSafeQueue<CentralSystemEvent>& q) {
    std::vector<CentralSystemEvent> r;
    while (auto e = q.try_pop()) r.push_back(std::move(*e));
    return r;
}

static std::vector<std::string> drainIpc(ThreadSafeQueue<std::string>& q) {
    std::vector<std::string> r;
    while (auto m = q.try_pop()) r.push_back(std::move(*m));
    return r;
}

static std::string getIpcAction(const std::string& json) {
    Poco::JSON::Parser p;
    auto o = p.parse(json).extract<Poco::JSON::Object::Ptr>();
    return o->optValue<std::string>("action", "");
}

static std::vector<CentralSystemEvent> filterByType(
    const std::vector<CentralSystemEvent>& events, CentralSystemEvent::Type type) {
    std::vector<CentralSystemEvent> r;
    for (const auto& e : events) if (e.type == type) r.push_back(e);
    return r;
}

static void pushAndWait(ThreadSafeQueue<SessionEvent>& q, SessionEvent evt) {
    q.push(std::move(evt));
    Poco::Thread::sleep(100);
}

struct TestQueues {
    ThreadSafeQueue<SessionEvent> eventQ;
    ThreadSafeQueue<std::string> uiQ;
    ThreadSafeQueue<std::string> ipcQ;
    ThreadSafeQueue<CentralSystemEvent> csysQ;
};

static void setupActiveSession(TestQueues& tq, SessionManager& sm,
                               const std::string& idTag = "TAG1", int txId = 100) {
    sm.start();

    SessionEvent e1; e1.type = SessionEvent::Type::ConnectorStateChanged; e1.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(e1));

    SessionEvent e2; e2.type = SessionEvent::Type::WebStartCharge; e2.stringParam = idTag;
    pushAndWait(tq.eventQ, std::move(e2));

    SessionEvent e3; e3.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object ar; Poco::JSON::Object::Ptr iti = new Poco::JSON::Object;
    iti->set("status", "Accepted"); ar.set("idTagInfo", iti); e3.jsonParam = ar;
    pushAndWait(tq.eventQ, std::move(e3));

    SessionEvent e4; e4.type = SessionEvent::Type::ConnectorStateChanged; e4.stringParam = "Charging";
    pushAndWait(tq.eventQ, std::move(e4));

    SessionEvent e5; e5.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object tr; tr.set("action", "StartTransaction"); tr.set("transactionId", txId);
    e5.jsonParam = tr;
    pushAndWait(tq.eventQ, std::move(e5));

    drainCsys(tq.csysQ); drainIpc(tq.ipcQ);
    while (tq.uiQ.try_pop().has_value()) {}
}

// ============================================================================
// P5: Inoltro MeterValues
// ============================================================================

static void testP5_MeterForwarded() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm);
    for (int v : {500, 1000, 1500}) {
        SessionEvent e; e.type = SessionEvent::Type::MeterValue; e.intParam = v;
        pushAndWait(tq.eventQ, std::move(e));
    }
    auto cs = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues);
    ASSERT_EQ(3, (int)cs.size());
    ASSERT_EQ(500, cs[0].meterValue); ASSERT_EQ(1000, cs[1].meterValue); ASSERT_EQ(1500, cs[2].meterValue);
    ASSERT_EQ(100, cs[0].transactionId);
    tq.eventQ.close(); sm.stop();
}

static void testP5_MeterNotForwardedWhenNotCharging() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent e; e.type = SessionEvent::Type::MeterValue; e.intParam = 500;
    pushAndWait(tq.eventQ, std::move(e));
    ASSERT_EQ(0, (int)filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues).size());
    tq.eventQ.close(); sm.stop();
}

static void testP5_MeterNotForwardedAfterSessionEnds() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm);
    SessionEvent f; f.type = SessionEvent::Type::ConnectorStateChanged; f.stringParam = "Finishing";
    pushAndWait(tq.eventQ, std::move(f));
    SessionEvent a; a.type = SessionEvent::Type::ConnectorStateChanged; a.stringParam = "Available";
    pushAndWait(tq.eventQ, std::move(a));
    drainCsys(tq.csysQ);
    SessionEvent m; m.type = SessionEvent::Type::MeterValue; m.intParam = 200;
    pushAndWait(tq.eventQ, std::move(m));
    ASSERT_EQ(0, (int)filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::MeterValues).size());
    tq.eventQ.close(); sm.stop();
}

// ============================================================================
// P12: Authorize prima di StartTransaction
// ============================================================================

static void testP12_AuthorizeSent() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent p; p.type = SessionEvent::Type::ConnectorStateChanged; p.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(p));
    drainCsys(tq.csysQ);
    SessionEvent s; s.type = SessionEvent::Type::WebStartCharge; s.stringParam = "MYTAG";
    pushAndWait(tq.eventQ, std::move(s));
    auto cs = drainCsys(tq.csysQ);
    ASSERT_EQ(1, (int)filterByType(cs, CentralSystemEvent::Type::Authorize).size());
    ASSERT_EQ(std::string("MYTAG"), filterByType(cs, CentralSystemEvent::Type::Authorize)[0].idTag);
    ASSERT_EQ(0, (int)filterByType(cs, CentralSystemEvent::Type::StartTransaction).size());
    tq.eventQ.close(); sm.stop();
}

static void testP12_StartAfterAuthorizeAccepted() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent p; p.type = SessionEvent::Type::ConnectorStateChanged; p.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(p));
    SessionEvent s; s.type = SessionEvent::Type::WebStartCharge; s.stringParam = "MYTAG";
    pushAndWait(tq.eventQ, std::move(s));
    drainCsys(tq.csysQ);
    SessionEvent a; a.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object ar; Poco::JSON::Object::Ptr iti = new Poco::JSON::Object;
    iti->set("status", "Accepted"); ar.set("idTagInfo", iti); a.jsonParam = ar;
    pushAndWait(tq.eventQ, std::move(a));
    auto starts = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::StartTransaction);
    ASSERT_EQ(1, (int)starts.size());
    ASSERT_EQ(std::string("MYTAG"), starts[0].idTag);
    tq.eventQ.close(); sm.stop();
}

static void testP12_NoStartIfAuthorizeRejected() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent p; p.type = SessionEvent::Type::ConnectorStateChanged; p.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(p));
    SessionEvent s; s.type = SessionEvent::Type::WebStartCharge; s.stringParam = "BADTAG";
    pushAndWait(tq.eventQ, std::move(s));
    drainCsys(tq.csysQ);
    SessionEvent a; a.type = SessionEvent::Type::ProtocolResponse;
    Poco::JSON::Object ar; Poco::JSON::Object::Ptr iti = new Poco::JSON::Object;
    iti->set("status", "Blocked"); ar.set("idTagInfo", iti); a.jsonParam = ar;
    pushAndWait(tq.eventQ, std::move(a));
    ASSERT_EQ(0, (int)filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::StartTransaction).size());
    tq.eventQ.close(); sm.stop();
}

// ============================================================================
// P13: Comandi remoti
// ============================================================================

static void testP13_RemoteStartAvailable() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent e; e.type = SessionEvent::Type::RemoteCommand;
    e.stringParam = "RemoteStartTransaction"; e.stringParam2 = "uid-001";
    Poco::JSON::Object pl; pl.set("idTag", "REMOTETAG"); pl.set("connectorId", 1);
    e.jsonParam = pl;
    pushAndWait(tq.eventQ, std::move(e));
    // Verifica CallResult Accepted (inviato subito)
    auto cs = drainCsys(tq.csysQ);
    auto results = filterByType(cs, CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, (int)results.size());
    ASSERT_EQ(std::string("Accepted"), results[0].payload.optValue<std::string>("status", ""));
    // Nessun plug_in automatico — l'utente deve farlo
    ASSERT_EQ(0, (int)drainIpc(tq.ipcQ).size());
    // Simula l'utente che fa Plug In dalla web UI
    SessionEvent pi; pi.type = SessionEvent::Type::WebPlugIn;
    pushAndWait(tq.eventQ, std::move(pi));
    auto ipc = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipc.size() >= 1);
    ASSERT_EQ(std::string("plug_in"), getIpcAction(ipc[0]));
    // Simula Preparing dal firmware
    SessionEvent p; p.type = SessionEvent::Type::ConnectorStateChanged; p.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(p));
    // Authorize deve essere inviato automaticamente
    auto auths = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::Authorize);
    ASSERT_EQ(1, (int)auths.size());
    ASSERT_EQ(std::string("REMOTETAG"), auths[0].idTag);
    tq.eventQ.close(); sm.stop();
}

static void testP13_RemoteStartNotAvailable() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent p; p.type = SessionEvent::Type::ConnectorStateChanged; p.stringParam = "Preparing";
    pushAndWait(tq.eventQ, std::move(p));
    drainCsys(tq.csysQ); drainIpc(tq.ipcQ);
    SessionEvent e; e.type = SessionEvent::Type::RemoteCommand;
    e.stringParam = "RemoteStartTransaction"; e.stringParam2 = "uid-002";
    Poco::JSON::Object pl; pl.set("idTag", "REMOTETAG"); e.jsonParam = pl;
    pushAndWait(tq.eventQ, std::move(e));
    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, (int)results.size());
    ASSERT_EQ(std::string("Rejected"), results[0].payload.optValue<std::string>("status", ""));
    ASSERT_EQ(0, (int)drainIpc(tq.ipcQ).size());
    tq.eventQ.close(); sm.stop();
}

static void testP13_RemoteStopMatching() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm, "TAG1", 42);
    SessionEvent e; e.type = SessionEvent::Type::RemoteCommand;
    e.stringParam = "RemoteStopTransaction"; e.stringParam2 = "uid-010";
    Poco::JSON::Object pl; pl.set("transactionId", 42); e.jsonParam = pl;
    pushAndWait(tq.eventQ, std::move(e));
    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, (int)results.size());
    ASSERT_EQ(std::string("Accepted"), results[0].payload.optValue<std::string>("status", ""));
    auto ipc = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipc.size() >= 1);
    ASSERT_EQ(std::string("stop_charge"), getIpcAction(ipc[0]));
    // Simula Finishing — nessun plug_out automatico, l'utente deve farlo
    SessionEvent f; f.type = SessionEvent::Type::ConnectorStateChanged; f.stringParam = "Finishing";
    pushAndWait(tq.eventQ, std::move(f));
    // Nessun plug_out automatico
    ASSERT_EQ(0, (int)drainIpc(tq.ipcQ).size());
    // L'utente fa Plug Out dalla web UI
    SessionEvent po; po.type = SessionEvent::Type::WebPlugOut;
    pushAndWait(tq.eventQ, std::move(po));
    ipc = drainIpc(tq.ipcQ);
    ASSERT_TRUE(ipc.size() >= 1);
    ASSERT_EQ(std::string("plug_out"), getIpcAction(ipc[0]));
    tq.eventQ.close(); sm.stop();
}

static void testP13_RemoteStopWrongTxId() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm, "TAG1", 42);
    SessionEvent e; e.type = SessionEvent::Type::RemoteCommand;
    e.stringParam = "RemoteStopTransaction"; e.stringParam2 = "uid-011";
    Poco::JSON::Object pl; pl.set("transactionId", 999); e.jsonParam = pl;
    pushAndWait(tq.eventQ, std::move(e));
    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, (int)results.size());
    ASSERT_EQ(std::string("Rejected"), results[0].payload.optValue<std::string>("status", ""));
    ASSERT_EQ(0, (int)drainIpc(tq.ipcQ).size());
    tq.eventQ.close(); sm.stop();
}

static void testP13_RemoteStopNoSession() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent e; e.type = SessionEvent::Type::RemoteCommand;
    e.stringParam = "RemoteStopTransaction"; e.stringParam2 = "uid-012";
    Poco::JSON::Object pl; pl.set("transactionId", 1); e.jsonParam = pl;
    pushAndWait(tq.eventQ, std::move(e));
    auto results = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::CallResult);
    ASSERT_EQ(1, (int)results.size());
    ASSERT_EQ(std::string("Rejected"), results[0].payload.optValue<std::string>("status", ""));
    tq.eventQ.close(); sm.stop();
}

// ============================================================================
// Extra: EmergencyStop
// ============================================================================

static void testEmergencyStop_FaultDuringCharging() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm, "TAG1", 77);
    SessionEvent err; err.type = SessionEvent::Type::Error; err.stringParam = "HardwareFault";
    pushAndWait(tq.eventQ, std::move(err));
    SessionEvent f; f.type = SessionEvent::Type::ConnectorStateChanged;
    f.stringParam = "Faulted"; f.stringParam2 = "HardwareFault";
    pushAndWait(tq.eventQ, std::move(f));
    auto cs = drainCsys(tq.csysQ);
    bool foundFaulted = false;
    for (const auto& s : filterByType(cs, CentralSystemEvent::Type::StatusNotification))
        if (s.status == "Faulted") { foundFaulted = true; break; }
    ASSERT_TRUE(foundFaulted);
    auto stops = filterByType(cs, CentralSystemEvent::Type::StopTransaction);
    ASSERT_EQ(1, (int)stops.size());
    ASSERT_EQ(77, stops[0].transactionId);
    ASSERT_EQ(std::string("EmergencyStop"), stops[0].reason);
    tq.eventQ.close(); sm.stop();
}

static void testEmergencyStop_FaultWhenNotCharging() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    sm.start();
    SessionEvent f; f.type = SessionEvent::Type::ConnectorStateChanged; f.stringParam = "Faulted";
    pushAndWait(tq.eventQ, std::move(f));
    auto cs = drainCsys(tq.csysQ);
    ASSERT_TRUE(filterByType(cs, CentralSystemEvent::Type::StatusNotification).size() >= 1);
    ASSERT_EQ(0, (int)filterByType(cs, CentralSystemEvent::Type::StopTransaction).size());
    tq.eventQ.close(); sm.stop();
}

static void testEmergencyStop_FaultWithMeter() {
    TestQueues tq;
    SessionManager sm(&tq.eventQ, &tq.uiQ, &tq.ipcQ, &tq.csysQ, 0);
    setupActiveSession(tq, sm, "TAG1", 55);
    for (int v : {1000, 2000}) {
        SessionEvent m; m.type = SessionEvent::Type::MeterValue; m.intParam = v;
        pushAndWait(tq.eventQ, std::move(m));
    }
    drainCsys(tq.csysQ);
    SessionEvent err; err.type = SessionEvent::Type::Error; err.stringParam = "TamperDetection";
    pushAndWait(tq.eventQ, std::move(err));
    SessionEvent f; f.type = SessionEvent::Type::ConnectorStateChanged;
    f.stringParam = "Faulted"; f.stringParam2 = "TamperDetection";
    pushAndWait(tq.eventQ, std::move(f));
    auto stops = filterByType(drainCsys(tq.csysQ), CentralSystemEvent::Type::StopTransaction);
    ASSERT_EQ(1, (int)stops.size());
    ASSERT_EQ(55, stops[0].transactionId);
    ASSERT_EQ(2000, stops[0].meterValue);
    ASSERT_EQ(std::string("EmergencyStop"), stops[0].reason);
    tq.eventQ.close(); sm.stop();
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "=== TestSessionManager ===\n";

    runTest("P5: meter values inoltrati durante sessione attiva", testP5_MeterForwarded);
    runTest("P5: meter values NON inoltrati senza sessione", testP5_MeterNotForwardedWhenNotCharging);
    runTest("P5: meter values NON inoltrati dopo fine sessione", testP5_MeterNotForwardedAfterSessionEnds);

    runTest("P12: Authorize inviato prima di StartTransaction", testP12_AuthorizeSent);
    runTest("P12: StartTransaction dopo Authorize Accepted", testP12_StartAfterAuthorizeAccepted);
    runTest("P12: NO StartTransaction se Authorize Blocked", testP12_NoStartIfAuthorizeRejected);

    runTest("P13: RemoteStart con connettore Available → Accepted", testP13_RemoteStartAvailable);
    runTest("P13: RemoteStart con connettore non Available → Rejected", testP13_RemoteStartNotAvailable);
    runTest("P13: RemoteStop con txId corretto → Accepted + stop", testP13_RemoteStopMatching);
    runTest("P13: RemoteStop con txId errato → Rejected", testP13_RemoteStopWrongTxId);
    runTest("P13: RemoteStop senza sessione attiva → Rejected", testP13_RemoteStopNoSession);

    runTest("Extra: fault durante ricarica → StopTransaction EmergencyStop", testEmergencyStop_FaultDuringCharging);
    runTest("Extra: fault senza sessione → nessun StopTransaction", testEmergencyStop_FaultWhenNotCharging);
    runTest("Extra: fault durante ricarica con meter → meterStop corretto", testEmergencyStop_FaultWithMeter);

    std::cout << "\nRisultati: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
