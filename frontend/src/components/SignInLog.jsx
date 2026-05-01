import { useState } from 'react';

const MAX_ROWS = 50;

export default function SignInLog({ signins, devices }) {
  const [filterDevice, setFilterDevice] = useState('all');
  const deviceIds = Object.keys(devices);

  let rows = [];
  Object.entries(signins).forEach(([deviceId, evts]) => {
    if (filterDevice !== 'all' && deviceId !== filterDevice) return;
    evts.forEach(ev => rows.push({ ...ev, deviceId }));
  });
  rows.sort((a, b) => (b.ts || 0) - (a.ts || 0));
  rows = rows.slice(0, MAX_ROWS);

  return (
    <section className="section">
      <div className="alerts-header">
        <h2 className="section-title">Sign-in Log</h2>
        <div className="chart-controls">
          <select
            id="signinDeviceSelect"
            className="select"
            value={filterDevice}
            onChange={e => setFilterDevice(e.target.value)}
          >
            <option value="all">All Devices</option>
            {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
          </select>
        </div>
      </div>
      <div className="table-wrapper">
        <table className="health-table" id="signinTable">
          <thead>
            <tr>
              <th>Time</th><th>Device</th><th>User</th>
              <th>Match Score</th><th>Result</th><th>Anomaly</th>
            </tr>
          </thead>
          <tbody id="signinTableBody">
            {rows.length === 0 ? (
              <tr><td colSpan="6" className="table-empty">No sign-ins recorded yet</td></tr>
            ) : rows.map((ev, i) => {
              const ts        = ev.ts ? new Date(ev.ts * 1000).toLocaleTimeString() : '—';
              const anomClass = ev.anomalyScore >= 0.6 ? 'anomaly-high' : ev.anomalyScore >= 0.3 ? 'anomaly-med' : '';
              return (
                <tr key={i}>
                  <td>{ts}</td>
                  <td><code>{ev.deviceId}</code></td>
                  <td>{ev.userName || ev.userId || '—'}</td>
                  <td><span className="score-badge">{ev.matchScore?.toFixed(3) ?? '—'}</span></td>
                  <td>
                    <span className={`result-badge ${ev.success ? 'result-ok' : 'result-fail'}`}>
                      {ev.success ? '✓ Granted' : '✗ Denied'}
                    </span>
                  </td>
                  <td>
                    <span className={`anomaly-badge ${anomClass}`}>
                      {ev.anomalyScore != null ? `${(ev.anomalyScore * 100).toFixed(0)}%` : '—'}
                    </span>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </section>
  );
}
