import { useState, useEffect, useCallback } from 'react';

const LABEL_META = {
  normal:   { cls: 'risk-normal',   icon: '✓', text: 'Normal'   },
  warning:  { cls: 'risk-warning',  icon: '⚠', text: 'Warning'  },
  critical: { cls: 'risk-critical', icon: '✕', text: 'Critical' },
};

async function fetchPrediction(latest) {
  const temp  = latest?.temperatureC;
  const hum   = latest?.humidityPct;
  const smoke = latest?.smokePct;

  if (temp == null || hum == null) return null;

  const body = {
    temperature_c: Math.min(Math.max(parseFloat(temp),  -10), 60),
    humidity_pct:  Math.min(Math.max(parseFloat(hum),     0), 100),
    light_norm:    Math.min(Math.max(parseFloat(smoke ?? 0) / 100, 0), 1),
  };

  const res = await fetch('/api/predict', {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(body),
  });
  if (!res.ok) throw new Error(`Predict ${res.status}`);
  return res.json();
}

function DeviceRiskCard({ deviceId, device }) {
  const [result,    setResult]    = useState(null);
  const [loading,   setLoading]   = useState(false);
  const [error,     setError]     = useState('');
  const [updatedAt, setUpdatedAt] = useState(null);

  const run = useCallback(async () => {
    setLoading(true);
    setError('');
    try {
      const data = await fetchPrediction(device?.latest);
      setResult(data);
      setUpdatedAt(new Date().toLocaleTimeString());
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, [device?.latest]);

  // Run on mount and every 30 s
  useEffect(() => {
    run();
    const t = setInterval(run, 30_000);
    return () => clearInterval(t);
  }, [run]);

  const meta    = result ? (LABEL_META[result.label] ?? LABEL_META.normal) : null;
  const score   = result?.risk_score ?? 0;
  const barPct  = Math.round(score * 100);
  const latest  = device?.latest ?? {};
  const online  = device?.online ?? false;

  return (
    <div className={`risk-card${result?.label === 'critical' ? ' risk-card--critical' : result?.label === 'warning' ? ' risk-card--warning' : ''}`}>
      <div className="risk-card-header">
        <div className="risk-device-info">
          <span className={`device-dot ${online ? 'online' : 'offline'}`} />
          <div>
            <div className="risk-device-id">{deviceId}</div>
            <div className="risk-device-loc">{device?.meta?.location ?? '—'}</div>
          </div>
        </div>
        {meta && (
          <span className={`risk-label-badge ${meta.cls}`}>
            {meta.icon} {meta.text}
          </span>
        )}
      </div>

      {/* Inputs fed to the model */}
      <div className="risk-inputs">
        <div className="risk-input-item">
          <span className="risk-input-lbl">Temp</span>
          <span className="risk-input-val">{latest.temperatureC != null ? `${parseFloat(latest.temperatureC).toFixed(1)}°C` : '—'}</span>
        </div>
        <div className="risk-input-item">
          <span className="risk-input-lbl">Humidity</span>
          <span className="risk-input-val">{latest.humidityPct != null ? `${parseFloat(latest.humidityPct).toFixed(1)}%` : '—'}</span>
        </div>
        <div className="risk-input-item">
          <span className="risk-input-lbl">Smoke</span>
          <span className="risk-input-val">{latest.smokePct != null ? `${parseFloat(latest.smokePct).toFixed(1)}%` : '—'}</span>
        </div>
      </div>

      {/* Score bar */}
      {result && (
        <div className="risk-score-wrap">
          <div className="risk-score-row">
            <span className="risk-score-lbl">Risk Score</span>
            <span className="risk-score-val">{barPct}%</span>
          </div>
          <div className="risk-bar-bg">
            <div
              className={`risk-bar-fill ${meta.cls}`}
              style={{ width: `${barPct}%` }}
            />
          </div>
        </div>
      )}

      {error && <p className="risk-error">{error}</p>}

      <div className="risk-footer">
        <span className="risk-backend">
          {result ? `Model: ${result.backend}` : '—'}
        </span>
        <div className="risk-footer-right">
          {updatedAt && <span className="risk-updated">{updatedAt}</span>}
          <button
            className="risk-refresh-btn"
            onClick={run}
            disabled={loading}
            title="Refresh prediction"
          >
            {loading ? '⏳' : '↻'}
          </button>
        </div>
      </div>
    </div>
  );
}

export default function RiskAssessment({ devices }) {
  const entries = Object.entries(devices ?? {});
  if (!entries.length) return null;

  return (
    <div className="risk-section">
      <div className="risk-section-header">
        <h3 className="risk-section-title">ML Risk Assessment</h3>
        <span className="risk-section-sub">
          Backend model scores each device every 30 s · inputs: temp, humidity, smoke
        </span>
      </div>
      <div className="risk-grid">
        {entries.map(([id, dev]) => (
          <DeviceRiskCard key={id} deviceId={id} device={dev} />
        ))}
      </div>
    </div>
  );
}
