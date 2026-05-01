import { useState, useEffect } from 'react';
import { ref, query, limitToLast, onChildAdded } from 'firebase/database';

const MAX = 50;

export function useSignins(db) {
  const [signins, setSignins] = useState({});

  useEffect(() => {
    if (!db) return;
    const cleanups   = [];
    const subscribed = new Set();

    const devUnsub = onChildAdded(ref(db, '/devices'), snap => {
      const id = snap.key;
      if (subscribed.has(id)) return;
      subscribed.add(id);

      const q = query(ref(db, `/signins/${id}`), limitToLast(MAX));
      const sUnsub = onChildAdded(q, sSnap => {
        const ev = { ...sSnap.val(), deviceId: id };
        setSignins(prev => {
          const arr = [ev, ...(prev[id] || [])];
          return { ...prev, [id]: arr.length > MAX ? arr.slice(0, MAX) : arr };
        });
      });
      cleanups.push(sUnsub);
    });

    return () => {
      devUnsub();
      cleanups.forEach(fn => fn());
    };
  }, [db]);

  return signins;
}
