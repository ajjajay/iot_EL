import { useState, useEffect } from 'react';
import { ref, onValue, onChildAdded } from 'firebase/database';

export function useAlerts(db) {
  const [alerts, setAlerts] = useState([]);

  useEffect(() => {
    if (!db) return;
    const cleanups   = [];
    const subscribed = new Set();

    // Discover devices, then subscribe to each device's alerts subtree
    const devUnsub = onChildAdded(ref(db, '/devices'), snap => {
      const deviceId = snap.key;
      if (subscribed.has(deviceId)) return;
      subscribed.add(deviceId);

      const alertUnsub = onValue(ref(db, `/alerts/${deviceId}`), aSnap => {
        const deviceAlerts = aSnap.val() || {};
        const unacked = Object.values(deviceAlerts).filter(a => !a.acknowledged);
        setAlerts(prev => {
          const other = prev.filter(a => a.deviceId !== deviceId);
          return [...unacked, ...other].sort((a, b) => (b.ts || 0) - (a.ts || 0));
        });
      });
      cleanups.push(alertUnsub);
    });

    return () => {
      devUnsub();
      cleanups.forEach(fn => fn());
    };
  }, [db]);

  return [alerts, setAlerts];
}
