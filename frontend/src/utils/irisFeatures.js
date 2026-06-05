// Same 8×8 zonal mean descriptor as the ESP32 firmware (IrisCamera::_extractFeatures).
// Frames from any resolution are scaled to 160×120 before extraction so the
// numeric range stays consistent between enrollment and authentication.

export function extractFeatures(videoEl, canvas) {
  const W = 160, H = 120;
  canvas.width  = W;
  canvas.height = H;
  const ctx = canvas.getContext('2d');
  ctx.drawImage(videoEl, 0, 0, W, H);
  const { data } = ctx.getImageData(0, 0, W, H);

  const features = new Array(64);
  for (let cy = 0; cy < 8; cy++) {
    for (let cx = 0; cx < 8; cx++) {
      const x0 = Math.round(cx * 20),  x1 = Math.round((cx + 1) * 20);
      const y0 = Math.round(cy * 15),  y1 = Math.round((cy + 1) * 15);
      let sum = 0, count = 0;
      for (let y = y0; y < y1; y++) {
        for (let x = x0; x < x1; x++) {
          const i = (y * W + x) * 4;
          sum += (0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2]) / 255;
          count++;
        }
      }
      features[cy * 8 + cx] = sum / count;
    }
  }
  return features;
}

export function averageFeatures(frames) {
  const result = new Array(64).fill(0);
  for (const f of frames) {
    for (let i = 0; i < 64; i++) result[i] += f[i];
  }
  return result.map(v => v / frames.length);
}
