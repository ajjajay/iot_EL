import { useState, useEffect, useCallback } from 'react';
import { BrowserRouter, Routes, Route, Navigate, useLocation } from 'react-router-dom';
import { signInWithEmailAndPassword } from 'firebase/auth';
import { ref, set, push, remove } from 'firebase/database';
import { db, auth, DASH_EMAIL, DASH_PASSWORD } from './firebase.js';
import { useDevices }       from './hooks/useDevices.js';
import { useReadings }      from './hooks/useReadings.js';
import { useSignins }       from './hooks/useSignins.js';
import { useUsers }         from './hooks/useUsers.js';
import { useAlerts }        from './hooks/useAlerts.js';
import { useBackendStream } from './hooks/useBackendStream.js';
import { makeDemoState, simulateDemoTick } from './utils/demo.js';
import { useToast }    from './components/ToastContainer.jsx';

import Sidebar       from './components/Sidebar.jsx';
import OverviewPage  from './pages/OverviewPage.jsx';
import SensorsPage   from './pages/SensorsPage.jsx';
import AuthPage      from './pages/AuthPage.jsx';
import UsersPage     from './pages/UsersPage.jsx';
import AlertsPage    from './pages/AlertsPage.jsx';
import VoicePage     from './pages/VoicePage.jsx';
import HealthPage    from './pages/HealthPage.jsx';

const PAGE_TITLES = {
  '/':        'Overview',
  '/devices': 'Devices & Sensors',
  '/auth':    'Biometric Auth',
  '/users':   'Enrolled Users',
  '/alerts':  'Security Alerts',
  '/voice':   'Voice Assistant',
  '/health':  'Device Health',
};

function Topbar({ anomalyCount, devicesOnline, deviceTotal, theme, onTheme, onRefresh }) {
  const { pathname } = useLocation();
  const title = PAGE_TITLES[pathname] ?? 'Dashboard';
  return (
    <header className="topbar">
      <div className="topbar-left">
        <h1 className="page-title">{title}</h1>
        <span className={`status-pill ${anomalyCount > 0 ? 'status-pill-err' : 'status-pill-ok'}`}>
          {anomalyCount > 0 ? `⚠ ${anomalyCount} Alert${anomalyCount > 1 ? 's' : ''}` : '● Live'}
        </span>
      </div>
      <div className="topbar-right">
        <button className="icon-btn" title="Toggle theme" onClick={onTheme}>
          {theme === 'dark' ? '☀' : '☾'}
        </button>
        <button className="icon-btn" title="Refresh" onClick={onRefresh}>↻</button>
        <div className="topbar-user">
          <div className="topbar-avatar">A</div>
          <div className="topbar-user-info">
            <span className="topbar-user-name">Admin</span>
            <span className="topbar-user-role">{devicesOnline}/{deviceTotal} Online</span>
          </div>
        </div>
      </div>
    </header>
  );
}

function AppInner() {
  const showToast = useToast();

  const [theme,     setTheme]     = useState('light');
  const [isDemo,    setIsDemo]    = useState(false);
  const [liveDb,    setLiveDb]    = useState(null);
  const [demoState, setDemoState] = useState(null);
  const [tick,      setTick]      = useState(0);

  useEffect(() => {
    signInWithEmailAndPassword(auth, DASH_EMAIL, DASH_PASSWORD)
      .then(() => { setLiveDb(db); showToast('Connected to Firebase', 'ok'); })
      .catch(err => {
        console.warn('[FB]', err.message);
        showToast('Firebase unavailable — demo mode', 'info');
        setIsDemo(true);
        setDemoState(makeDemoState());
      });
  }, []);

  useEffect(() => {
    if (!isDemo) return;
    const t = setInterval(() => {
      setDemoState(prev => {
        const { devices, readings, signins } = simulateDemoTick(prev);
        return { ...prev, devices, readings, signins };
      });
    }, 3000);
    return () => clearInterval(t);
  }, [isDemo]);

  useEffect(() => {
    const t = setInterval(() => setTick(n => n + 1), 5000);
    return () => clearInterval(t);
  }, []);

  useEffect(() => { document.body.dataset.theme = theme; }, [theme]);

  const liveDevices  = useDevices(liveDb);
  const liveReadings = useReadings(liveDb);
  const liveSignins  = useSignins(liveDb);
  const liveUsers    = useUsers(liveDb);
  const [liveAlerts, clearLiveAlerts] = useAlerts(liveDb);

  // Backend SSE stream — runs in parallel; fills in data when Firebase SDK
  // has no entries yet (e.g. auth delay) or as a cross-check.
  const { devices: streamDevices, readings: streamReadings } = useBackendStream();

  // Merge: Firebase SDK (real-time WebSocket) wins per-device when present;
  // backend SSE fills in any device the SDK hasn't seen yet.
  const mergedLiveDevices = { ...streamDevices, ...liveDevices };
  const mergedLiveReadings = (() => {
    const out = { ...streamReadings };
    Object.entries(liveReadings).forEach(([id, arr]) => { out[id] = arr; });
    return out;
  })();

  const devices  = isDemo ? (demoState?.devices  ?? {}) : mergedLiveDevices;
  const readings = isDemo ? (demoState?.readings ?? {}) : mergedLiveReadings;
  const signins  = isDemo ? (demoState?.signins  ?? {}) : liveSignins;
  const users    = isDemo ? (demoState?.users    ?? {}) : liveUsers;
  const alerts   = isDemo ? (demoState?.alerts   ?? []) : liveAlerts;

  const deviceList    = Object.values(devices);
  const devicesOnline = deviceList.filter(d => d.online).length;
  const anomalyCount  = alerts.filter(a => !!a.alertType && a.alertType !== 'high_env_risk').length;

  const handleRemoveUser = useCallback((userId) => {
    if (!window.confirm(`Remove enrolled user "${userId}"?`)) return;
    if (isDemo) {
      setDemoState(prev => { const u = { ...prev.users }; delete u[userId]; return { ...prev, users: u }; });
      showToast(`[Demo] User ${userId} removed`, 'info');
      return;
    }
    remove(ref(db, `/users/${userId}`))
      .then(() => showToast(`User ${userId} removed`, 'ok'))
      .catch(e  => showToast(`Remove failed: ${e.message}`, 'alert'));
  }, [isDemo, showToast]);

  const handleSendEnroll = useCallback((deviceId, userId, name) => {
    if (isDemo) {
      setDemoState(prev => ({
        ...prev,
        users: { ...prev.users, [userId]: { userId, name, deviceId, enrolledAt: Date.now() / 1000, active: true } },
      }));
      showToast(`[Demo] Enrollment command sent for "${name}"`, 'ok');
      return Promise.resolve();
    }
    return set(ref(db, `/devices/${deviceId}/commands/enroll`), { userId, name, pending: true })
      .then(() => showToast(`Enrollment command sent to ${deviceId}`, 'ok'))
      .catch(e  => { showToast(`Command failed: ${e.message}`, 'alert'); throw e; });
  }, [isDemo, showToast]);

  const handleWebcamAuth = useCallback(async (deviceId, result) => {
    if (isDemo) {
      showToast(
        result.matched ? `[Demo] Access granted: ${result.userName}` : '[Demo] Access denied',
        result.matched ? 'ok' : 'alert',
      );
      return;
    }

    if (result.livenessFailed) {
      // Log the spoof attempt to Firebase so it appears in the signin log and alerts tab
      const ts = Date.now();
      await push(ref(db, `/signins/${deviceId}`), {
        userId: 'unknown', userName: 'Unknown',
        deviceId, matchScore: 1, success: false,
        anomalyScore: 1.0, source: 'liveness_failed', ts,
      });
      await push(ref(db, `/alerts/${deviceId}`), {
        deviceId, alertType: 'spoof_attempt',
        userId: 'unknown', anomalyScore: 1.0, acknowledged: false, ts,
      });
      showToast('Spoof detected — access denied', 'alert');
      return;
    }

    if (result.lambdaHandled) {
      // Lambda already wrote the signin record and set relayOverride in Firebase.
      // Just surface the result to the operator via toast.
      if (result.granted) {
        showToast(`Access granted — ${result.userName}`, 'ok');
      } else {
        const reason = result.reason === 'anomaly'
          ? `Anomalous pattern detected for ${result.userName}`
          : 'No face match';
        showToast(`Access denied — ${reason}`, 'alert');
      }
      return;
    }

    // Local matching fallback (no API URL configured): write to Firebase ourselves.
    await push(ref(db, `/signins/${deviceId}`), {
      userId: result.userId ?? 'unknown', userName: result.userName ?? 'Unknown',
      deviceId, matchScore: result.score, success: result.matched, anomalyScore: 0, ts: Date.now(),
    });
    if (result.matched) {
      await set(ref(db, `/devices/${deviceId}/commands/relayOverride`), 'ON');
      showToast(`Access granted — ${result.userName}`, 'ok');
    } else {
      showToast('Access denied', 'alert');
    }
  }, [isDemo, showToast]);

  const handleWebcamEnroll = useCallback(async (deviceId, userId, name, template) => {
    if (isDemo) {
      setDemoState(prev => ({
        ...prev,
        users: { ...prev.users, [userId]: { userId, name, deviceId, enrolledAt: Date.now(), active: true, template } },
      }));
      showToast(`[Demo] ${name} enrolled via webcam`, 'ok');
      return;
    }

    if (template) {
      // Local mode: Lambda not in use — save iris template directly to Firebase.
      await set(ref(db, `/users/${userId}`), { userId, name, deviceId, enrolledAt: Date.now(), active: true, template });
    }
    // Rekognition mode: Lambda already wrote the user record to Firebase (/users/{userId}).
    showToast(`${name} enrolled`, 'ok');
  }, [isDemo, showToast]);

  const handleClearAlerts = useCallback(() => {
    if (isDemo) setDemoState(prev => ({ ...prev, alerts: [] }));
    else        clearLiveAlerts();
  }, [isDemo, clearLiveAlerts]);

  const handleRefresh = useCallback(() => {
    setTick(n => n + 1);
    showToast('Dashboard refreshed', 'info');
  }, [showToast]);

  return (
    <div className="app-layout">
      <Sidebar anomalyCount={anomalyCount} />

      <div className="main-area">
        <Topbar
          anomalyCount={anomalyCount}
          devicesOnline={devicesOnline}
          deviceTotal={deviceList.length}
          theme={theme}
          onTheme={() => setTheme(t => t === 'dark' ? 'light' : 'dark')}
          onRefresh={handleRefresh}
        />

        <main className="content">
          <Routes>
            <Route path="/" element={
              <OverviewPage devices={devices} signins={signins} users={users} alerts={alerts} tick={tick} />
            } />
            <Route path="/devices" element={
              <SensorsPage readings={readings} devices={devices} />
            } />
            <Route path="/auth" element={
              <AuthPage devices={devices} users={users} signins={signins} onResult={handleWebcamAuth} />
            } />
            <Route path="/users" element={
              <UsersPage
                devices={devices} users={users}
                onRemove={handleRemoveUser}
                onSendEnroll={handleSendEnroll}
                onWebcamEnroll={handleWebcamEnroll}
              />
            } />
            <Route path="/alerts" element={
              <AlertsPage alerts={alerts} signins={signins} devices={devices} onClear={handleClearAlerts} />
            } />
            <Route path="/voice" element={
              <VoicePage devices={devices} users={users} alerts={alerts} />
            } />
            <Route path="/health" element={
              <HealthPage devices={devices} />
            } />
            <Route path="*" element={<Navigate to="/" replace />} />
          </Routes>
        </main>
      </div>
    </div>
  );
}

export default function App() {
  return (
    <BrowserRouter>
      <AppInner />
    </BrowserRouter>
  );
}
