/**
 * livenessCheck.js — Multi-signal anti-spoofing
 *
 * Signals used (in order of reliability):
 *
 *  1. FACE PRESENCE  — BlazeFace must detect a face in the frame.
 *                      No face = fail immediately.
 *
 *  2. BLINK DETECTION — We track eye-region brightness for 3 s (45 frames).
 *                       A live face shows at least one brightness dip in each
 *                       eye region (natural involuntary blink ~every 3-5 s).
 *                       A printed photo shows flat, non-dipping brightness.
 *
 *  3. FACE POSITION JITTER — Live faces drift slightly due to breathing.
 *                             A perfectly still bounding box over 3 s is a red flag.
 *
 *  4. TEXTURE SCORE — Laplacian variance of the face crop.
 *                     Screens/photos have abnormal sharpness compared to real skin.
 *
 * Each signal contributes to a 0–100 composite score.
 * Threshold for passing: score ≥ 45 AND no hard-fail triggered.
 */

let _model = null;

async function loadModel() {
  if (_model) return _model;
  // Dynamic imports keep the initial bundle small
  const [tfMod, bfMod] = await Promise.all([
    import('@tensorflow/tfjs'),
    import('@tensorflow-models/blazeface'),
  ]);
  // CJS interop: ready() may be on default or module namespace
  const tf = tfMod.default ?? tfMod;
  const blazeface = bfMod.default ?? bfMod;
  if (typeof tf.ready === 'function') await tf.ready();
  _model = await blazeface.load();
  return _model;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function patchMeanBrightness(ctx, cx, cy, w, h) {
  const x = Math.max(0, Math.round(cx - w / 2));
  const y = Math.max(0, Math.round(cy - h / 2));
  const pw = Math.min(w, ctx.canvas.width  - x);
  const ph = Math.min(h, ctx.canvas.height - y);
  if (pw <= 0 || ph <= 0) return 0.5;
  const { data } = ctx.getImageData(x, y, pw, ph);
  let sum = 0;
  for (let i = 0; i < data.length; i += 4) {
    sum += (data[i] * 0.299 + data[i + 1] * 0.587 + data[i + 2] * 0.114) / 255;
  }
  return sum / (data.length / 4);
}

function countBlinks(signal) {
  if (signal.length < 4) return 0;
  const mean = signal.reduce((a, b) => a + b, 0) / signal.length;
  // A blink = any run of consecutive frames significantly below mean
  const DIP_THRESHOLD = 0.04; // 4 pp below mean
  const MIN_DIP = 2, MAX_DIP = 10;
  let blinks = 0, inDip = false, dipLen = 0;
  for (const v of signal) {
    if (mean - v > DIP_THRESHOLD) {
      if (!inDip) { inDip = true; dipLen = 0; }
      dipLen++;
    } else {
      if (inDip) {
        if (dipLen >= MIN_DIP && dipLen <= MAX_DIP) blinks++;
        inDip = false; dipLen = 0;
      }
    }
  }
  return blinks;
}

function laplacianVariance(ctx, x, y, w, h) {
  const pw = Math.min(w, ctx.canvas.width  - x);
  const ph = Math.min(h, ctx.canvas.height - y);
  if (pw < 3 || ph < 3) return 0;
  const { data } = ctx.getImageData(x, y, pw, ph);
  const gray = new Float32Array(pw * ph);
  for (let i = 0; i < pw * ph; i++) {
    gray[i] = (data[i*4]*0.299 + data[i*4+1]*0.587 + data[i*4+2]*0.114) / 255;
  }
  let sumSq = 0, n = 0;
  for (let row = 1; row < ph - 1; row++) {
    for (let col = 1; col < pw - 1; col++) {
      const v =
        -gray[(row-1)*pw + col] + -gray[(row+1)*pw + col] +
        -gray[row*pw + (col-1)] + -gray[row*pw + (col+1)] +
         4*gray[row*pw + col];
      sumSq += v * v; n++;
    }
  }
  return n > 0 ? sumSq / n : 0;
}

// ── Main export ───────────────────────────────────────────────────────────────

/**
 * @param {HTMLVideoElement}  videoEl
 * @param {HTMLCanvasElement} canvasEl  — any scratch canvas
 * @param {object} opts
 * @param {Function} opts.onPhase  (phaseLabel: string) callback for UI messages
 * @param {Function} opts.onProgress (pct: 0-100) callback
 *
 * @returns {Promise<{
 *   live: boolean,
 *   score: number,        // 0–100
 *   reason: string,
 *   details: object,
 * }>}
 */
export async function checkLiveness(videoEl, canvasEl, {
  onPhase    = () => {},
  onProgress = () => {},
} = {}) {

  // ── Phase 0: Load BlazeFace ────────────────────────────────────────────────
  onPhase('Loading face detector…');
  onProgress(5);
  let model;
  try {
    model = await loadModel();
  } catch {
    // BlazeFace unavailable — fall back to motion-only check
    return motionOnlyFallback(videoEl, canvasEl, { onProgress });
  }

  // ── Phase 1: Face detection ────────────────────────────────────────────────
  onPhase('Detecting face…');
  onProgress(15);

  canvasEl.width  = videoEl.videoWidth  || 640;
  canvasEl.height = videoEl.videoHeight || 480;
  const ctx = canvasEl.getContext('2d');
  ctx.drawImage(videoEl, 0, 0);

  let predictions;
  try {
    predictions = await model.estimateFaces(videoEl, false);
  } catch {
    predictions = [];
  }

  if (!predictions || predictions.length === 0) {
    return fail('No face detected in the frame. Make sure your face is clearly visible and well-lit.', { faceFound: false });
  }

  // Pick highest-probability face
  const face = predictions.reduce((best, p) =>
    (p.probability?.[0] ?? p.probability ?? 0) > (best.probability?.[0] ?? best.probability ?? 0) ? p : best
  );

  // Extract bounding box and eye landmarks
  const [x1, y1] = face.topLeft;
  const [x2, y2] = face.bottomRight;
  const faceW = x2 - x1, faceH = y2 - y1;

  // BlazeFace landmarks: [rightEye, leftEye, nose, mouth, rightEar, leftEar]
  const landmarks = face.landmarks ?? [];
  const scaleX = canvasEl.width  / (videoEl.videoWidth  || 640);
  const scaleY = canvasEl.height / (videoEl.videoHeight || 480);

  let rightEye = landmarks[0] ? [landmarks[0][0], landmarks[0][1]] : [x1 + faceW * 0.35, y1 + faceH * 0.35];
  let leftEye  = landmarks[1] ? [landmarks[1][0], landmarks[1][1]] : [x1 + faceW * 0.65, y1 + faceH * 0.35];

  // Eye patch size: ~15% face width × 8% face height
  const eyePatchW = Math.max(10, faceW * 0.18);
  const eyePatchH = Math.max(6,  faceH * 0.10);

  // ── Phase 2: 3-second eye tracking (45 frames @ ~15fps) ───────────────────
  onPhase('Hold still — scanning eyes for blinks…');

  const FRAMES = 45, INTERVAL = 67; // ~15 fps × 3 s
  const rBright = [], lBright = [];
  const facePositions = [];

  for (let i = 0; i < FRAMES; i++) {
    await new Promise(r => setTimeout(r, INTERVAL));
    ctx.drawImage(videoEl, 0, 0, canvasEl.width, canvasEl.height);

    // Re-detect face every 15 frames to track position drift
    if (i % 15 === 0) {
      try {
        const preds = await model.estimateFaces(videoEl, false);
        if (preds && preds.length > 0) {
          const f = preds[0];
          facePositions.push(f.topLeft);
          if (f.landmarks) {
            rightEye = [f.landmarks[0][0], f.landmarks[0][1]];
            leftEye  = [f.landmarks[1][0], f.landmarks[1][1]];
          }
        }
      } catch {}
    }

    rBright.push(patchMeanBrightness(ctx, rightEye[0], rightEye[1], eyePatchW, eyePatchH));
    lBright.push(patchMeanBrightness(ctx,  leftEye[0],  leftEye[1], eyePatchW, eyePatchH));

    onProgress(15 + Math.round((i / FRAMES) * 65));
  }

  // ── Phase 3: Texture score on face crop ───────────────────────────────────
  onPhase('Analysing texture…');
  onProgress(85);

  ctx.drawImage(videoEl, 0, 0, canvasEl.width, canvasEl.height);
  const texScore = laplacianVariance(
    ctx,
    Math.max(0, Math.round(x1)), Math.max(0, Math.round(y1)),
    Math.round(faceW), Math.round(faceH)
  );

  // ── Score computation ──────────────────────────────────────────────────────
  onProgress(95);

  const rBlinks = countBlinks(rBright);
  const lBlinks = countBlinks(lBright);
  const totalBlinks = rBlinks + lBlinks; // sum across both eyes

  // Face position jitter: standard deviation of x-coordinate
  let jitterScore = 0;
  if (facePositions.length > 1) {
    const xs = facePositions.map(p => p[0]);
    const meanX = xs.reduce((a, b) => a + b, 0) / xs.length;
    const stdX  = Math.sqrt(xs.reduce((s, x) => s + (x - meanX) ** 2, 0) / xs.length);
    // Live: stdX > 0.5px over 3s; Photo: stdX ≈ 0
    jitterScore = Math.min(stdX / 3.0, 1);
  }

  // Normalise texture (empirical range 0.0005 – 0.015)
  const texNorm = Math.min(Math.max((texScore - 0.0005) / 0.015, 0), 1);

  // Blink score: ≥ 2 combined blinks across both eyes is very strong
  const blinkScore = Math.min(totalBlinks / 2, 1);

  // Composite
  const composite = (
    0.55 * blinkScore +
    0.25 * jitterScore +
    0.20 * texNorm
  );
  const score = Math.round(composite * 100);

  onProgress(100);

  const details = { rBlinks, lBlinks, totalBlinks, jitterScore: jitterScore.toFixed(3), texScore: texScore.toFixed(5), texNorm: texNorm.toFixed(3) };

  // Hard-fail: no blinks at all detected across 3 seconds is strong photo signal
  if (totalBlinks === 0) {
    return {
      live: false, score,
      reason: `No blinks detected across ${(FRAMES * INTERVAL / 1000).toFixed(1)} s — photos and static screens cannot blink naturally.`,
      details,
    };
  }

  if (score >= 45) {
    return {
      live: true, score,
      reason: `${totalBlinks} blink event${totalBlinks > 1 ? 's' : ''} detected with natural face motion — live face confirmed.`,
      details,
    };
  }

  return {
    live: false, score,
    reason: `Liveness score too low (${score}/100). Ensure good lighting and that your full face is visible.`,
    details,
  };
}

// ── Fallback (no BlazeFace) ───────────────────────────────────────────────────

async function motionOnlyFallback(videoEl, canvasEl, { onProgress }) {
  const W = 80, H = 60;
  canvasEl.width = W; canvasEl.height = H;
  const ctx = canvasEl.getContext('2d');
  const grays = [];

  for (let i = 0; i < 20; i++) {
    await new Promise(r => setTimeout(r, 80));
    ctx.drawImage(videoEl, 0, 0, W, H);
    const { data } = ctx.getImageData(0, 0, W, H);
    const g = new Float32Array(W * H);
    for (let j = 0; j < W * H; j++)
      g[j] = (data[j*4]*0.299 + data[j*4+1]*0.587 + data[j*4+2]*0.114) / 255;
    grays.push(g);
    onProgress(10 + i * 4);
  }

  let totalVar = 0;
  for (let p = 0; p < W * H; p++) {
    const vals = grays.map(g => g[p]);
    const mean = vals.reduce((a, b) => a + b, 0) / vals.length;
    totalVar += vals.reduce((s, v) => s + (v - mean) ** 2, 0) / vals.length;
  }
  const motionScore = totalVar / (W * H);
  const score = Math.round(Math.min(motionScore / 0.001, 1) * 100);
  const live  = score >= 30;

  return {
    live, score,
    reason: live
      ? 'Motion detected (face detector unavailable — limited check only).'
      : 'Insufficient motion — possible static image.',
    details: { motionScore },
  };
}

function fail(reason, details = {}) {
  return { live: false, score: 0, reason, details };
}
