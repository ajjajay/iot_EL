import { useState } from 'react';
import { useToast } from './ToastContainer.jsx';

export default function EnrollPanel({ devices, onSendEnroll }) {
  const showToast = useToast();
  const [deviceId, setDeviceId] = useState('');
  const [userId,   setUserId]   = useState('');
  const [name,     setName]     = useState('');
  const [status,   setStatus]   = useState('');

  const deviceIds = Object.keys(devices);

  async function handleEnroll() {
    const uid = userId.trim().replace(/\s+/g, '_');
    if (!deviceId) { showToast('Select a device first', 'info'); return; }
    if (!uid)      { showToast('Enter a User ID',        'info'); return; }
    if (!name.trim()) { showToast("Enter the user's name", 'info'); return; }

    try {
      await onSendEnroll(deviceId, uid, name.trim());
      setStatus(`Enrollment command sent — "${name.trim()}" should look at the camera now.`);
      setUserId('');
      setName('');
    } catch {
      setStatus('');
    }
  }

  return (
    <div className="enroll-panel">
      <h3 className="enroll-title">Enroll New User via Dashboard</h3>
      <p className="section-subtitle">
        Sends a command to the device. The user must then look at the camera within 30 s.
      </p>
      <div className="enroll-form">
        <select
          id="enrollDevice"
          className="select"
          value={deviceId}
          onChange={e => setDeviceId(e.target.value)}
        >
          <option value="">Select device...</option>
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
        <input
          id="enrollUserId"
          type="text"
          className="input"
          placeholder="User ID  (e.g. john_doe)"
          value={userId}
          onChange={e => setUserId(e.target.value)}
        />
        <input
          id="enrollName"
          type="text"
          className="input"
          placeholder="Full Name (e.g. John Doe)"
          value={name}
          onChange={e => setName(e.target.value)}
        />
        <button id="enrollBtn" className="btn btn-primary" onClick={handleEnroll}>
          Send Enrollment Command
        </button>
      </div>
      <div id="enrollStatus" className="enroll-status">{status}</div>
    </div>
  );
}
