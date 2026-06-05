import HealthTable from '../components/HealthTable.jsx';

export default function HealthPage({ devices }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Device Health</h2>
        <p className="page-sub">Heartbeats, free heap, uptime and firmware version</p>
      </div>
      <HealthTable devices={devices} />
    </>
  );
}
