import SecurityAlerts from '../components/SecurityAlerts.jsx';
import SignInLog      from '../components/SignInLog.jsx';

export default function AlertsPage({ alerts, signins, devices, onClear }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Security Alerts</h2>
        <p className="page-sub">Anomaly detections and full authentication log</p>
      </div>
      <SecurityAlerts alerts={alerts} onClear={onClear} />
      <SignInLog signins={signins} devices={devices} />
    </>
  );
}
