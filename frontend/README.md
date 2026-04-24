# Frontend — Real-time Dashboard

## Features

- Live sensor charts (temperature, humidity, light, risk score)
- Device status cards with FSM state badges
- Alert timeline with severity colouring
- Manual relay/fan toggle (with 5-minute override expiry)
- Device health table (uptime, free heap, firmware version)
- Dark / light mode toggle
- Mobile-responsive layout
- Demo mode (runs without Firebase — shows simulated data)

## Quick Start

### Option A — Open directly (no server needed)

1. Edit `app.js` — replace `FIREBASE_CONFIG` with your project values
2. Open `index.html` in a browser

> Without Firebase config, the dashboard runs in **demo mode** automatically
> (simulated sensor data with 3-second updates).

### Option B — Serve locally

```bash
# Python 3
cd frontend/
python -m http.server 8080
# Then open http://localhost:8080
```

### Option C — Deploy to Firebase Hosting

```bash
npm install -g firebase-tools
firebase login
firebase init hosting   # select frontend/ as public dir
firebase deploy
```

## Configuring Firebase

In `app.js`, find and replace:
```js
const FIREBASE_CONFIG = {
  apiKey:            "REPLACE_WITH_YOUR_API_KEY",
  authDomain:        "REPLACE...",
  // ...
};
const DASH_EMAIL    = "dashboard@yourdomain.com";
const DASH_PASSWORD = "YourDashboardPassword";
```

Create the dashboard user in Firebase Console → Authentication → Users.

## Layout Structure

```
index.html
├── <nav>         Navbar with global status badge
├── .summary-strip  Quick-glance metrics
├── .device-grid    Per-device cards (auto-populated)
├── .chart-grid     4 real-time Chart.js graphs
├── .alert-timeline Alert events
├── .controls-grid  Manual relay toggles
└── .health-table   Device uptime / heap / firmware
```
