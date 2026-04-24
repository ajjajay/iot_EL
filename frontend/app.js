/**
 * app.js — IoT Dashboard Application
 *
 * Connects to Firebase Realtime Database, subscribes to live device data,
 * renders device cards, real-time charts, alerts, and manual controls.
 *
 * HOW TO CONFIGURE:
 *   Replace the FIREBASE_CONFIG object below with your project's config
 *   from Firebase Console → Project Settings → Web App.
 *
 * DATA FLOW:
 *   Firebase RTDB  →  onValue listeners  →  UI updates + Chart.js datasets
 */

"use strict";

// ── Firebase Configuration ────────────────────────────────────────────────────
// Replace ALL values below with your real Firebase project config.
const FIREBASE_CONFIG = {
  apiKey:            "REPLACE_WITH_YOUR_API_KEY",
  authDomain:        "REPLACE_WITH_YOUR_AUTH_DOMAIN",
  databaseURL:       "REPLACE_WITH_YOUR_DATABASE_URL",
  projectId:         "REPLACE_WITH_YOUR_PROJECT_ID",
  storageBucket:     "REPLACE_WITH_YOUR_STORAGE_BUCKET",
  messagingSenderId: "REPLACE_WITH_YOUR_MESSAGING_SENDER_ID",
  appId:             "REPLACE_WITH_YOUR_APP_ID",
};

// Dashboard credentials (create a dedicated user in Firebase Auth)
const DASH_EMAIL    = "dashboard@yourdomain.com";
const DASH_PASSWORD = "YourDashboardPassword";

// Chart history window (number of data points shown)
const MAX_CHART_POINTS = 50;

// ── State ─────────────────────────────────────────────────────────────────────
const state = {
  devices:   {},   // { deviceId: { meta, latest, heartbeat, online } }
  readings:  {},   // { deviceId: [reading, ...] }
  alerts:    [],   // [{ deviceId, ts, riskScore, mlLabel, ... }]
  charts:    {},   // Chart.js instances keyed by canvas id
  chartData: {     // rolling dataset per sensor
    temp:  { labels: [], datasets: {} },
    hum:   { labels: [], datasets: {} },
    light: { labels: [], datasets: {} },
    risk:  { labels: [], datasets: {} },
  },
  demo:       false,  // true when running without Firebase (demo mode)
};

// ── Colour palette for multi-device charts ────────────────────────────────────
const DEVICE_COLOURS = [
  "#4f8ef7", "#22d3ee", "#a855f7", "#22c55e",
  "#f97316", "#eab308", "#ec4899", "#06b6d4",
];
let deviceColourIndex = 0;
const deviceColourMap = {};

function deviceColour(id) {
  if (!deviceColourMap[id]) {
    deviceColourMap[id] = DEVICE_COLOURS[deviceColourIndex++ % DEVICE_COLOURS.length];
  }
  return deviceColourMap[id];
}

// ── Firebase init ─────────────────────────────────────────────────────────────
let db   = null;
let auth = null;

async function initFirebase() {
  try {
    firebase.initializeApp(FIREBASE_CONFIG);
    db   = firebase.database();
    auth = firebase.auth();

    await auth.signInWithEmailAndPassword(DASH_EMAIL, DASH_PASSWORD);
    console.log("[FB] Authenticated as dashboard user");
    showToast("Connected to Firebase", "ok");
    subscribeAll();
  } catch (err) {
    console.warn("[FB] Firebase init failed:", err.message);
    showToast("Firebase unavailable — running in demo mode", "info");
    state.demo = true;
    startDemoMode();
  }
}

// ── Firebase subscriptions ────────────────────────────────────────────────────
function subscribeAll() {
  // /devices — listen for any device additions / changes
  db.ref("/devices").on("value", (snap) => {
    const data = snap.val() || {};
    Object.entries(data).forEach(([id, device]) => {
      state.devices[id] = device;
    });
    renderDeviceGrid();
    renderControlsGrid();
    renderHealthTable();
    updateSummary();
    updateDeviceSelect();
  });

  // /readings — per-device time series (last 200 entries)
  db.ref("/devices").on("child_added", (snap) => {
    const deviceId = snap.key;
    db.ref(`/readings/${deviceId}`).limitToLast(200).on("child_added", (rSnap) => {
      const r = rSnap.val();
      if (!state.readings[deviceId]) state.readings[deviceId] = [];
      state.readings[deviceId].push(r);
      if (state.readings[deviceId].length > 200) state.readings[deviceId].shift();
      refreshCharts();
    });
  });

  // /alerts
  db.ref("/alerts").on("child_added", (snap) => {
    const deviceAlerts = snap.val() || {};
    Object.values(deviceAlerts).forEach((alert) => {
      if (!alert.acknowledged) {
        addAlertItem(alert);
        showToast(`⚠ Alert on ${alert.deviceId} — risk ${(alert.riskScore * 100).toFixed(0)}%`, "alert");
      }
    });
  });
}

// ── Demo mode (runs without real Firebase) ────────────────────────────────────
function startDemoMode() {
  const devices = ["esp32_node_01", "esp32_node_02"];
  const locations = ["Lab Room A", "Server Room"];

  devices.forEach((id, i) => {
    state.devices[id] = {
      meta: { deviceId: id, location: locations[i], firmware: "1.0.0" },
      online: true,
      heartbeat: { state: "MONITORING", heapFree: 215000, uptime: 300 },
      latest: {
        temperatureC: 22 + i * 5,
        humidityPct:  55 + i * 10,
        lightRaw:     2048,
        lightNorm:    0.5,
        riskScore:    i === 1 ? 0.72 : 0.12,
        mlLabel:      i === 1 ? 2 : 0,
        state:        i === 1 ? "ALERT" : "MONITORING",
        ts:           Date.now(),
      },
    };
    state.readings[id] = [];
  });

  renderDeviceGrid();
  renderControlsGrid();
  renderHealthTable();
  updateSummary();
  updateDeviceSelect();

  // Simulate live data
  setInterval(() => {
    devices.forEach((id, i) => {
      const r = {
        temperatureC: 22 + i * 5 + (Math.random() - 0.5) * 4,
        humidityPct:  55 + i * 10 + (Math.random() - 0.5) * 8,
        lightNorm:    0.4 + Math.random() * 0.4,
        riskScore:    i === 1 ? 0.6 + Math.random() * 0.3 : Math.random() * 0.3,
        ts:           Date.now(),
      };
      state.devices[id].latest = { ...state.devices[id].latest, ...r, state: i === 1 ? "ALERT" : "MONITORING" };
      if (!state.readings[id]) state.readings[id] = [];
      state.readings[id].push(r);
      if (state.readings[id].length > 200) state.readings[id].shift();
    });
    renderDeviceGrid();
    updateSummary();
    refreshCharts();
  }, 3000);
}

// ── Device grid rendering ─────────────────────────────────────────────────────
function renderDeviceGrid() {
  const grid = document.getElementById("deviceGrid");
  grid.innerHTML = "";

  const tmpl = document.getElementById("deviceCardTemplate");

  Object.entries(state.devices).forEach(([id, device]) => {
    const card = tmpl.content.cloneNode(true).querySelector(".device-card");
    card.dataset.deviceId = id;

    const latest = device.latest || {};
    const meta   = device.meta   || {};
    const online = device.online;
    const st     = latest.state || "UNKNOWN";
    const isAlert = st === "ALERT";

    // Header
    card.querySelector(".device-dot").className =
      `device-dot ${online ? (isAlert ? "alert" : "online") : "offline"}`;
    card.querySelector(".device-name").textContent     = id;
    card.querySelector(".device-location").textContent = meta.location || "—";

    const stateBadge = card.querySelector(".device-state-badge");
    stateBadge.textContent  = st;
    stateBadge.className    = `device-state-badge state-${st}`;

    if (isAlert) card.classList.add("alert-active");

    // Sensor values
    const temp  = latest.temperatureC?.toFixed(1) ?? "—";
    const hum   = latest.humidityPct?.toFixed(1)  ?? "—";
    const light = latest.lightNorm   != null ? (latest.lightNorm * 100).toFixed(0) + "%" : "—";
    const risk  = latest.riskScore   != null ? (latest.riskScore * 100).toFixed(0) + "%" : "—";

    card.querySelector(".temp-value").textContent  = temp  !== "—" ? `${temp}°C`  : "—";
    card.querySelector(".hum-value").textContent   = hum   !== "—" ? `${hum}%`   : "—";
    card.querySelector(".light-value").textContent = light;
    card.querySelector(".risk-value").textContent  = risk;

    // Colour risk value
    const riskEl = card.querySelector(".risk-value");
    const rs     = latest.riskScore ?? 0;
    riskEl.style.color = rs >= 0.6 ? "var(--risk-crit)" :
                         rs >= 0.3 ? "var(--risk-warn)" : "var(--risk-safe)";

    // Risk bar
    const fillEl = card.querySelector(".risk-bar-fill");
    fillEl.style.width      = `${Math.min(100, rs * 100)}%`;
    fillEl.style.background = rs >= 0.6 ? "var(--risk-crit)" :
                              rs >= 0.3 ? "var(--risk-warn)" : "var(--risk-safe)";

    // Last seen
    const ts = latest.ts ? new Date(latest.ts).toLocaleTimeString() : "—";
    card.querySelector(".last-seen").textContent = `Last update: ${ts}`;

    grid.appendChild(card);
  });
}

// ── Chart rendering ───────────────────────────────────────────────────────────
const CHART_CONFIGS = {
  tempChart:  { key: "temp",  field: "temperatureC", label: "°C",  colour: "#f97316" },
  humChart:   { key: "hum",   field: "humidityPct",  label: "%",   colour: "#22d3ee" },
  lightChart: { key: "light", field: "lightNorm",    label: "",    colour: "#eab308" },
  riskChart:  { key: "risk",  field: "riskScore",    label: "",    colour: "#ef4444" },
};

function initCharts() {
  const chartDefaults = {
    type: "line",
    options: {
      animation:    { duration: 200 },
      responsive:   true,
      maintainAspectRatio: false,
      interaction:  { mode: "index", intersect: false },
      plugins: {
        legend: { display: true, labels: { color: "#8b90a7", font: { size: 11 } } },
        tooltip: { backgroundColor: "#222636", titleColor: "#f0f2ff", bodyColor: "#8b90a7" },
      },
      scales: {
        x: {
          ticks: { color: "#555b7a", maxTicksLimit: 8, font: { size: 10 } },
          grid:  { color: "rgba(255,255,255,0.04)" },
        },
        y: {
          ticks: { color: "#555b7a", font: { size: 10 } },
          grid:  { color: "rgba(255,255,255,0.04)" },
        },
      },
    },
  };

  Object.entries(CHART_CONFIGS).forEach(([canvasId, cfg]) => {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;
    const chart = new Chart(canvas, {
      ...chartDefaults,
      data: { labels: [], datasets: [] },
      options: {
        ...chartDefaults.options,
        scales: {
          ...chartDefaults.options.scales,
          y: {
            ...chartDefaults.options.scales.y,
            suggestedMin: canvasId === "riskChart" ? 0 : undefined,
            suggestedMax: canvasId === "riskChart" ? 1 : undefined,
          },
        },
      },
    });
    state.charts[canvasId] = chart;
  });
}

function refreshCharts() {
  const selectedDevice = document.getElementById("deviceSelect")?.value || "all";
  const maxPoints = parseInt(document.getElementById("timeWindow")?.value || "50");

  Object.entries(CHART_CONFIGS).forEach(([canvasId, cfg]) => {
    const chart = state.charts[canvasId];
    if (!chart) return;

    chart.data.datasets = [];
    let commonLabels    = null;

    const devicesToShow = selectedDevice === "all"
      ? Object.keys(state.devices)
      : [selectedDevice];

    devicesToShow.forEach((id) => {
      const readings = (state.readings[id] || []).slice(-maxPoints);
      if (readings.length === 0) return;

      const labels = readings.map((r) =>
        r.ts ? new Date(r.ts).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }) : "?"
      );
      if (!commonLabels || labels.length > commonLabels.length) commonLabels = labels;

      const colour = deviceColour(id);
      chart.data.datasets.push({
        label:           id,
        data:            readings.map((r) => r[cfg.field] ?? null),
        borderColor:     colour,
        backgroundColor: colour + "22",
        borderWidth:     2,
        pointRadius:     0,
        tension:         0.4,
        fill:            true,
      });
    });

    chart.data.labels = commonLabels || [];
    chart.update("none");
  });

  // Update live-value badges
  const latestForDisplay = selectedDevice === "all"
    ? Object.values(state.devices)[0]?.latest
    : state.devices[selectedDevice]?.latest;

  if (latestForDisplay) {
    document.getElementById("tempNow").textContent  = `${latestForDisplay.temperatureC?.toFixed(1) ?? "—"}°C`;
    document.getElementById("humNow").textContent   = `${latestForDisplay.humidityPct?.toFixed(1)  ?? "—"}%`;
    document.getElementById("lightNow").textContent = `${((latestForDisplay.lightNorm ?? 0) * 100).toFixed(0)}%`;
    document.getElementById("riskNow").textContent  = `${((latestForDisplay.riskScore ?? 0) * 100).toFixed(0)}%`;
  }
}

// ── Summary strip ─────────────────────────────────────────────────────────────
function updateSummary() {
  const devices  = Object.values(state.devices);
  const online   = devices.filter((d) => d.online).length;
  const alerts   = devices.filter((d) => d.latest?.state === "ALERT").length;
  const temps    = devices.map((d) => d.latest?.temperatureC).filter(Number.isFinite);
  const hums     = devices.map((d) => d.latest?.humidityPct).filter(Number.isFinite);
  const avgTemp  = temps.length  ? (temps.reduce((a, b) => a + b) / temps.length).toFixed(1)   : "—";
  const avgHum   = hums.length   ? (hums.reduce((a, b) => a + b)  / hums.length).toFixed(1)   : "—";

  document.getElementById("devicesOnline").textContent = `${online}/${devices.length}`;
  document.getElementById("activeAlerts").textContent  = alerts;
  document.getElementById("avgTemp").textContent       = avgTemp !== "—" ? `${avgTemp}°C` : "—";
  document.getElementById("avgHum").textContent        = avgHum  !== "—" ? `${avgHum}%`  : "—";
  document.getElementById("lastUpdate").textContent    = new Date().toLocaleTimeString();

  // Global status badge
  const badge = document.getElementById("globalStatus");
  if (alerts > 0) {
    badge.textContent = `⚠ ${alerts} Alert${alerts > 1 ? "s" : ""}`;
    badge.className   = "status-badge status-err";
  } else if (online < devices.length) {
    badge.textContent = "Partial";
    badge.className   = "status-badge status-warn";
  } else {
    badge.textContent = "● Live";
    badge.className   = "status-badge status-ok";
  }
}

// ── Alert timeline ────────────────────────────────────────────────────────────
function addAlertItem(alert) {
  state.alerts.unshift(alert);

  const container = document.getElementById("alertTimeline");
  const noAlerts  = container.querySelector(".no-alerts");
  if (noAlerts) noAlerts.remove();

  const item = document.createElement("div");
  item.className = `alert-item ${alert.mlLabel === 1 ? "warning-item" : ""}`;
  item.innerHTML = `
    <div class="alert-device">${alert.deviceId}</div>
    <div class="alert-detail">
      Risk: <strong>${(alert.riskScore * 100).toFixed(0)}%</strong>
      | Temp: ${alert.temperatureC?.toFixed(1) ?? "—"}°C
      | Label: ${["Normal", "Warning", "Critical"][alert.mlLabel] ?? "—"}
    </div>
    <div class="alert-time">${new Date(alert.ts).toLocaleTimeString()}</div>
  `;
  container.prepend(item);
}

// ── Controls grid ─────────────────────────────────────────────────────────────
function renderControlsGrid() {
  const grid = document.getElementById("controlsGrid");
  grid.innerHTML = "";

  Object.entries(state.devices).forEach(([id, device]) => {
    const card = document.createElement("div");
    card.className = "control-card";

    const checkboxId = `relay-${id}`;
    const isRelayOn  = device.commands?.relayOverride === "ON";

    card.innerHTML = `
      <div class="control-card-title">${id}</div>
      <div class="control-card-sub">${device.meta?.location ?? ""}</div>
      <div class="toggle-row">
        <span class="toggle-label">Relay / Fan</span>
        <label class="toggle" title="Manual override — auto-expires in 5 min">
          <input type="checkbox" id="${checkboxId}" ${isRelayOn ? "checked" : ""}
                 data-device="${id}" />
          <span class="toggle-slider"></span>
        </label>
      </div>
    `;

    card.querySelector(`#${checkboxId}`).addEventListener("change", (e) => {
      const on = e.target.checked;
      sendRelayCommand(id, on ? "ON" : "OFF");
    });

    grid.appendChild(card);
  });
}

function sendRelayCommand(deviceId, value) {
  if (state.demo) {
    showToast(`[Demo] Relay ${deviceId} → ${value}`, "info");
    if (state.devices[deviceId]) {
      state.devices[deviceId].commands = { relayOverride: value };
    }
    return;
  }
  db.ref(`/devices/${deviceId}/commands/relayOverride`).set(value)
    .then(() => showToast(`Relay command sent to ${deviceId}: ${value}`, "ok"))
    .catch((e) => showToast(`Command failed: ${e.message}`, "alert"));
}

// ── Health table ──────────────────────────────────────────────────────────────
function renderHealthTable() {
  const tbody = document.getElementById("healthTableBody");
  tbody.innerHTML = "";

  if (Object.keys(state.devices).length === 0) {
    tbody.innerHTML = `<tr><td colspan="7" class="table-empty">No devices found</td></tr>`;
    return;
  }

  Object.entries(state.devices).forEach(([id, device]) => {
    const hb   = device.heartbeat || {};
    const meta = device.meta     || {};
    const tr   = document.createElement("tr");

    const uptimeSec = hb.uptime || 0;
    const uptime    = uptimeSec < 60
      ? `${uptimeSec}s`
      : uptimeSec < 3600
        ? `${Math.floor(uptimeSec / 60)}m`
        : `${Math.floor(uptimeSec / 3600)}h ${Math.floor((uptimeSec % 3600) / 60)}m`;

    const heapKb = hb.heapFree ? `${(hb.heapFree / 1024).toFixed(0)} KB` : "—";
    const lastTs = hb.ts ? new Date(hb.ts * 1000).toLocaleTimeString() : "—";
    const stCls  = `state-${hb.state || "UNKNOWN"}`;

    tr.innerHTML = `
      <td><code>${id}</code></td>
      <td>${meta.location ?? "—"}</td>
      <td><span class="device-state-badge ${stCls}">${hb.state ?? "—"}</span></td>
      <td>${uptime}</td>
      <td>${heapKb}</td>
      <td>${lastTs}</td>
      <td>${meta.firmware ?? "—"}</td>
    `;
    tbody.appendChild(tr);
  });
}

// ── Device selector ───────────────────────────────────────────────────────────
function updateDeviceSelect() {
  const sel = document.getElementById("deviceSelect");
  const current = sel.value;
  sel.innerHTML = `<option value="all">All Devices</option>`;
  Object.keys(state.devices).forEach((id) => {
    const opt = document.createElement("option");
    opt.value       = id;
    opt.textContent = id;
    sel.appendChild(opt);
  });
  if (current) sel.value = current;
}

// ── Toast ─────────────────────────────────────────────────────────────────────
function showToast(message, type = "info") {
  const container = document.getElementById("toastContainer");
  const toast     = document.createElement("div");
  const icons     = { alert: "⚠", ok: "✓", info: "ℹ" };
  toast.className  = `toast toast-${type}`;
  toast.innerHTML  = `<span>${icons[type] ?? ""}</span><span>${message}</span>`;
  container.appendChild(toast);
  setTimeout(() => toast.remove(), 5000);
}

// ── UI event bindings ─────────────────────────────────────────────────────────
document.getElementById("themeToggle").addEventListener("click", () => {
  const body  = document.body;
  const light = body.dataset.theme === "light";
  body.dataset.theme = light ? "dark" : "light";
  document.getElementById("themeToggle").textContent = light ? "☾" : "☀";
});

document.getElementById("refreshBtn").addEventListener("click", () => {
  renderDeviceGrid();
  refreshCharts();
  renderHealthTable();
  updateSummary();
  showToast("Dashboard refreshed", "info");
});

document.getElementById("clearAlertsBtn").addEventListener("click", () => {
  document.getElementById("alertTimeline").innerHTML =
    `<div class="no-alerts">No alerts yet — everything is normal</div>`;
  state.alerts = [];
});

document.getElementById("deviceSelect").addEventListener("change", refreshCharts);
document.getElementById("timeWindow").addEventListener("change", refreshCharts);

// ── Boot ──────────────────────────────────────────────────────────────────────
initCharts();
initFirebase();
