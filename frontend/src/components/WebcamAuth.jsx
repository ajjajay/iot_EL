import { useRef, useState, useEffect } from 'react';
import { extractFeatures } from '../utils/irisFeatures.js';
import { matchAgainstUsers } from '../utils/biometricMatch.js';

/**
 * Laptop-webcam authentication panel.
 *
 * Props:
 *   devices   — Firebase /devices snapshot value (for device selector)
 *   users     — Firebase /users snapshot value   (templates for matching)
 *   onResult  — async (deviceId, matchResult) called after each attempt;
 *               parent writes sign-in event + relay command to Firebase
 */
export default function WebcamAuth({ devices, users, onResult }) {
  const videoRef  = useRef(null);
  const canvasRef = useRef(null);
  const streamRef = useRef(null);

  const [deviceId, setDeviceId] = useState('');
  const [phase,    setPhase]    = useState('idle');   // idle | live | result
  const [result,   setResult]   = useState(null);
  const [camErr,   setCamErr]   = useState('');
  const [busy,     setBusy]     = useState(false);

  const deviceIds = Object.keys(devices);

  // Pre-select first device
  useEffect(() => {
    if (deviceIds.length > 0 && !deviceId) setDeviceId(deviceIds[0]);
  }, [deviceIds.length]);

  // Stop stream on unmount
  useEffect(() => () => stopStream(), []);

  function stopStream() {
    streamRef.current?.getTracks().forEach(t => t.stop());
    streamRef.current = null;
  }

  async function openCamera() {
    setCamErr('');
    try {
      const s = await navigator.mediaDevices.getUserMedia({ video: true });
      streamRef.current = s;
      videoRef.current.srcObject = s;
      setPhase('live');
    } catch {
      setCamErr('Camera access denied or unavailable');
    }
  }

  function closeCamera() {
    stopStream();
    setPhase('idle');
    setResult(null);
  }

  async function authenticate() {
    if (!deviceId) return;
    setBusy(true);
    const features = extractFeatures(videoRef.current, canvasRef.current);
    const r = matchAgainstUsers(features, users);
    setResult(r);
    setPhase('result');
    try { await onResult(deviceId, r); } catch {}
    setBusy(false);
  }

  const enrolledWithTemplates = Object.values(users).filter(u => u.active && Array.isArray(u.template)).length;

  return (
    <div className="webcam-auth-panel">
      <div className="webcam-auth-header">
        <h3 className="enroll-title">Authenticate via Laptop Camera</h3>
        <select className="select" value={deviceId} onChange={e => setDeviceId(e.target.value)}>
          {deviceIds.length === 0 && <option value="">No devices</option>}
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
      </div>

      {enrolledWithTemplates === 0 && (
        <p className="webcam-warn">
          No users with webcam templates found. Enroll at least one user using
          the "Laptop Camera" tab in the Enroll panel below.
        </p>
      )}

      {phase === 'idle' && (
        <div className="webcam-idle">
          <p className="section-subtitle">
            Capture your iris via the laptop camera. A successful match unlocks the
            relay on the selected device and logs a sign-in event.
          </p>
          {camErr && <p className="webcam-err">{camErr}</p>}
          <button className="btn btn-primary" onClick={openCamera}>
            Open Camera
          </button>
        </div>
      )}

      {(phase === 'live' || phase === 'result') && (
        <div className="webcam-live">
          <div className="webcam-video-wrap">
            <video ref={videoRef} autoPlay playsInline muted className="webcam-video" />
            <canvas ref={canvasRef} style={{ display: 'none' }} />
            {phase === 'result' && result && (
              <div className={`webcam-overlay ${result.matched ? 'overlay-ok' : 'overlay-fail'}`}>
                {result.matched ? `✓ ${result.userName}` : '✗ No match'}
              </div>
            )}
          </div>

          <div className="webcam-controls">
            {phase === 'live' && (
              <button className="btn btn-primary" onClick={authenticate} disabled={busy || !deviceId || enrolledWithTemplates === 0}>
                {busy ? 'Matching...' : 'Authenticate'}
              </button>
            )}
            {phase === 'result' && (
              <button className="btn btn-primary" onClick={() => { setResult(null); setPhase('live'); }}>
                Try Again
              </button>
            )}
            <button className="btn btn-danger-outline btn-sm" onClick={closeCamera}>
              Close Camera
            </button>
          </div>

          {phase === 'result' && result && (
            <div className={`auth-result-badge ${result.matched ? 'result-ok' : 'result-fail'}`}>
              {result.matched
                ? `Access granted — ${result.userName} (score: ${result.score.toFixed(3)})`
                : `Access denied (score: ${result.score.toFixed(3)})`}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
