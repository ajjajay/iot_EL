import { useState, useRef, useEffect } from 'react';
import { useToast } from './ToastContainer.jsx';
import { extractFeatures, averageFeatures } from '../utils/irisFeatures.js';

const AUTH_API_URL   = import.meta.env.VITE_AUTH_API_URL ?? '';
const USE_REKOGNITION = !!AUTH_API_URL;

// Captures the current video frame as a base64 JPEG at native resolution.
function captureJpegBase64(videoEl) {
  const w   = videoEl.videoWidth  || 640;
  const h   = videoEl.videoHeight || 480;
  const tmp = document.createElement('canvas');
  tmp.width  = w;
  tmp.height = h;
  tmp.getContext('2d').drawImage(videoEl, 0, 0, w, h);
  return tmp.toDataURL('image/jpeg', 0.85).split(',')[1];
}

export default function EnrollPanel({ devices, onSendEnroll, onWebcamEnroll }) {
  const showToast = useToast();

  const [mode,     setMode]     = useState('webcam'); // 'device' | 'webcam'
  const [deviceId, setDeviceId] = useState('');
  const [userId,   setUserId]   = useState('');
  const [name,     setName]     = useState('');

  const videoRef  = useRef(null);
  const canvasRef = useRef(null);
  const streamRef = useRef(null);

  // camPhase: idle | live | capturing | done
  const [camPhase,    setCamPhase]    = useState('idle');
  const [progress,    setProgress]    = useState(0);
  const [camErr,      setCamErr]      = useState('');
  const [template,    setTemplate]    = useState(null);   // local mode: float32 array
  const [imageBase64, setImageBase64] = useState(null);   // Rekognition mode: JPEG b64

  const deviceIds = Object.keys(devices);

  useEffect(() => {
    if (camPhase !== 'idle' && videoRef.current && streamRef.current) {
      videoRef.current.srcObject = streamRef.current;
      videoRef.current.play().catch(() => {});
    }
  }, [camPhase]);

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
    setImageBase64(null);
    setCamErr('');
    setMode(m);
  }

  // ── Device camera mode ──────────────────────────────────────────────────────

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

  // ── Laptop camera mode ──────────────────────────────────────────────────────

  async function openCamera() {
    setCamErr('');
    if (!navigator.mediaDevices?.getUserMedia) {
      setCamErr('Camera API unavailable — open the page on https://localhost and accept the certificate warning.');
      return;
    }
    try {
      const s = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user' }, audio: false });
      streamRef.current = s;
      setCamPhase('live');
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setCamErr('Camera permission denied. Allow access in the browser address bar.');
      } else if (err.name === 'NotFoundError') {
        setCamErr('No camera found on this device.');
      } else {
        setCamErr(`Camera error: ${err.message}`);
      }
    }
  }

  function cancelWebcam() {
    stopStream();
    setCamPhase('idle');
    setProgress(0);
    setTemplate(null);
    setImageBase64(null);
  }

  async function captureFrames() {
    const uid = userId.trim().replace(/\s+/g, '_');
    if (!deviceId)    { showToast('Select a device first', 'info'); return; }
    if (!uid)         { showToast('Enter a User ID', 'info'); return; }
    if (!name.trim()) { showToast("Enter the user's name", 'info'); return; }

    setCamPhase('capturing');

    if (USE_REKOGNITION) {
      // Rekognition mode: capture a single JPEG snapshot
      await new Promise(r => setTimeout(r, 200)); // brief pause so face is stable
      setImageBase64(captureJpegBase64(videoRef.current));
      setCamPhase('done');
    } else {
      // Local mode: capture 5 frames, average iris descriptors
      const frames = [];
      for (let i = 0; i < 5; i++) {
        await new Promise(r => setTimeout(r, 350));
        frames.push(extractFeatures(videoRef.current, canvasRef.current));
        setProgress(i + 1);
      }
      setTemplate(averageFeatures(frames));
      setCamPhase('done');
    }
  }

  async function confirmEnroll() {
    const uid = userId.trim().replace(/\s+/g, '_');
    try {
      if (USE_REKOGNITION && imageBase64) {
        // Send JPEG to Lambda enroll endpoint → Rekognition IndexFaces → Firebase write
        const resp = await fetch(`${AUTH_API_URL}/enroll`, {
          method:  'POST',
          headers: { 'Content-Type': 'application/json' },
          body:    JSON.stringify({
            deviceId,
            userId:      uid,
            name:        name.trim(),
            imageBase64,
          }),
        });
        if (!resp.ok) throw new Error(`Enroll API error: ${resp.status}`);
        const data = await resp.json();

        if (data.status === 'no_face') {
          showToast('No face detected — try again with better lighting', 'alert');
          setCamPhase('live');  // let them re-capture
          setImageBase64(null);
          return;
        }

        // Lambda wrote the user record to Firebase; notify parent (no template)
        await onWebcamEnroll(deviceId, uid, name.trim(), null);
        cancelWebcam();
        setUserId(''); setName('');
      } else if (template) {
        // Local mode: pass the averaged template to parent, which saves it to Firebase
        await onWebcamEnroll(deviceId, uid, name.trim(), template);
        cancelWebcam();
        setUserId(''); setName('');
      }
    } catch (e) {
      showToast(`Enrollment failed: ${e.message}`, 'alert');
    }
  }

  // ── Render ──────────────────────────────────────────────────────────────────

  return (
    <div className="enroll-panel">
      <h3 className="enroll-title">Enroll New User</h3>

      <div className="enroll-tabs">
        <button className={`enroll-tab ${mode === 'webcam' ? 'active' : ''}`} onClick={() => switchMode('webcam')}>
          Laptop Camera
        </button>
        <button className={`enroll-tab ${mode === 'device' ? 'active' : ''}`} onClick={() => switchMode('device')}>
          Device Camera
        </button>
      </div>

      <div className="enroll-form">
        <select className="select" value={deviceId} onChange={e => setDeviceId(e.target.value)}>
          <option value="">Select device…</option>
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
        <input type="text" className="input" placeholder="User ID (e.g. john_doe)"
          value={userId} onChange={e => setUserId(e.target.value)} />
        <input type="text" className="input" placeholder="Full Name (e.g. John Doe)"
          value={name} onChange={e => setName(e.target.value)} />
      </div>

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

      {mode === 'webcam' && (
        <div className="webcam-enroll-body">
          <p className="section-subtitle">
            {USE_REKOGNITION
              ? 'Captures a photo from your laptop camera and indexes your face in AWS Rekognition.'
              : 'Captures 5 frames from your laptop camera, averages the iris descriptor, and stores the template directly in Firebase.'}
          </p>

          <div style={{ display: camPhase === 'idle' ? 'none' : 'block' }}>
            <div className="webcam-live">
              <div className="webcam-video-wrap">
                <video ref={videoRef} autoPlay playsInline muted className="webcam-video" />
                <canvas ref={canvasRef} style={{ display: 'none' }} />
                {camPhase === 'capturing' && (
                  <div className="webcam-overlay overlay-capture">
                    {USE_REKOGNITION ? 'Capturing photo…' : `Capturing ${progress} / 5…`}
                  </div>
                )}
                {camPhase === 'done' && (
                  <div className="webcam-overlay overlay-ok">✓ Ready to enroll</div>
                )}
              </div>

              <div className="webcam-controls">
                {camPhase === 'live' && (
                  <button className="btn btn-primary" onClick={captureFrames}>
                    {USE_REKOGNITION ? 'Capture Photo' : 'Capture (5 frames)'}
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
          </div>

          {camPhase === 'idle' && (
            <>
              {camErr && <p className="webcam-err">{camErr}</p>}
              <button className="btn btn-primary" onClick={openCamera}>Open Camera</button>
            </>
          )}
        </div>
      )}
    </div>
  );
}
