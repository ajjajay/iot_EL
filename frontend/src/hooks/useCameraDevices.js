import { useState, useEffect } from 'react';

export const PHONE_CAM_ID  = '__phone_mjpeg__';
const        PHONE_CAM_URL = '/phone-cam/video';

async function openMjpegAsMediaStream() {
  const canvas = document.createElement('canvas');
  canvas.width  = 640;
  canvas.height = 480;
  const ctx = canvas.getContext('2d');
  const img = new Image();
  img.src = PHONE_CAM_URL;

  await new Promise((resolve, reject) => {
    img.onload = resolve;
    setTimeout(() => reject(new Error('Phone camera timed out — is DroidCam running on the phone?')), 6000);
  });

  if (img.naturalWidth)  canvas.width  = img.naturalWidth;
  if (img.naturalHeight) canvas.height = img.naturalHeight;

  let running = true;
  function draw() {
    if (!running) return;
    ctx.drawImage(img, 0, 0);
    requestAnimationFrame(draw);
  }
  draw();

  const stream = canvas.captureStream(30);
  // attach a stop hook so callers can clean up the draw loop
  stream._stopMjpeg = () => { running = false; img.src = ''; };
  return stream;
}

export function useCameraDevices() {
  const [cameras,    setCameras]    = useState([]);
  const [selectedId, setSelectedId] = useState('');

  useEffect(() => {
    async function enumerate() {
      if (!navigator.mediaDevices?.enumerateDevices) return;
      try {
        const devices = await navigator.mediaDevices.enumerateDevices();
        const inputs  = devices.filter(d => d.kind === 'videoinput');
        // append the MJPEG phone option after real cameras
        const all = [...inputs, { deviceId: PHONE_CAM_ID, label: 'Phone Camera (DroidCam)' }];
        setCameras(all);
        setSelectedId(prev => {
          if (prev && all.some(d => d.deviceId === prev)) return prev;
          return all[0]?.deviceId ?? '';
        });
      } catch {}
    }
    enumerate();
    navigator.mediaDevices?.addEventListener('devicechange', enumerate);
    return () => navigator.mediaDevices?.removeEventListener('devicechange', enumerate);
  }, []);

  async function openStream() {
    if (selectedId === PHONE_CAM_ID) {
      return openMjpegAsMediaStream();
    }
    const constraints = selectedId
      ? { video: { deviceId: { exact: selectedId } }, audio: false }
      : { video: true, audio: false };
    return navigator.mediaDevices.getUserMedia(constraints);
  }

  return { cameras, selectedId, setSelectedId, openStream };
}
