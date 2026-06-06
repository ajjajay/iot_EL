import { useState, useEffect, useRef } from 'react';

const BACKEND_URL = import.meta.env.VITE_API_URL ?? '';

/**
 * Connects to GET /api/stream (SSE) on the backend.
 * Returns { devices, readings, connected } updated every ~2 s.
 *
 * devices  — { [deviceId]: { online, latest, heartbeat } }
 * readings — { [deviceId]: reading[] }  (last 200, newest last)
 * connected — boolean: true once the first event arrives
 */
export function useBackendStream() {
  const [devices,   setDevices]   = useState({});
  const [readings,  setReadings]  = useState({});
  const [connected, setConnected] = useState(false);
  const esRef = useRef(null);

  useEffect(() => {
    const es = new EventSource(`${BACKEND_URL}/api/stream`);
    esRef.current = es;

    es.onmessage = (event) => {
      try {
        const payload = JSON.parse(event.data);
        if (!Array.isArray(payload)) return;

        const devMap  = {};
        const readMap = {};

        payload.forEach(device => {
          const id = device.deviceId;
          devMap[id] = {
            online:    device.online,
            latest:    device.latest    ?? {},
            heartbeat: device.heartbeat ?? {},
          };
          if (Array.isArray(device.readings) && device.readings.length) {
            readMap[id] = device.readings;
          }
        });

        setDevices(devMap);
        setReadings(prev => {
          const next = { ...prev };
          Object.entries(readMap).forEach(([id, incoming]) => {
            const merged = [...(prev[id] ?? []), ...incoming];
            // deduplicate by ts, keep newest 200
            const seen  = new Set();
            const dedup = merged.filter(r => {
              const key = r.ts ?? JSON.stringify(r);
              if (seen.has(key)) return false;
              seen.add(key);
              return true;
            });
            next[id] = dedup.slice(-200);
          });
          return next;
        });
        setConnected(true);
      } catch (_) {}
    };

    es.onerror = () => {
      setConnected(false);
      // EventSource auto-reconnects; no manual retry needed
    };

    return () => {
      es.close();
    };
  }, []);

  return { devices, readings, connected };
}
