export default function SummaryStrip({ devices, signins, users, alerts }) {
  const deviceList = Object.values(devices);
  const online     = deviceList.filter(d => d.online).length;
  const enrolled   = Object.keys(users).length;

  const todayTs = (() => { const d = new Date(); d.setHours(0,0,0,0); return d.getTime() / 1000; })();
  let total = 0, success = 0;
  Object.values(signins).forEach(evts => {
    evts.forEach(e => {
      if ((e.ts || 0) >= todayTs) { total++; if (e.success) success++; }
    });
  });

  const anomalyCount = alerts.filter(a => !!a.alertType && a.alertType !== 'high_env_risk').length;
  const successRate  = total > 0 ? `${Math.round((success / total) * 100)}%` : '—';

  return (
    <section className="summary-strip" id="summaryStrip">
      <div className="summary-card">
        <div className="summary-label">Devices Online</div>
        <div className="summary-value">{deviceList.length ? `${online}/${deviceList.length}` : '—'}</div>
      </div>
      <div className="summary-card">
        <div className="summary-label">Enrolled Users</div>
        <div className="summary-value">{enrolled || '—'}</div>
      </div>
      <div className="summary-card">
        <div className="summary-label">Sign-ins Today</div>
        <div className="summary-value">{total || '—'}</div>
      </div>
      <div className="summary-card">
        <div className="summary-label">Success Rate</div>
        <div className="summary-value">{successRate}</div>
      </div>
      <div className="summary-card">
        <div className="summary-label">Anomaly Alerts</div>
        <div className="summary-value alert-count">{anomalyCount || '—'}</div>
      </div>
      <div className="summary-card">
        <div className="summary-label">Last Update</div>
        <div className="summary-value summary-value-sm">{new Date().toLocaleTimeString()}</div>
      </div>
    </section>
  );
}
