const MAX_SIGNIN_ROWS = 50;

export function makeDemoState() {
  const now = Date.now();
  const devices = {
    esp32_node_01: {
      meta:      { deviceId: "esp32_node_01", location: "Entrance A",  firmware: "2.0.0" },
      online:    true,
      heartbeat: { state: "MONITORING", heapFree: 215000, uptime: 300 },
      latest:    { temperatureC: 22, humidityPct: 55, lightRaw: 2048, lightNorm: 0.5, riskScore: 0.08, mlLabel: 0, state: "MONITORING", ts: now },
    },
    esp32_node_02: {
      meta:      { deviceId: "esp32_node_02", location: "Server Room", firmware: "2.0.0" },
      online:    true,
      heartbeat: { state: "ALERT",      heapFree: 215000, uptime: 300 },
      latest:    { temperatureC: 27, humidityPct: 65, lightRaw: 2048, lightNorm: 0.5, riskScore: 0.72, mlLabel: 2, state: "ALERT", ts: now },
    },
  };

  const users = {
    john_doe:   { userId: "john_doe",   name: "John Doe",   deviceId: "esp32_node_01", enrolledAt: now / 1000 - 86400, active: true },
    jane_smith: { userId: "jane_smith", name: "Jane Smith", deviceId: "esp32_node_01", enrolledAt: now / 1000 - 72000, active: true },
  };

  const signins = {
    esp32_node_01: [
      { userId: "john_doe",   userName: "John Doe",   matchScore: 0.12, success: true,  anomalyScore: 0.05, ts: now / 1000 - 300,  deviceId: "esp32_node_01" },
      { userId: "jane_smith", userName: "Jane Smith", matchScore: 0.18, success: true,  anomalyScore: 0.07, ts: now / 1000 - 600,  deviceId: "esp32_node_01" },
      { userId: "unknown",    userName: "Unknown",    matchScore: 0.71, success: false, anomalyScore: 0.60, ts: now / 1000 - 900,  deviceId: "esp32_node_01" },
      { userId: "unknown",    userName: "Unknown",    matchScore: 0.68, success: false, anomalyScore: 0.85, ts: now / 1000 - 1000, deviceId: "esp32_node_01" },
    ],
    esp32_node_02: [],
  };

  const readings = { esp32_node_01: [], esp32_node_02: [] };

  const alerts = [{
    deviceId:     "esp32_node_01",
    alertType:    "brute_force",
    userId:       "unknown",
    anomalyScore: 0.85,
    detail:       "3 consecutive failed iris attempts",
    ts:           now / 1000 - 1000,
  }];

  return { devices, users, signins, readings, alerts };
}

export function simulateDemoTick(prev) {
  const now     = Date.now();
  const devices = { ...prev.devices };
  const readings = { ...prev.readings };
  const signins  = { ...prev.signins };

  Object.entries(devices).forEach(([id, device], i) => {
    const r = {
      temperatureC: 22 + i * 5 + (Math.random() - 0.5) * 4,
      humidityPct:  55 + i * 10 + (Math.random() - 0.5) * 8,
      lightNorm:    0.4 + Math.random() * 0.4,
      riskScore:    i === 1 ? 0.6 + Math.random() * 0.3 : Math.random() * 0.25,
      ts:           now,
    };
    devices[id] = { ...device, latest: { ...device.latest, ...r } };
    const arr = [...(readings[id] || []), r];
    readings[id] = arr.length > 200 ? arr.slice(-200) : arr;
  });

  if (Math.random() < 0.15) {
    const success  = Math.random() > 0.3;
    const userList = Object.values(prev.users);
    const u = success && userList.length
      ? userList[Math.floor(Math.random() * userList.length)]
      : null;
    const ev = {
      userId:       u ? u.userId : "unknown",
      userName:     u ? u.name   : "Unknown",
      matchScore:   success ? 0.08 + Math.random() * 0.15 : 0.45 + Math.random() * 0.35,
      success,
      anomalyScore: success ? Math.random() * 0.25 : 0.3 + Math.random() * 0.6,
      ts:           now / 1000,
      deviceId:     "esp32_node_01",
    };
    const arr = [ev, ...(signins["esp32_node_01"] || [])];
    signins["esp32_node_01"] = arr.length > MAX_SIGNIN_ROWS ? arr.slice(0, MAX_SIGNIN_ROWS) : arr;
  }

  return { devices, readings, signins };
}
