import SensorCharts   from '../components/SensorCharts.jsx';
import RiskAssessment from '../components/RiskAssessment.jsx';

export default function SensorsPage({ readings, devices }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Live Sensor Data</h2>
        <p className="page-sub">Temperature · Humidity · Smoke · Distance — all devices</p>
      </div>
      <RiskAssessment devices={devices} />
      <SensorCharts readings={readings} devices={devices} />
    </>
  );
}
