import { formatUptime, formatTs } from '../utils/formatters.js';

export default function HealthTable({ devices }) {
  const entries = Object.entries(devices);

  return (
    <section className="section">
      <h2 className="section-title">Device Health</h2>
      <div className="table-wrapper">
        <table className="health-table" id="healthTable">
          <thead>
            <tr>
              <th>Device</th><th>Location</th><th>State</th>
              <th>Uptime</th><th>Free Heap</th><th>Last Seen</th><th>Firmware</th>
            </tr>
          </thead>
          <tbody id="healthTableBody">
            {entries.length === 0 ? (
              <tr><td colSpan="7" className="table-empty">Connecting to Firebase...</td></tr>
            ) : entries.map(([id, device]) => {
              const hb   = device.heartbeat || {};
              const meta = device.meta      || {};
              return (
                <tr key={id}>
                  <td><code>{id}</code></td>
                  <td>{meta.location ?? '—'}</td>
                  <td>
                    <span className={`device-state-badge state-${hb.state || 'UNKNOWN'}`}>
                      {hb.state || '—'}
                    </span>
                  </td>
                  <td>{formatUptime(hb.uptime)}</td>
                  <td>{hb.heapFree ? `${(hb.heapFree / 1024).toFixed(0)} KB` : '—'}</td>
                  <td>{formatTs(hb.ts)}</td>
                  <td>{meta.firmware ?? '—'}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </section>
  );
}
