import { initializeApp } from 'firebase/app';
import { getDatabase } from 'firebase/database';
import { getAuth } from 'firebase/auth';

export const FIREBASE_CONFIG = {
  apiKey:            "AIzaSyBsbx7C15g-Ws6yIYtNo3zd7vU5geQwS8g",
  authDomain:        "iot-fc8b3.firebaseapp.com",
  databaseURL:       "https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId:         "iot-fc8b3",
  storageBucket:     "iot-fc8b3.firebasestorage.app",
  messagingSenderId: "764355810285",
  appId:             "1:764355810285:web:5301eb89107e45a5090de4",
  measurementId:     "G-MG3PHYQ22V",
};

export const DASH_EMAIL    = "dashboard@yourdomain.com";
export const DASH_PASSWORD = "YourDashboardPassword";

const app  = initializeApp(FIREBASE_CONFIG);
export const db   = getDatabase(app);
export const auth = getAuth(app);
