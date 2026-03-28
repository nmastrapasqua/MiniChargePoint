/**
 * app.js — MiniChargePoint Web Interface
 *
 * Connessione WebSocket all'Application_Layer, invio comandi JSON,
 * ricezione e visualizzazione aggiornamenti di stato e log eventi.
 * Riconnessione automatica ogni 3 secondi se la connessione viene persa.
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
  var elLog            = document.getElementById("log-container");
  var inputIdTag       = document.getElementById("input-idtag");

  var MAX_LOG_ENTRIES = 200;
  var RECONNECT_MS   = 3000;

  var ws = null;
  var reconnectTimer = null;

  // --- WebSocket ---
  function connect() {
    var protocol = location.protocol === "https:" ? "wss:" : "ws:";
    var url = protocol + "//" + location.host + "/";

    ws = new WebSocket(url);

    ws.onopen = function () {
      setWsStatus(true);
      addLog("information", "WebSocket connesso");
    };

    ws.onclose = function () {
      setWsStatus(false);
      scheduleReconnect();
    };

    ws.onerror = function () {
      setWsStatus(false);
    };

    ws.onmessage = function (evt) {
      try {
        var msg = JSON.parse(evt.data);
        if (msg.type === "status_update") {
          applyStatus(msg);
        } else if (msg.type === "log_event") {
          addLog(msg.level, msg.message, msg.timestamp);
        }
      } catch (e) {
        // ignore malformed messages
      }
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
    // Connector state
    var state = s.connectorState || "Available";
    elConnectorState.textContent = state;
    elConnectorState.className   = "state-badge " + state.toLowerCase();

    // Meter
    elMeterValue.textContent = s.meterValue != null ? s.meterValue : 0;

    // Session
    elTransactionId.textContent = (s.transactionId && s.transactionId > 0) ? s.transactionId : "—";
    elIdTag.textContent         = s.idTag || "—";

    // Central System connection
    var csConn = !!s.centralSystemConnected;
    elCsStatus.textContent = csConn ? "Connesso" : "Disconnesso";
    elCsStatus.className   = "badge " + (csConn ? "connected" : "disconnected");

    // Firmware connection
    var fwConn = !!s.firmwareConnected;
    elFwStatus.textContent = fwConn ? "Connesso" : "Disconnesso";
    elFwStatus.className   = "badge " + (fwConn ? "connected" : "disconnected");
  }

  function addLog(level, message, timestamp) {
    var time = timestamp ? formatTime(timestamp) : formatTime(new Date().toISOString());
    var entry = document.createElement("div");
    entry.className = "log-entry";
    entry.innerHTML =
      '<span class="log-time">' + escapeHtml(time) + "</span>" +
      '<span class="log-level ' + escapeHtml(level) + '">' + escapeHtml(level) + "</span>" +
      "<span>" + escapeHtml(message) + "</span>";
    elLog.prepend(entry);

    // Trim old entries
    while (elLog.children.length > MAX_LOG_ENTRIES) {
      elLog.removeChild(elLog.lastChild);
    }
  }

  function formatTime(iso) {
    try {
      var d = new Date(iso);
      return d.toLocaleTimeString();
    } catch (e) {
      return iso;
    }
  }

  function escapeHtml(str) {
    var div = document.createElement("div");
    div.appendChild(document.createTextNode(str));
    return div.innerHTML;
  }

  // --- Button handlers ---
  document.getElementById("btn-plug-in").addEventListener("click", function () {
    send({ command: "plug_in" });
  });
  document.getElementById("btn-plug-out").addEventListener("click", function () {
    send({ command: "plug_out" });
  });
  document.getElementById("btn-start-charge").addEventListener("click", function () {
    var tag = inputIdTag.value.trim() || "TESTIDTAG1";
    send({ command: "start_charge", idTag: tag });
  });
  document.getElementById("btn-stop-charge").addEventListener("click", function () {
    send({ command: "stop_charge" });
  });
  document.getElementById("btn-err-hw").addEventListener("click", function () {
    send({ command: "trigger_error", errorType: "HardwareFault" });
  });
  document.getElementById("btn-err-tamper").addEventListener("click", function () {
    send({ command: "trigger_error", errorType: "TamperDetection" });
  });
  document.getElementById("btn-clear-err").addEventListener("click", function () {
    send({ command: "clear_error" });
  });

  // --- Init ---
  connect();
})();
