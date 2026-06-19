import { initializeApp } from 'firebase/app';
import { getDatabase } from 'firebase/database';
import { getAuth } from 'firebase/auth';

export const FIREBASE_CONFIG = {
  apiKey:            import.meta.env.VITE_FIREBASE_API_KEY            ?? '',
  authDomain:        import.meta.env.VITE_FIREBASE_AUTH_DOMAIN        ?? '',
  databaseURL:       import.meta.env.VITE_FIREBASE_DATABASE_URL       ?? '',
  projectId:         import.meta.env.VITE_FIREBASE_PROJECT_ID         ?? '',
  storageBucket:     import.meta.env.VITE_FIREBASE_STORAGE_BUCKET     ?? '',
  messagingSenderId: import.meta.env.VITE_FIREBASE_MESSAGING_SENDER_ID ?? '',
  appId:             import.meta.env.VITE_FIREBASE_APP_ID              ?? '',
  measurementId:     import.meta.env.VITE_FIREBASE_MEASUREMENT_ID     ?? '',
};

export const DASH_EMAIL    = import.meta.env.VITE_DASH_EMAIL    ?? '';
export const DASH_PASSWORD = import.meta.env.VITE_DASH_PASSWORD ?? '';

let _db = null;
let _auth = null;

try {
  if (FIREBASE_CONFIG.databaseURL && FIREBASE_CONFIG.apiKey) {
    const app = initializeApp(FIREBASE_CONFIG);
    _db   = getDatabase(app);
    _auth = getAuth(app);
  }
} catch (e) {
  console.warn('[Firebase] Init skipped — running in demo mode:', e.message);
}

export const db   = _db;
export const auth = _auth;
