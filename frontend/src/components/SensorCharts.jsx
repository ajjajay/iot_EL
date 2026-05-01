import { useState, useMemo } from 'react';
import { Line } from 'react-chartjs-2';
import { deviceColour } from '../utils/colors.js';

const BASE_OPTIONS = {
  animation:  { duration: 200 },
  responsive: true,
  maintainAspectRatio: false,
  interaction: { mode: 'index', intersect: false },
  plugins: {
    legend:  { display: true, labels: { color: '#8b90a7', font: { size: 11 } } },
    tooltip: { backgroundColor: '#222636', titleColor: '#f0f2ff', bodyColor: '#8b90a7' },
  },
  scales: {
    x: { ticks: { color: '#555b7a', maxTicksLimit: 8, font: { size: 10 } }, grid: { color: 'rgba(255,255,255,0.04)' } },
    y: { ticks: { color: '#555b7a', font: { size: 10 } },                   grid: { color: 'rgba(255,255,255,0.04)' } },
  },
};

const RISK_OPTIONS = {
  ...BASE_OPTIONS,
  scales: { ...BASE_OPTIONS.scales, y: { ...BASE_OPTIONS.scales.y, suggestedMin: 0, suggestedMax: 1 } },
};

const CHART_DEFS = [
  { field: 'temperatureC', title: 'Temperature (°C)', badgeClass: 'temp-badge',  fmt: v => `${v.toFixed(1)}°C`,          options: BASE_OPTIONS },
  { field: 'humidityPct',  title: 'Humidity (%)',     badgeClass: 'hum-badge',   fmt: v => `${v.toFixed(1)}%`,           options: BASE_OPTIONS },
  { field: 'lightNorm',    title: 'Light Level',      badgeClass: 'light-badge', fmt: v => `${(v*100).toFixed(0)}%`,     options: BASE_OPTIONS },
  { field: 'riskScore',    title: 'Env. Risk Score',  badgeClass: 'risk-badge',  fmt: v => `${(v*100).toFixed(0)}%`,     options: RISK_OPTIONS },
];

export default function SensorCharts({ readings, devices }) {
  const [selectedDevice, setSelectedDevice] = useState('all');
  const [timeWindow,     setTimeWindow]     = useState('50');

  const deviceIds    = Object.keys(devices);
  const maxPts       = parseInt(timeWindow);
  const activeDevs   = selectedDevice === 'all' ? deviceIds : [selectedDevice];
  const latestForBadge = selectedDevice === 'all'
    ? Object.values(devices)[0]?.latest
    : devices[selectedDevice]?.latest;

  const chartDatasets = useMemo(() => {
    return CHART_DEFS.map(def => {
      const datasets = [];
      let maxLabels  = [];
      activeDevs.forEach(id => {
        const pts = (readings[id] || []).slice(-maxPts);
        if (!pts.length) return;
        const labels = pts.map(r =>
          r.ts ? new Date(r.ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) : '?'
        );
        if (labels.length > maxLabels.length) maxLabels = labels;
        const colour = deviceColour(id);
        datasets.push({
          label:           id,
          data:            pts.map(r => r[def.field] ?? null),
          borderColor:     colour,
          backgroundColor: colour + '22',
          borderWidth:     2,
          pointRadius:     0,
          tension:         0.4,
          fill:            true,
        });
      });
      return { labels: maxLabels, datasets };
    });
  }, [readings, activeDevs, maxPts]);

  return (
    <section className="section">
      <div className="charts-header">
        <h2 className="section-title">Ambient Sensor Trends</h2>
        <div className="chart-controls">
          <label htmlFor="deviceSelect" className="sr-only">Device</label>
          <select id="deviceSelect" className="select" value={selectedDevice} onChange={e => setSelectedDevice(e.target.value)}>
            <option value="all">All Devices</option>
            {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
          </select>
          <select id="timeWindow" className="select" value={timeWindow} onChange={e => setTimeWindow(e.target.value)}>
            <option value="50">Last 50 readings</option>
            <option value="100">Last 100 readings</option>
            <option value="200">Last 200 readings</option>
          </select>
        </div>
      </div>
      <div className="chart-grid">
        {CHART_DEFS.map((def, i) => {
          const nowVal = latestForBadge?.[def.field];
          return (
            <div key={def.field} className="chart-card">
              <div className="chart-card-header">
                <span className="chart-title">{def.title}</span>
                <span className={`chart-badge ${def.badgeClass}`}>
                  {nowVal != null ? def.fmt(nowVal) : '—'}
                </span>
              </div>
              <div style={{ height: 120 }}>
                <Line data={chartDatasets[i]} options={def.options} />
              </div>
            </div>
          );
        })}
      </div>
    </section>
  );
}
