import { useState, useEffect } from 'react';
import { ref, onValue } from 'firebase/database';

export function useUsers(db) {
  const [users, setUsers] = useState({});
  useEffect(() => {
    if (!db) return;
    const r     = ref(db, '/users');
    const unsub = onValue(r, snap => setUsers(snap.val() ?? {}));
    return unsub;
  }, [db]);
  return users;
}
