import { ALERT_LABELS } from '../utils/formatters.js';

export default function SecurityAlerts({ alerts, onClear }) {
  return (
    <section className="section">
      <div className="alerts-header">
        <h2 className="section-title">Security Alerts</h2>
        <button className="btn btn-danger-outline btn-sm" onClick={onClear}>Clear All</button>
      </div>
      <div className="alert-timeline" id="bioAlertTimeline">
        {alerts.length === 0 ? (
          <div className="no-alerts">No security alerts — all sign-ins normal</div>
        ) : alerts.map((alert, i) => {
          const isBio = !!alert.alertType && alert.alertType !== 'high_env_risk';
          const ts    = alert.ts ? new Date(alert.ts * 1000).toLocaleTimeString() : '—';
          return (
            <div key={i} className={`alert-item ${isBio ? 'bio-alert-item' : 'warning-item'}`}>
              <div className="alert-device">{alert.deviceId}</div>
              <div className="alert-detail">
                {isBio ? (
                  <>
                    <span className="alert-type-badge">
                      {ALERT_LABELS[alert.alertType] || alert.alertType}
                    </span>
                    User: <strong>{alert.userId || 'unknown'}</strong>
                    {' '}| Anomaly: {((alert.anomalyScore || 0) * 100).toFixed(0)}%
                    {alert.detail ? ` | ${alert.detail}` : ''}
                  </>
                ) : (
                  <>
                    Env Risk: <strong>{((alert.riskScore || 0) * 100).toFixed(0)}%</strong>
                    {' '}| Temp: {alert.temperatureC?.toFixed(1) ?? '—'}°C
                    {' '}| Label: {['Normal','Warning','Critical'][alert.mlLabel] ?? '—'}
                  </>
                )}
              </div>
              <div className="alert-time">{ts}</div>
            </div>
          );
        })}
      </div>
    </section>
  );
}
