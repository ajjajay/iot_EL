import { useState, useEffect } from 'react';
import { ref, query, limitToLast, onChildAdded } from 'firebase/database';

export function useReadings(db) {
  const [readings, setReadings] = useState({});

  useEffect(() => {
    if (!db) return;
    const cleanups    = [];
    const subscribed  = new Set();

    const devUnsub = onChildAdded(ref(db, '/devices'), snap => {
      const id = snap.key;
      if (subscribed.has(id)) return;
      subscribed.add(id);

      const q = query(ref(db, `/readings/${id}`), limitToLast(200));
      const rUnsub = onChildAdded(q, rSnap => {
        setReadings(prev => {
          const arr = [...(prev[id] || []), rSnap.val()];
          return { ...prev, [id]: arr.length > 200 ? arr.slice(-200) : arr };
        });
      });
      cleanups.push(rUnsub);
    });

    return () => {
      devUnsub();
      cleanups.forEach(fn => fn());
    };
  }, [db]);

  return readings;
}
