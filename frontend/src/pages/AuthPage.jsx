import WebcamAuth from '../components/WebcamAuth.jsx';
import SignInLog   from '../components/SignInLog.jsx';
import QrPanel     from '../components/QrPanel.jsx';

export default function AuthPage({ devices, users, signins, onResult }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Biometric Authentication</h2>
        <p className="page-sub">Iris recognition via laptop webcam</p>
      </div>
      <WebcamAuth devices={devices} users={users} onResult={onResult} />
      <QrPanel devices={devices} />
      <SignInLog signins={signins} devices={devices} />
    </>
  );
}
