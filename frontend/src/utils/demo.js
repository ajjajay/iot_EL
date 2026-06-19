const MAX_SIGNIN_ROWS = 50;

export function makeDemoState() {
  const now = Date.now();
  const devices = {
    esp32_node_01: {
      meta:      { deviceId: "esp32_node_01", location: "Entrance A",  firmware: "2.0.0" },
      online:    true,
      heartbeat: { state: "MONITORING", heapFree: 215000, uptime: 300 },
      latest:    { temperatureC: 22, humidityPct: 55, smokeRaw: 300, smokePct: 7.3, distanceCm: 45.2, riskScore: 0.08, mlLabel: 0, state: "MONITORING", ts: now },
    },
    esp32_node_02: {
      meta:      { deviceId: "esp32_node_02", location: "Server Room", firmware: "2.0.0" },
      online:    true,
      heartbeat: { state: "ALERT",      heapFree: 215000, uptime: 300 },
      latest:    { temperatureC: 27, humidityPct: 65, smokeRaw: 2500, smokePct: 61.0, distanceCm: 12.5, riskScore: 0.72, mlLabel: 2, state: "ALERT", ts: now },
    },
  };

  const users = {
    aditya_s:    { userId: "aditya_s",   name: "Aditya Sridhar", deviceId: "esp32_node_01", enrolledAt: now / 1000 - 172800, active: true,  role: "admin",   allowedDevices: ["esp32_node_01","esp32_node_02"], avatarUrl: "https://i.pravatar.cc/80?img=12" },
    rahul_s:     { userId: "rahul_s",    name: "Rahul Sharma",   deviceId: "esp32_node_01", enrolledAt: now / 1000 - 86400,  active: true,  role: "staff",   allowedDevices: ["esp32_node_01"],                 avatarUrl: "https://i.pravatar.cc/80?img=15" },
    priya_p:     { userId: "priya_p",    name: "Priya Patel",    deviceId: "esp32_node_02", enrolledAt: now / 1000 - 72000,  active: true,  role: "staff",   allowedDevices: ["esp32_node_02"],                 avatarUrl: "https://i.pravatar.cc/80?img=47" },
    alex_c:      { userId: "alex_c",     name: "Alex Chen",      deviceId: "esp32_node_01", enrolledAt: now / 1000 - 50000,  active: true,  role: "visitor", allowedDevices: ["esp32_node_01"],                 avatarUrl: "https://i.pravatar.cc/80?img=7"  },
    sarah_j:     { userId: "sarah_j",    name: "Sarah Johnson",  deviceId: "esp32_node_02", enrolledAt: now / 1000 - 36000,  active: true,  role: "staff",   allowedDevices: ["esp32_node_01","esp32_node_02"], avatarUrl: "https://i.pravatar.cc/80?img=44" },
    mohammed_a:  { userId: "mohammed_a", name: "Mohammed Ali",   deviceId: "esp32_node_01", enrolledAt: now / 1000 - 21600,  active: true,  role: "visitor", allowedDevices: ["esp32_node_01"],                 avatarUrl: "https://i.pravatar.cc/80?img=59" },
    emma_w:      { userId: "emma_w",     name: "Emma Wilson",    deviceId: "esp32_node_02", enrolledAt: now / 1000 - 14400,  active: false, role: "visitor", allowedDevices: ["esp32_node_02"],                 avatarUrl: "https://i.pravatar.cc/80?img=38" },
    david_k:     { userId: "david_k",    name: "David Kim",      deviceId: "esp32_node_01", enrolledAt: now / 1000 - 7200,   active: true,  role: "staff",   allowedDevices: ["esp32_node_01"],                 avatarUrl: "https://i.pravatar.cc/80?img=53" },
  };

  const signins = {
    esp32_node_01: [
      { userId: "aditya_s",   userName: "Aditya Sridhar", matchScore: 0.09, success: true,  anomalyScore: 0.04, denyReason: "none",                 ts: now / 1000 - 180,  deviceId: "esp32_node_01" },
      { userId: "rahul_s",    userName: "Rahul Sharma",   matchScore: 0.14, success: true,  anomalyScore: 0.06, denyReason: "none",                 ts: now / 1000 - 420,  deviceId: "esp32_node_01" },
      { userId: "unknown",    userName: "Unknown",        matchScore: 0.71, success: false, anomalyScore: 0.60, denyReason: "no_match",             ts: now / 1000 - 660,  deviceId: "esp32_node_01" },
      { userId: "priya_p",    userName: "Priya Patel",    matchScore: 0.18, success: false, anomalyScore: 0.10, denyReason: "unauthorized_device",  ts: now / 1000 - 800,  deviceId: "esp32_node_01" },
      { userId: "alex_c",     userName: "Alex Chen",      matchScore: 0.21, success: true,  anomalyScore: 0.08, denyReason: "none",                 ts: now / 1000 - 900,  deviceId: "esp32_node_01" },
      { userId: "unknown",    userName: "Unknown",        matchScore: 0.68, success: false, anomalyScore: 0.85, denyReason: "no_match",             ts: now / 1000 - 1100, deviceId: "esp32_node_01" },
      { userId: "david_k",    userName: "David Kim",      matchScore: 0.17, success: true,  anomalyScore: 0.03, denyReason: "none",                 ts: now / 1000 - 3600, deviceId: "esp32_node_01" },
      { userId: "mohammed_a", userName: "Mohammed Ali",   matchScore: 0.11, success: true,  anomalyScore: 0.05, denyReason: "none",                 ts: now / 1000 - 7200, deviceId: "esp32_node_01" },
    ],
    esp32_node_02: [
      { userId: "priya_p",    userName: "Priya Patel",    matchScore: 0.13, success: true,  anomalyScore: 0.04, denyReason: "none",                 ts: now / 1000 - 240,  deviceId: "esp32_node_02" },
      { userId: "sarah_j",    userName: "Sarah Johnson",  matchScore: 0.19, success: true,  anomalyScore: 0.07, denyReason: "none",                 ts: now / 1000 - 720,  deviceId: "esp32_node_02" },
      { userId: "unknown",    userName: "Unknown",        matchScore: 0.74, success: false, anomalyScore: 0.72, denyReason: "no_match",             ts: now / 1000 - 1200, deviceId: "esp32_node_02" },
      { userId: "rahul_s",    userName: "Rahul Sharma",   matchScore: 0.16, success: false, anomalyScore: 0.08, denyReason: "unauthorized_device",  ts: now / 1000 - 2400, deviceId: "esp32_node_02" },
    ],
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
      smokePct:     i === 1 ? 45 + Math.random() * 30 : Math.random() * 15,
      smokeRaw:     Math.round(Math.random() * 4095),
      distanceCm:   5 + Math.random() * 200,
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
