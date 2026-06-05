import { useState, useEffect, useCallback } from 'react';
import { signInWithEmailAndPassword } from 'firebase/auth';
import { ref, set, push, remove } from 'firebase/database';
import { db, auth, DASH_EMAIL, DASH_PASSWORD } from './firebase.js';
import { useDevices }  from './hooks/useDevices.js';
import { useReadings } from './hooks/useReadings.js';
import { useSignins }  from './hooks/useSignins.js';
import { useUsers }    from './hooks/useUsers.js';
import { useAlerts }   from './hooks/useAlerts.js';
import { makeDemoState, simulateDemoTick } from './utils/demo.js';
import { useToast }    from './components/ToastContainer.jsx';

import SummaryStrip    from './components/SummaryStrip.jsx';
import DeviceGrid      from './components/DeviceGrid.jsx';
import SignInLog       from './components/SignInLog.jsx';
import SecurityAlerts  from './components/SecurityAlerts.jsx';
import EnrolledUsers   from './components/EnrolledUsers.jsx';
import EnrollPanel     from './components/EnrollPanel.jsx';
import WebcamAuth      from './components/WebcamAuth.jsx';
import SensorCharts    from './components/SensorCharts.jsx';
import HealthTable     from './components/HealthTable.jsx';

export default function App() {
  const showToast = useToast();

  const [theme,     setTheme]     = useState('light');
  const [isDemo,    setIsDemo]    = useState(false);
  const [liveDb,    setLiveDb]    = useState(null);
  const [demoState, setDemoState] = useState(null);
  const [tick,      setTick]      = useState(0);

  // Firebase auth on mount
  useEffect(() => {
    signInWithEmailAndPassword(auth, DASH_EMAIL, DASH_PASSWORD)
      .then(() => {
        setLiveDb(db);
        showToast('Connected to Firebase', 'ok');
      })
      .catch(err => {
        console.warn('[FB] Firebase init failed:', err.message);
        showToast('Firebase unavailable — running in demo mode', 'info');
        setIsDemo(true);
        setDemoState(makeDemoState());
      });
  }, []);

  // Demo simulation
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

  // Tick every 5 s to refresh the "Last Update" clock in SummaryStrip
  useEffect(() => {
    const t = setInterval(() => setTick(n => n + 1), 5000);
    return () => clearInterval(t);
  }, []);

  // Apply theme to body
  useEffect(() => {
    document.body.dataset.theme = theme;
  }, [theme]);

  // Live Firebase hooks (return empty state when liveDb is null)
  const liveDevices = useDevices(liveDb);
  const liveReadings = useReadings(liveDb);
  const liveSignins  = useSignins(liveDb);
  const liveUsers    = useUsers(liveDb);
  const [liveAlerts, clearLiveAlerts] = useAlerts(liveDb);

  // Resolve final data (demo overrides live when in demo mode)
  const devices = isDemo ? (demoState?.devices  ?? {}) : liveDevices;
  const readings = isDemo ? (demoState?.readings ?? {}) : liveReadings;
  const signins  = isDemo ? (demoState?.signins  ?? {}) : liveSignins;
  const users    = isDemo ? (demoState?.users    ?? {}) : liveUsers;
  const alerts   = isDemo ? (demoState?.alerts   ?? []) : liveAlerts;

  const deviceList    = Object.values(devices);
  const devicesOnline = deviceList.filter(d => d.online).length;
  const anomalyCount  = alerts.filter(a => !!a.alertType && a.alertType !== 'high_env_risk').length;

  // ── Commands ───────────────────────────────────────────────────────────────

  const handleRemoveUser = useCallback((userId) => {
    if (!window.confirm(`Remove enrolled user "${userId}"?`)) return;
    if (isDemo) {
      setDemoState(prev => {
        const u = { ...prev.users };
        delete u[userId];
        return { ...prev, users: u };
      });
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
        users: {
          ...prev.users,
          [userId]: { userId, name, deviceId, enrolledAt: Date.now() / 1000, active: true },
        },
      }));
      showToast(`[Demo] Enrollment command sent for "${name}"`, 'ok');
      return Promise.resolve();
    }
    return set(ref(db, `/devices/${deviceId}/commands/enroll`), { userId, name, pending: true })
      .then(() => showToast(`Enrollment command sent to ${deviceId}`, 'ok'))
      .catch(e  => { showToast(`Command failed: ${e.message}`, 'alert'); throw e; });
  }, [isDemo, showToast]);

  const handleRelayCommand = useCallback((deviceId, value) => {
    if (isDemo) {
      setDemoState(prev => ({
        ...prev,
        devices: {
          ...prev.devices,
          [deviceId]: { ...prev.devices[deviceId], commands: { relayOverride: value } },
        },
      }));
      showToast(`[Demo] Relay ${deviceId} → ${value}`, 'info');
      return;
    }
    set(ref(db, `/devices/${deviceId}/commands/relayOverride`), value)
      .then(() => showToast(`Relay command sent to ${deviceId}: ${value}`, 'ok'))
      .catch(e  => showToast(`Command failed: ${e.message}`, 'alert'));
  }, [isDemo, showToast]);

  // Webcam authentication: log sign-in + unlock relay on match
  const handleWebcamAuth = useCallback(async (deviceId, result) => {
    if (isDemo) {
      showToast(
        result.matched ? `[Demo] Access granted: ${result.userName}` : '[Demo] Access denied',
        result.matched ? 'ok' : 'alert'
      );
      return;
    }
    await push(ref(db, `/signins/${deviceId}`), {
      userId:      result.userId   ?? 'unknown',
      userName:    result.userName ?? 'Unknown',
      deviceId,
      matchScore:  result.score,
      success:     result.matched,
      anomalyScore: 0,
      ts:          Date.now(),
    });
    if (result.matched) {
      await set(ref(db, `/devices/${deviceId}/commands/relayOverride`), 'ON');
      showToast(`Access granted — ${result.userName}`, 'ok');
    } else {
      showToast('Access denied', 'alert');
    }
  }, [isDemo, showToast]);

  // Webcam enrollment: store template + user record directly in Firebase
  const handleWebcamEnroll = useCallback(async (deviceId, userId, name, template) => {
    if (isDemo) {
      setDemoState(prev => ({
        ...prev,
        users: {
          ...prev.users,
          [userId]: { userId, name, deviceId, enrolledAt: Date.now(), active: true, template },
        },
      }));
      showToast(`[Demo] ${name} enrolled via webcam`, 'ok');
      return;
    }
    await set(ref(db, `/users/${userId}`), {
      userId, name, deviceId,
      enrolledAt: Date.now(),
      active: true,
      template,
    });
    showToast(`${name} enrolled via webcam`, 'ok');
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

      <div className="main-area">
        {/* Top bar */}
        <header className="topbar">
          <div className="topbar-left">
            <h1 className="page-title">Dashboard</h1>
            <span className={`status-pill ${anomalyCount > 0 ? 'status-pill-err' : 'status-pill-ok'}`}>
              {anomalyCount > 0 ? `⚠ ${anomalyCount} Alert${anomalyCount > 1 ? 's' : ''}` : '● Live'}
            </span>
          </div>
          <div className="topbar-right">
            <button className="icon-btn" title="Toggle theme" onClick={() => setTheme(t => t === 'dark' ? 'light' : 'dark')}>
              {theme === 'dark' ? '☀' : '☾'}
            </button>
            <button className="icon-btn" title="Refresh" onClick={handleRefresh}>↻</button>
            <div className="topbar-user">
              <div className="topbar-avatar">A</div>
              <div className="topbar-user-info">
                <span className="topbar-user-name">Admin</span>
                <span className="topbar-user-role">{devicesOnline}/{deviceList.length} Online</span>
              </div>
            </div>
          </div>
        </header>

        {/* Main scrollable content */}
        <main className="content">
          <SummaryStrip
            key={tick}
            devices={devices}
            signins={signins}
            users={users}
            alerts={alerts}
          />

          <DeviceGrid devices={devices} signins={signins} />

          <SensorCharts readings={readings} devices={devices} />

          <div className="two-col">
            <SignInLog signins={signins} devices={devices} />
            <SecurityAlerts alerts={alerts} onClear={handleClearAlerts} />
          </div>

          <section className="section">
            <h2 className="section-title">Biometric Authentication</h2>
            <WebcamAuth
              devices={devices}
              users={users}
              onResult={handleWebcamAuth}
            />
          </section>

          <section className="section">
            <h2 className="section-title">Enrolled Users</h2>
            <EnrolledUsers users={users} onRemove={handleRemoveUser} />
            <EnrollPanel
              devices={devices}
              onSendEnroll={handleSendEnroll}
              onWebcamEnroll={handleWebcamEnroll}
            />
          </section>

          <HealthTable devices={devices} />
        </main>
      </div>
    </div>
  );
}
