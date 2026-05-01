import { useState, useEffect } from 'react';
import { ref, onValue } from 'firebase/database';

export function useDevices(db) {
  const [devices, setDevices] = useState({});
  useEffect(() => {
    if (!db) return;
    const r    = ref(db, '/devices');
    const unsub = onValue(r, snap => setDevices(snap.val() ?? {}));
    return unsub;
  }, [db]);
  return devices;
}
