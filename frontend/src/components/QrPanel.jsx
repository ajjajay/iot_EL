import { useState, useEffect, useRef } from 'react';
import { ref, onValue, set } from 'firebase/database';
import { db } from '../firebase.js';
import { useToast } from './ToastContainer.jsx';
import jsQR from 'jsqr';

const BACKEND_URL = import.meta.env.VITE_API_URL ?? '';

export default function QrPanel({ devices }) {
  const showToast = useToast();

  const [deviceId,   setDeviceId]   = useState('');
  const [requesting, setRequesting] = useState(false);
  const [scanning,   setScanning]   = useState(false);
  const [result,     setResult]     = useState(null);
  const [camErr,     setCamErr]     = useState('');
  const [detected,   setDetected]   = useState('');
  const [qrData,     setQrData]     = useState(null);  // { emailSent, expiresIn }
  const [qrUnlocked, setQrUnlocked] = useState(false);

  const videoRef   = useRef(null);
  const canvasRef  = useRef(null);
  const streamRef  = useRef(null);
  const rafRef     = useRef(null);

  const deviceIds = Object.keys(devices);

  useEffect(() => {
    if (deviceIds.length > 0 && !deviceId) setDeviceId(deviceIds[0]);
  }, [deviceIds.length]);

  // Listen for ESP32 keypad triple-press → auto-trigger QR request
  useEffect(() => {
    if (!deviceId || !db) return;
    const path = ref(db, `/devices/${deviceId}/commands/qrRequest`);
    const unsub = onValue(path, snap => {
      const val = snap.val();
      if (val?.pending === true) handleRequest(deviceId);
    });
    return () => unsub();
  }, [deviceId]);

  // Listen for keypad QR password unlock → show/hide Generate button
  useEffect(() => {
    if (!deviceId || !db) return;
    const path = ref(db, `/devices/${deviceId}/commands/qrUnlocked`);
    const unsub = onValue(path, snap => {
      setQrUnlocked(snap.val() === true);
    });
    return () => { unsub(); setQrUnlocked(false); };
  }, [deviceId]);

  useEffect(() => () => stopCamera(), []);

  // ── Camera / QR scan ────────────────────────────────────────────────────────

  function stopCamera() {
    cancelAnimationFrame(rafRef.current);
    streamRef.current?.getTracks().forEach(t => t.stop());
    streamRef.current = null;
  }

  async function openScanner() {
    setCamErr('');
    setResult(null);
    setDetected('');
    if (!navigator.mediaDevices?.getUserMedia) {
      setCamErr('Camera unavailable — open the page on https://localhost.');
      return;
    }
    try {
      const s = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: { ideal: 'environment' } },  // rear cam on mobile, front on desktop
        audio: false,
      });
      streamRef.current = s;
      setScanning(true);
    } catch (err) {
      setCamErr(
        err.name === 'NotAllowedError'  ? 'Camera permission denied.' :
        err.name === 'NotFoundError'    ? 'No camera found.'          :
        err.name === 'NotReadableError' ? 'Camera in use by another app or tab — close it and try again.' :
        `Camera error: ${err.message}`
      );
    }
  }

  function closeScanner() {
    stopCamera();
    setScanning(false);
    setDetected('');
  }

  // Wire video stream once scanning = true
  useEffect(() => {
    if (!scanning || !videoRef.current || !streamRef.current) return;
    videoRef.current.srcObject = streamRef.current;
    videoRef.current.play().catch(() => {});
    scanLoop();
  }, [scanning]);

  function scanLoop() {
    const video  = videoRef.current;
    const canvas = canvasRef.current;
    if (!video || !canvas) return;

    const tick = () => {
      if (video.readyState === video.HAVE_ENOUGH_DATA) {
        canvas.width  = video.videoWidth;
        canvas.height = video.videoHeight;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(video, 0, 0);
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const code = jsQR(imageData.data, imageData.width, imageData.height);

        if (code?.data) {
          const token = code.data.trim().toUpperCase();
          setDetected(token);
          stopCamera();
          setScanning(false);
          verifyToken(token);
          return;   // stop loop once found
        }
      }
      rafRef.current = requestAnimationFrame(tick);
    };
    rafRef.current = requestAnimationFrame(tick);
  }

  // ── Request + verify ────────────────────────────────────────────────────────

  async function handleRequest(devId) {
    const id = devId ?? deviceId;
    if (!id) { showToast('Select a device first', 'info'); return; }
    setRequesting(true);
    setResult(null);
    setQrData(null);
    try {
      const resp = await fetch(`${BACKEND_URL}/api/qr/request`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ deviceId: id }),
      });
      if (!resp.ok) throw new Error(`Server error ${resp.status}`);
      const data = await resp.json();
      setQrData(data);
      // Reset the keypad unlock so the button hides again after use
      if (db && id) {
        set(ref(db, `/devices/${id}/commands/qrUnlocked`), false).catch(() => {});
      }
      showToast(data.emailSent ? 'QR code emailed — open camera to scan it' : 'QR generated — open camera to scan', 'ok');
      openScanner();
    } catch (e) {
      showToast(`Request failed: ${e.message}`, 'alert');
    } finally {
      setRequesting(false);
    }
  }

  async function verifyToken(token) {
    if (!deviceId) { showToast('Select a device first', 'info'); return; }
    try {
      const resp = await fetch(`${BACKEND_URL}/api/qr/verify`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ token, deviceId }),
      });
      if (!resp.ok) throw new Error(`Server error ${resp.status}`);
      const data = await resp.json();
      setResult(data);
      if (data.granted) {
        showToast('QR verified — access granted', 'ok');
      } else {
        const reasons = {
          invalid_token:   'Invalid QR code',
          already_used:    'QR code already used',
          expired:         'QR code expired — request a new one',
          device_mismatch: 'QR code is for a different device',
        };
        showToast(`Denied — ${reasons[data.reason] ?? data.reason}`, 'alert');
      }
    } catch (e) {
      showToast(`Verify failed: ${e.message}`, 'alert');
    }
  }

  // ── Render ──────────────────────────────────────────────────────────────────

  return (
    <div className="enroll-panel" style={{ marginTop: '1.5rem' }}>
      <h3 className="enroll-title">QR Code Fallback Access</h3>
      <p className="section-subtitle">
        When facial recognition fails — press <b>*</b> on the keypad, enter the
        4-digit password to unlock this panel, then generate a one-time QR code.
        It will be emailed to the authorised recipient who shows it on their phone.
        Use the camera here to scan it and unlock.
      </p>

      {/* Device selector */}
      <div className="enroll-form">
        <select className="select" value={deviceId} onChange={e => setDeviceId(e.target.value)}>
          <option value="">Select device…</option>
          {deviceIds.map(id => <option key={id} value={id}>{id}</option>)}
        </select>
      </div>

      {/* Step 1 — generate QR (only visible after keypad password) */}
      <div style={{ marginTop: '0.75rem' }}>
        {qrUnlocked ? (
          <button
            className="btn btn-primary"
            onClick={() => handleRequest()}
            disabled={requesting || !deviceId}
          >
            {requesting ? 'Generating…' : '🔑 Generate QR Code'}
          </button>
        ) : (
          <p style={{ color: 'var(--color-muted, #888)', fontSize: '0.85rem', margin: 0 }}>
            🔒 Press <b>*</b> on the keypad then enter the 4-digit password to unlock QR access.
          </p>
        )}
      </div>

      {/* Step 2 — camera scanner (auto-opens after generating) */}
      {qrData && !result && (
        <div style={{ marginTop: '0.75rem', display: 'flex', gap: '0.5rem', alignItems: 'center', flexWrap: 'wrap' }}>
          {qrData.emailSent && (
            <p style={{ color: 'var(--color-ok, #22c55e)', fontSize: '0.85rem', margin: 0, width: '100%' }}>
              ✓ QR code sent to email — ask recipient to show it, then scan below
            </p>
          )}
          {!scanning ? (
            <button className="btn btn-primary" onClick={openScanner} disabled={!deviceId}>
              📷 Open Camera to Scan
            </button>
          ) : (
            <button className="btn btn-danger-outline btn-sm" onClick={closeScanner}>
              Cancel Scan
            </button>
          )}
        </div>
      )}

      {camErr && <p className="webcam-err" style={{ marginTop: '0.5rem' }}>{camErr}</p>}

      {/* Camera view */}
      {scanning && (
        <div className="webcam-live" style={{ marginTop: '0.75rem' }}>
          <div className="webcam-video-wrap">
            <video ref={videoRef} autoPlay playsInline muted className="webcam-video" />
            <div className="webcam-overlay overlay-capture" style={{ fontSize: '0.9rem' }}>
              Point camera at QR code…
            </div>
          </div>
        </div>
      )}

      {/* Hidden canvas for jsQR */}
      <canvas ref={canvasRef} style={{ display: 'none' }} />

      {/* Detected token (before verify completes) */}
      {detected && !result && (
        <p style={{ marginTop: '0.5rem', fontWeight: 'bold', letterSpacing: '2px' }}>
          Scanned: {detected} — verifying…
        </p>
      )}

      {/* Result */}
      {result && (
        <div className={`auth-result-badge ${result.granted ? 'result-ok' : 'result-fail'}`}
             style={{ marginTop: '0.75rem' }}>
          {result.granted
            ? '✓ Access granted — door unlocked'
            : `✕ Access denied — ${result.reason?.replace(/_/g, ' ')}`}
        </div>
      )}

      {/* Try again */}
      {result && !result.granted && (
        <button
          className="btn btn-primary"
          style={{ marginTop: '0.5rem' }}
          onClick={() => { setResult(null); setDetected(''); setQrData(null); }}
        >
          Try Again
        </button>
      )}
    </div>
  );
}
