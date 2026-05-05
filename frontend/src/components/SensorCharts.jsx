import { useState, useMemo } from 'react';
import { Line } from 'react-chartjs-2';
import { deviceColour } from '../utils/colors.js';

function buildChartOptions() {
  const s = getComputedStyle(document.documentElement);
  const g = v => s.getPropertyValue(v).trim();
  const base = {
    animation:  { duration: 200 },
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend:  { display: true, labels: { color: g('--text-secondary'), font: { size: 11 } } },
      tooltip: { backgroundColor: g('--bg-base'), titleColor: g('--text-primary'), bodyColor: g('--text-secondary') },
    },
    scales: {
      x: { ticks: { color: g('--text-muted'), maxTicksLimit: 8, font: { size: 10 } }, grid: { color: g('--border') } },
      y: { ticks: { color: g('--text-muted'), font: { size: 10 } },                   grid: { color: g('--border') } },
    },
  };
  const risk = { ...base, scales: { ...base.scales, y: { ...base.scales.y, suggestedMin: 0, suggestedMax: 1 } } };
  return { base, risk };
}

const CHART_DEFS = [
  { field: 'temperatureC', title: 'Temperature (°C)', badgeClass: 'temp-badge',  fmt: v => `${v.toFixed(1)}°C`      },
  { field: 'humidityPct',  title: 'Humidity (%)',     badgeClass: 'hum-badge',   fmt: v => `${v.toFixed(1)}%`       },
  { field: 'lightNorm',    title: 'Light Level',      badgeClass: 'light-badge', fmt: v => `${(v*100).toFixed(0)}%` },
  { field: 'riskScore',    title: 'Env. Risk Score',  badgeClass: 'risk-badge',  fmt: v => `${(v*100).toFixed(0)}%`, isRisk: true },
];

export default function SensorCharts({ readings, devices }) {
  const [selectedDevice, setSelectedDevice] = useState('all');
  const [timeWindow,     setTimeWindow]     = useState('50');

  const { base: baseOptions, risk: riskOptions } = buildChartOptions();

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
          backgroundColor: colour + '18',
          borderWidth:     2,
          pointRadius:     3,
          pointBackgroundColor: colour,
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
              <div className="chart-canvas-wrap">
                <Line data={chartDatasets[i]} options={def.isRisk ? riskOptions : baseOptions} />
              </div>
            </div>
          );
        })}
      </div>
    </section>
  );
}
