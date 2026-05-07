# Frontend — Real-time Dashboard

## Tech Stack

| Technology | Version | Purpose |
|---|---|---|
| HTML5 / CSS3 | — | Layout and styling |
| JavaScript (ES6+) | — | Firebase integration, UI logic |
| Chart.js | 4.x (CDN) | Real-time sensor charts |
| Firebase JS SDK | 9.x (CDN) | Realtime Database WebSocket listener |

No build step required — open `index.html` directly in a browser.

## Features

- Live sensor charts (temperature, humidity, light, risk score) via Firebase WebSocket
- Device status cards auto-populated from `/devices/` — works with any mix of ESP32 and RPi nodes
- Sign-in log with matched user, match score, and anomaly score
- Enrolled users table pulled from `/users/`
- Security alert timeline with severity colouring
- Manual relay toggle with 5-minute auto-expiry indicator
- Device health table (uptime, FSM state, firmware version)
- Dark / light mode toggle
- Mobile-responsive layout
- **Demo mode** — runs entirely without Firebase, showing simulated data

## Quick Start

### Option A — Open directly (no server needed)

1. Edit `app.js` — replace `FIREBASE_CONFIG` with your project values and set `DASH_EMAIL` / `DASH_PASSWORD`
2. Open `index.html` in Chrome or Firefox

Without real Firebase credentials the dashboard automatically enters **demo mode** with simulated biometric events and sensor data updating every 3 seconds.

### Option B — Serve locally

```bash
cd frontend
python -m http.server 8080
# Open http://localhost:8080
```

### Option C — Deploy to Firebase Hosting

```bash
npm install -g firebase-tools
firebase login
firebase init hosting
# When prompted, set public directory to: frontend
firebase deploy
```

## Configuring Firebase

In `app.js`, find and replace the config block:

```js
const FIREBASE_CONFIG = {
  apiKey:            "YOUR_API_KEY",
  authDomain:        "YOUR_PROJECT.firebaseapp.com",
  databaseURL:       "https://YOUR_PROJECT-default-rtdb.firebaseio.com",
  projectId:         "YOUR_PROJECT",
  storageBucket:     "YOUR_PROJECT.appspot.com",
  messagingSenderId: "YOUR_SENDER_ID",
  appId:             "YOUR_APP_ID",
};
const DASH_EMAIL    = "dashboard@yourdomain.com";
const DASH_PASSWORD = "YourDashboardPassword";
```

Create the dashboard user in Firebase Console → Authentication → Users.

## Enrolling a User from the Dashboard

1. Open **Enrolled Users** section → click **Enroll New User**
2. Select the target device from the dropdown
3. Enter a User ID (alphanumeric, no spaces) and Full Name
4. Click **Send Enrollment Command**
5. The device enters ENROLLING state — the user looks at the camera for ~1 second
6. On success, the enrolled user appears in the table within a few seconds

## Layout Structure

```
index.html
├── <nav>             Global status badge (devices online / offline)
├── .summary-strip    Quick-glance: total devices, active alerts, last sign-in
├── .device-grid      Per-device cards (auto-populated, works for ESP32 + RPi)
├── .chart-grid       4 real-time Chart.js graphs (temp, humidity, light, risk)
├── .signin-log       Biometric sign-in history with match scores
├── .alert-timeline   Security alert events with anomaly scores
├── .controls-grid    Manual relay toggles with expiry countdown
└── .health-table     Device uptime / state / firmware
```
