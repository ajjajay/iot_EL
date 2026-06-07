import { useRef, useState, useEffect } from 'react';
import { extractFeatures } from '../utils/irisFeatures.js';
import { matchAgainstUsers } from '../utils/biometricMatch.js';
import { checkLiveness } from '../utils/livenessCheck.js';

const AUTH_API_URL = import.meta.env.VITE_AUTH_API_URL ?? '';

// Captures the current video frame as a base64 JPEG at the video's native resolution.
function captureJpegBase64(videoEl) {
  const w   = videoEl.videoWidth  || 640;
  const h   = videoEl.videoHeight || 480;
  const tmp = document.createElement('canvas');
  tmp.width  = w;
  tmp.height = h;
  tmp.getContext('2d').drawImage(videoEl, 0, 0, w, h);
  return tmp.toDataURL('image/jpeg', 0.85).split(',')[1];
}

// phase: idle → live → liveness → result
export default function WebcamAuth({ devices, users, onResult }) {
  const videoRef  = useRef(null);
  const canvasRef = useRef(null);
  const streamRef = useRef(null);

  const [deviceId,      setDeviceId]      = useState('');
  const [phase,         setPhase]         = useState('idle');
  const [result,        setResult]        = useState(null);
  const [liveness,      setLiveness]      = useState(null);
  const [camErr,        setCamErr]        = useState('');
  const [busy,          setBusy]          = useState(false);
  const [progress,      setProgress]      = useState(0);
  const [livenessPhase, setLivenessPhase] = useState('');

  const deviceIds = Object.keys(devices);

  useEffect(() => {
    if (deviceIds.length > 0 && !deviceId) setDeviceId(deviceIds[0]);
  }, [deviceIds.length]);

  useEffect(() => {
    if (phase !== 'idle' && videoRef.current && streamRef.current) {
      videoRef.current.srcObject = streamRef.current;
      videoRef.current.play().catch(() => {});
    }
  }, [phase]);

  useEffect(() => () => stopStream(), []);

  function stopStream() {
    streamRef.current?.getTracks().forEach(t => t.stop());
    streamRef.current = null;
  }

  async function openCamera() {
    setCamErr('');
    if (!navigator.mediaDevices?.getUserMedia) {
      setCamErr('Camera unavailable — open the page on https://localhost and accept the certificate warning.');
      return;
    }
    try {
      const s = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user' }, audio: false });
      streamRef.current = s;
      setPhase('live');
    } catch (err) {
      if (err.name === 'NotAllowedError') {
        setCamErr('Camera permission denied — click the camera icon in the address bar and allow access.');
      } else if (err.name === 'NotFoundError') {
        setCamErr('No camera found on this device.');
      } else {
        setCamErr(`Camera error: ${err.message}`);
      }
    }
  }

  function closeCamera() {
    stopStream();
    setPhase('idle');
    setResult(null);
    setLiveness(null);
    setProgress(0);
    setLivenessPhase('');
  }

  async function runLivenessAndAuth() {
    if (!deviceId) { setCamErr('Select a device first.'); return; }
    setBusy(true);
    setCamErr('');
    setLiveness(null);
    setPhase('liveness');
    setProgress(0);
    setLivenessPhase('Starting…');

    try {
      // ── Step 1: Liveness check (anti-spoofing) ────────────────────────────
      const lv = await checkLiveness(videoRef.current, canvasRef.current, {
        onPhase:    label => setLivenessPhase(label),
        onProgress: pct   => setProgress(pct),
      });
      setLiveness(lv);

      if (!lv.live) {
        const r = {
          matched: false, granted: false, userId: null, userName: null,
          score: 1, livenessScore: lv.score, livenessFailed: true,
        };
        setResult(r);
        setPhase('result');
        try { await onResult(deviceId, r); } catch {}
        return;
      }

      // ── Step 2: Face recognition ──────────────────────────────────────────
      let r;

      if (AUTH_API_URL) {
        // Cloud path: send JPEG to API Gateway → Lambda → Rekognition
        setLivenessPhase('Sending to Rekognition…');
        setProgress(100);

        const imageBase64 = captureJpegBase64(videoRef.current);
        const resp = await fetch(`${AUTH_API_URL}/auth`, {
          method:  'POST',
          headers: { 'Content-Type': 'application/json' },
          body:    JSON.stringify({ deviceId, imageBase64 }),
        });

        if (!resp.ok) throw new Error(`Auth API error: ${resp.status}`);
        const data = await resp.json();

        r = {
          matched:        data.granted,
          granted:        data.granted,
          reason:         data.reason  ?? null,
          userId:         data.userId  ?? null,
          userName:       data.userName ?? (data.granted ? 'Recognised' : 'Unknown'),
          score:          data.matchScore ?? (data.granted ? 0 : 1),
          livenessScore:  lv.score,
          livenessFailed: false,
          lambdaHandled:  true,   // signals App.jsx that Firebase writes are done
        };
      } else {
        // Local fallback: run iris matching in-browser (demo / no API URL set)
        const features = extractFeatures(videoRef.current, canvasRef.current);
        const enrolledUsers = Object.values(users).filter(u => u.active && Array.isArray(u.template));

        if (enrolledUsers.length === 0) {
          r = {
            matched: false, granted: false, userId: null,
            userName: 'No enrolled users', score: 1,
            livenessScore: lv.score, livenessFailed: false,
          };
        } else {
          const m = matchAgainstUsers(features, users);
          r = { ...m, granted: m.matched, livenessScore: lv.score, livenessFailed: false };
        }
      }

      setResult(r);
      setPhase('result');
      try { await onResult(deviceId, r); } catch {}

    } catch (e) {
      setCamErr(`Error during check: ${e.message}`);
      setPhase('live');
    } finally {
      setBusy(false);
      setProgress(0);
    }
  }

  const enrolledWithTemplates = Object.values(users).filter(u => u.active && Array.isArray(u.template)).length;
  // In Rekognition mode users have no local template — hide the warning
  const showNoUsersWarning = !AUTH_API_URL && enrolledWithTemplates === 0;

  return (
    <div className="webcam-auth-panel">
      <div className="webcam-auth-header">
        <h3 className="enroll-title">Authenticate via Laptop Camera</h3>
        <select className="select" value={deviceId} onChange={e => setDeviceId(e.target.value)}>
          {deviceIds.length === 0 && <option value="">No devices</option>}
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
      </div>

      {/* Always-mounted video + canvas so refs are valid */}
      <div style={{ display: phase === 'idle' ? 'none' : 'block' }}>
        <div className="webcam-live">
          <div className="webcam-video-wrap">
            <video ref={videoRef} autoPlay playsInline muted className="webcam-video" />
            <canvas ref={canvasRef} style={{ display: 'none' }} width="160" height="120" />

            {phase === 'liveness' && (
              <div className="webcam-overlay overlay-capture">
                <div style={{ fontSize: '0.85rem', marginBottom: 6 }}>{livenessPhase || 'Analysing…'}</div>
                <div className="liveness-bar-wrap">
                  <div className="liveness-bar-fill" style={{ width: `${progress}%` }} />
                </div>
                <div style={{ fontSize: '0.75rem', marginTop: 4 }}>{progress}%</div>
              </div>
            )}

            {phase === 'result' && result && (
              <div className={`webcam-overlay ${result.livenessFailed ? 'overlay-fail' : result.matched ? 'overlay-ok' : 'overlay-fail'}`}>
                {result.livenessFailed
                  ? '✕ Spoof detected'
                  : result.matched
                    ? `✓ ${result.userName}`
                    : '✕ No match'}
              </div>
            )}
          </div>

          {phase === 'liveness' && progress < 85 && (
            <div className="blink-instruction">
              <span className="blink-eye-icon">👁</span>
              <span>Look straight at the camera and <strong>blink naturally</strong></span>
            </div>
          )}

          {liveness && (
            <div className={`liveness-card ${liveness.live ? 'liveness-ok' : 'liveness-fail'}`}>
              <div className="liveness-card-header">
                <span className="liveness-icon">{liveness.live ? '✓' : '✕'}</span>
                <span className="liveness-title">
                  {liveness.live ? 'Live face confirmed' : 'Liveness check FAILED'}
                </span>
                <span className="liveness-score">{liveness.score}/100</span>
              </div>
              <div className="liveness-bar-bg">
                <div
                  className={`liveness-score-bar ${liveness.live ? 'liveness-score-ok' : 'liveness-score-fail'}`}
                  style={{ width: `${liveness.score}%` }}
                />
              </div>
              <p className="liveness-reason">{liveness.reason}</p>
            </div>
          )}

          <div className="webcam-controls">
            {phase === 'live' && (
              <button className="btn btn-primary" onClick={runLivenessAndAuth} disabled={busy || !deviceId}>
                {busy ? 'Checking…' : '🔍 Liveness + Authenticate'}
              </button>
            )}
            {phase === 'result' && (
              <button className="btn btn-primary" onClick={() => { setResult(null); setLiveness(null); setPhase('live'); }}>
                Try Again
              </button>
            )}
            <button className="btn btn-danger-outline btn-sm" onClick={closeCamera}>
              Close Camera
            </button>
          </div>

          {phase === 'result' && result && !result.livenessFailed && (
            <div className={`auth-result-badge ${result.matched ? 'result-ok' : 'result-fail'}`}>
              {result.matched
                ? `Access granted — ${result.userName}  (score: ${result.score.toFixed(3)}, liveness: ${result.livenessScore}/100)`
                : result.reason === 'anomaly'
                  ? `Access denied — anomalous pattern detected for ${result.userName}`
                  : showNoUsersWarning
                    ? 'No webcam-enrolled users found. Go to Users → Enroll New User → Laptop Camera tab first.'
                    : `Access denied  (best score: ${result.score.toFixed(3)})`}
            </div>
          )}

          {phase === 'result' && result?.livenessFailed && (
            <div className="auth-result-badge result-fail">
              Anti-spoofing check failed (liveness score {result.livenessScore}/100). Possible photo or screen replay.
            </div>
          )}
        </div>
      </div>

      {phase === 'idle' && (
        <div className="webcam-idle">
          {showNoUsersWarning && (
            <p className="webcam-warn">
              No webcam-enrolled users yet. Go to <strong>Users</strong> → Enroll New User → Laptop Camera tab to enroll yourself first.
            </p>
          )}
          {camErr && <p className="webcam-err">{camErr}</p>}
          <button className="btn btn-primary" onClick={openCamera}>Open Camera</button>
        </div>
      )}
    </div>
  );
}
