import { useState, useRef, useEffect } from 'react';
import { useToast } from './ToastContainer.jsx';
import { extractFeatures, averageFeatures } from '../utils/irisFeatures.js';

/**
 * Enrollment panel with two modes:
 *
 *   "Device Camera"  — sends a command to the ESP32; device captures the iris
 *                      locally via its OV2640 (original flow, for when the
 *                      ESP32-CAM module is in use).
 *
 *   "Laptop Camera"  — captures 5 averaged frames from the laptop webcam,
 *                      derives the template in the browser, and stores it
 *                      directly in Firebase /users/{userId}. No ESP32 camera
 *                      required.
 */
export default function EnrollPanel({ devices, onSendEnroll, onWebcamEnroll }) {
  const showToast = useToast();

  const [mode,     setMode]     = useState('webcam'); // 'device' | 'webcam'
  const [deviceId, setDeviceId] = useState('');
  const [userId,   setUserId]   = useState('');
  const [name,     setName]     = useState('');

  // Webcam enrollment state
  const videoRef  = useRef(null);
  const canvasRef = useRef(null);
  const streamRef = useRef(null);
  const [camPhase,  setCamPhase]  = useState('idle'); // idle|live|capturing|done
  const [progress,  setProgress]  = useState(0);
  const [camErr,    setCamErr]    = useState('');
  const [template,  setTemplate]  = useState(null);

  const deviceIds = Object.keys(devices);

  useEffect(() => () => stopStream(), []);

  function stopStream() {
    streamRef.current?.getTracks().forEach(t => t.stop());
    streamRef.current = null;
  }

  function switchMode(m) {
    stopStream();
    setCamPhase('idle');
    setProgress(0);
    setTemplate(null);
    setCamErr('');
    setMode(m);
  }

  // ── Device camera mode ────────────────────────────────────────────────────

  async function handleDeviceEnroll() {
    const uid = userId.trim().replace(/\s+/g, '_');
    if (!deviceId)    { showToast('Select a device first', 'info'); return; }
    if (!uid)         { showToast('Enter a User ID', 'info'); return; }
    if (!name.trim()) { showToast("Enter the user's name", 'info'); return; }
    try {
      await onSendEnroll(deviceId, uid, name.trim());
      setUserId(''); setName('');
    } catch {}
  }

  // ── Webcam mode ───────────────────────────────────────────────────────────

  async function openCamera() {
    setCamErr('');
    try {
      const s = await navigator.mediaDevices.getUserMedia({ video: true });
      streamRef.current = s;
      videoRef.current.srcObject = s;
      setCamPhase('live');
    } catch {
      setCamErr('Camera access denied or unavailable');
    }
  }

  function cancelWebcam() {
    stopStream();
    setCamPhase('idle');
    setProgress(0);
    setTemplate(null);
  }

  async function captureFrames() {
    const uid = userId.trim().replace(/\s+/g, '_');
    if (!deviceId)    { showToast('Select a device first', 'info'); return; }
    if (!uid)         { showToast('Enter a User ID', 'info'); return; }
    if (!name.trim()) { showToast("Enter the user's name", 'info'); return; }

    setCamPhase('capturing');
    const frames = [];
    for (let i = 0; i < 5; i++) {
      await new Promise(r => setTimeout(r, 300));
      frames.push(extractFeatures(videoRef.current, canvasRef.current));
      setProgress(i + 1);
    }
    setTemplate(averageFeatures(frames));
    setCamPhase('done');
  }

  async function confirmEnroll() {
    const uid = userId.trim().replace(/\s+/g, '_');
    try {
      await onWebcamEnroll(deviceId, uid, name.trim(), template);
      cancelWebcam();
      setUserId(''); setName('');
    } catch (e) {
      showToast(`Enrollment failed: ${e.message}`, 'alert');
    }
  }

  // ── Render ────────────────────────────────────────────────────────────────

  return (
    <div className="enroll-panel">
      <h3 className="enroll-title">Enroll New User</h3>

      {/* Mode tabs */}
      <div className="enroll-tabs">
        <button className={`enroll-tab ${mode === 'webcam'  ? 'active' : ''}`} onClick={() => switchMode('webcam')}>
          Laptop Camera
        </button>
        <button className={`enroll-tab ${mode === 'device'  ? 'active' : ''}`} onClick={() => switchMode('device')}>
          Device Camera
        </button>
      </div>

      {/* Shared input fields */}
      <div className="enroll-form">
        <select className="select" value={deviceId} onChange={e => setDeviceId(e.target.value)}>
          <option value="">Select device...</option>
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
        <input type="text" className="input" placeholder="User ID  (e.g. john_doe)"
          value={userId} onChange={e => setUserId(e.target.value)} />
        <input type="text" className="input" placeholder="Full Name (e.g. John Doe)"
          value={name} onChange={e => setName(e.target.value)} />
      </div>

      {/* ── Device camera mode ── */}
      {mode === 'device' && (
        <div>
          <p className="section-subtitle">
            Sends a command to the ESP32. The user must look at the device camera within 30 s.
          </p>
          <button className="btn btn-primary" onClick={handleDeviceEnroll}>
            Send Enrollment Command
          </button>
        </div>
      )}

      {/* ── Laptop camera mode ── */}
      {mode === 'webcam' && (
        <div className="webcam-enroll-body">
          <p className="section-subtitle">
            Captures 5 frames from your laptop camera, averages the iris descriptor,
            and stores the template directly in Firebase — no ESP32 camera required.
          </p>

          {camPhase === 'idle' && (
            <>
              {camErr && <p className="webcam-err">{camErr}</p>}
              <button className="btn btn-primary" onClick={openCamera}>Open Camera</button>
            </>
          )}

          {camPhase !== 'idle' && (
            <div className="webcam-live">
              <div className="webcam-video-wrap">
                <video ref={videoRef} autoPlay playsInline muted className="webcam-video" />
                <canvas ref={canvasRef} style={{ display: 'none' }} />
                {camPhase === 'capturing' && (
                  <div className="webcam-overlay overlay-capture">
                    Capturing {progress} / 5...
                  </div>
                )}
                {camPhase === 'done' && (
                  <div className="webcam-overlay overlay-ok">Template ready</div>
                )}
              </div>

              <div className="webcam-controls">
                {camPhase === 'live' && (
                  <button className="btn btn-primary" onClick={captureFrames}>
                    Capture (5 frames)
                  </button>
                )}
                {camPhase === 'done' && (
                  <button className="btn btn-primary" onClick={confirmEnroll}>
                    Save &amp; Enroll
                  </button>
                )}
                <button className="btn btn-danger-outline btn-sm" onClick={cancelWebcam}>
                  Cancel
                </button>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
