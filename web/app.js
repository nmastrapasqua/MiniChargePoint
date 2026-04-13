/**
 * app.js — MiniChargePoint Web Interface
 *
 * Connessione WebSocket all'Application_Layer, invio comandi JSON,
 * ricezione e visualizzazione aggiornamenti di stato.
 * Riconnessione automatica ogni 3 secondi se la connessione viene persa.
 * I pulsanti vengono abilitati/disabilitati in base allo stato del connettore.
 *
 * Requisiti validati: 6.2, 6.3, 6.4, 6.5, 6.6, 6.7
 */
(function () {
  "use strict";

  // --- DOM refs ---
  var elWsStatus       = document.getElementById("ws-status");
  var elConnectorState = document.getElementById("connector-state");
  var elMeterValue     = document.getElementById("meter-value");
  var elTransactionId  = document.getElementById("transaction-id");
  var elIdTag          = document.getElementById("id-tag");
  var elCsStatus       = document.getElementById("cs-status");
  var elFwStatus       = document.getElementById("fw-status");
  var inputIdTag       = document.getElementById("input-idtag");

  var btnPlugIn      = document.getElementById("btn-plug-in");
  var btnPlugOut     = document.getElementById("btn-plug-out");
  var btnStartCharge = document.getElementById("btn-start-charge");
  var btnStopCharge  = document.getElementById("btn-stop-charge");
  var btnErrHw       = document.getElementById("btn-err-hw");
  var btnErrTamper   = document.getElementById("btn-err-tamper");
  var btnClearErr    = document.getElementById("btn-clear-err");

  var RECONNECT_MS = 3000;
  var ws = null;
  var reconnectTimer = null;

  // --- WebSocket ---
  function connect() {
    var protocol = location.protocol === "https:" ? "wss:" : "ws:";
    var url = protocol + "//" + location.host + "/";
    ws = new WebSocket(url);

    ws.onopen = function () { setWsStatus(true); };
    ws.onclose = function () { setWsStatus(false); scheduleReconnect(); };
    ws.onerror = function () { setWsStatus(false); };

    ws.onmessage = function (evt) {
      try {
        var msg = JSON.parse(evt.data);
        if (msg.type === "status_update") applyStatus(msg);
      } catch (e) {}
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null;
      connect();
    }, RECONNECT_MS);
  }

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(obj));
    }
  }

  // --- UI updates ---
  function setWsStatus(connected) {
    elWsStatus.textContent = connected ? "Connesso" : "Disconnesso";
    elWsStatus.className   = "badge " + (connected ? "connected" : "disconnected");
  }

  function applyStatus(s) {
    var state = s.connectorState || "Available";
    elConnectorState.textContent = state;
    elConnectorState.className   = "state-badge " + state.toLowerCase();

    elMeterValue.textContent = s.meterValue != null ? s.meterValue : 0;

    elTransactionId.textContent = (s.transactionId && s.transactionId > 0) ? s.transactionId : "—";
    elIdTag.textContent         = s.idTag || "—";

    var csConn = !!s.centralSystemConnected;
    elCsStatus.textContent = csConn ? "Connesso" : "Disconnesso";
    elCsStatus.className   = "badge " + (csConn ? "connected" : "disconnected");

    var fwConn = !!s.firmwareConnected;
    elFwStatus.textContent = fwConn ? "Connesso" : "Disconnesso";
    elFwStatus.className   = "badge " + (fwConn ? "connected" : "disconnected");

    updateButtons(state, !!s.firmwareConnected, csConn);
  }

  function setLoading(btn) {
    btn.classList.add("loading");
    btn.disabled = true;
  }

  function clearAllLoading() {
    var all = [btnPlugIn, btnPlugOut, btnStartCharge, btnStopCharge,
               btnErrHw, btnErrTamper, btnClearErr];
    for (var i = 0; i < all.length; i++) all[i].classList.remove("loading");
  }

  /**
   * Abilita/disabilita i pulsanti in base allo stato del connettore.
   * Firmware disconnesso → tutti disabilitati.
   * Central System disconnesso → Start Charge e Stop Charge disabilitati.
   *
   * Available:  Plug In, HW Fault, Tamper
   * Preparing:  Plug Out, Start Charge*, HW Fault, Tamper
   * Charging:   Stop Charge, HW Fault, Tamper
   * Finishing:  Plug Out
   * Faulted:    Clear Error
   * (* = richiede Central System connesso)
   */
  function updateButtons(state, fwConnected, csConnected) {
    clearAllLoading();
    var all = [btnPlugIn, btnPlugOut, btnStartCharge, btnStopCharge,
               btnErrHw, btnErrTamper, btnClearErr, inputIdTag];
    for (var i = 0; i < all.length; i++) all[i].disabled = true;

    if (!fwConnected) return;

    switch (state) {
      case "Available":
        btnPlugIn.disabled = false;
        btnErrHw.disabled = false;
        btnErrTamper.disabled = false;
        break;
      case "Preparing":
        btnPlugOut.disabled = false;
        if (csConnected) {
          btnStartCharge.disabled = false;
          inputIdTag.disabled = false;
        }
        btnErrHw.disabled = false;
        btnErrTamper.disabled = false;
        break;
      case "Charging":
        btnStopCharge.disabled = false;
        btnErrHw.disabled = false;
        btnErrTamper.disabled = false;
        break;
      case "Finishing":
        btnPlugOut.disabled = false;
        break;
      case "Faulted":
        btnClearErr.disabled = false;
        break;
    }
  }

  // --- Button handlers ---
  btnPlugIn.addEventListener("click", function () {
    setLoading(btnPlugIn);
    send({ command: "plug_in" });
  });
  btnPlugOut.addEventListener("click", function () {
    setLoading(btnPlugOut);
    send({ command: "plug_out" });
  });
  btnStartCharge.addEventListener("click", function () {
    var tag = inputIdTag.value.trim() || "TESTIDTAG1";
    setLoading(btnStartCharge);
    send({ command: "start_charge", idTag: tag });
  });
  btnStopCharge.addEventListener("click", function () {
    setLoading(btnStopCharge);
    send({ command: "stop_charge" });
  });
  btnErrHw.addEventListener("click", function () {
    setLoading(btnErrHw);
    send({ command: "trigger_error", errorType: "HardwareFault" });
  });
  btnErrTamper.addEventListener("click", function () {
    setLoading(btnErrTamper);
    send({ command: "trigger_error", errorType: "TamperDetection" });
  });
  btnClearErr.addEventListener("click", function () {
    setLoading(btnClearErr);
    send({ command: "clear_error" });
  });

  // --- Init ---
  updateButtons("Available");
  connect();
})();
