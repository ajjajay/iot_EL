import { formatTsMs } from '../utils/formatters.js';

export default function DeviceCard({ id, device, lastSignin }) {
  const latest  = device.latest   || {};
  const meta    = device.meta     || {};
  const st      = latest.state    || 'UNKNOWN';
  const isAlert = st === 'ALERT';

  const dotClass = `device-dot ${device.online ? (isAlert ? 'alert' : 'online') : 'offline'}`;

  const smoke     = latest.smokePct ?? 0;
  const smokeClass = smoke >= 50 ? 'text-risk-crit' : smoke >= 25 ? 'text-risk-warn' : 'text-risk-safe';

  return (
    <div className={`device-card${isAlert ? ' alert-active' : ''}`} data-device-id={id}>
      <div className="device-card-header">
        <div className="device-info">
          <span className={dotClass}></span>
          <div>
            <div className="device-name">{id}</div>
            <div className="device-location">{meta.location || '—'}</div>
          </div>
        </div>
        <span className={`device-state-badge state-${st}`}>{st}</span>
      </div>

      <div className="bio-row">
        <div className="bio-item">
          <div className="bio-label">Last User</div>
          <div className="bio-value last-user">{lastSignin?.userName || lastSignin?.userId || '—'}</div>
        </div>
        <div className="bio-item">
          <div className="bio-label">Match Score</div>
          <div className="bio-value last-score">
            {lastSignin?.matchScore != null ? lastSignin.matchScore.toFixed(3) : '—'}
          </div>
        </div>
        <div className="bio-item">
          <div className="bio-label">Auth Result</div>
          <div className={`bio-value last-result${lastSignin ? (lastSignin.success ? ' text-success' : ' text-danger') : ''}`}>
            {lastSignin ? (lastSignin.success ? '✓ Granted' : '✗ Denied') : '—'}
          </div>
        </div>
      </div>

      <div className="sensor-grid">
        <div className="sensor-item">
          <div className="sensor-icon">🌡</div>
          <div className="sensor-label">Temp</div>
          <div className="sensor-value temp-value">
            {latest.temperatureC != null ? `${latest.temperatureC.toFixed(1)}°C` : '—'}
          </div>
        </div>
        <div className="sensor-item">
          <div className="sensor-icon">💧</div>
          <div className="sensor-label">Humidity</div>
          <div className="sensor-value hum-value">
            {latest.humidityPct != null ? `${latest.humidityPct.toFixed(1)}%` : '—'}
          </div>
        </div>
        <div className="sensor-item">
          <div className="sensor-icon">💨</div>
          <div className="sensor-label">Smoke</div>
          <div className={`sensor-value smoke-value ${smokeClass}`}>
            {latest.smokePct != null ? `${latest.smokePct.toFixed(1)}%` : '—'}
          </div>
        </div>
        <div className="sensor-item">
          <div className="sensor-icon">📏</div>
          <div className="sensor-label">Distance</div>
          <div className="sensor-value dist-value">
            {latest.distanceCm != null
              ? (latest.distanceCm < 0 ? 'N/A' : `${latest.distanceCm.toFixed(1)} cm`)
              : '—'}
          </div>
        </div>
      </div>

      <div className="device-card-footer">
        <span className="last-seen">
          Last update: {formatTsMs(latest.ts)}
        </span>
      </div>
    </div>
  );
}
