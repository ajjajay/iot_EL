# Firebase Setup

## Quick Start

### 1. Create Firebase Project

1. Go to [console.firebase.google.com](https://console.firebase.google.com)
2. **Add project** → give it a name (e.g. `iot-monitor`)
3. Disable Google Analytics (optional for IoT projects)

### 2. Enable Realtime Database

1. Firebase Console → **Build → Realtime Database → Create Database**
2. Choose region (pick closest to your location)
3. Start in **test mode** (you'll apply rules next)

### 3. Enable Authentication

1. Firebase Console → **Build → Authentication → Get started**
2. Enable **Email/Password** provider
3. Create device accounts:
   - `esp32_node_01@yourdomain.com` / `<strong-password>`
   - `esp32_node_02@yourdomain.com` / `<strong-password>`
   - `dashboard@yourdomain.com` / `<strong-password>` (for web app)

### 4. Apply Database Rules

**Development** (while building):
```bash
# Copy dev.rules into the Firebase console Rules editor
# Console → Realtime Database → Rules → paste content of dev.rules
```

**Production** (before deployment):
```bash
# Install Firebase CLI
npm install -g firebase-tools
firebase login
firebase init database    # select your project

# Copy prod.rules → database.rules.json, then:
firebase deploy --only database
```

### 5. Get Your Config Keys

Firebase Console → **Project settings (gear icon) → General → Your apps**

Click **Add app → Web (`</>`)** — you'll get a config object like:
```js
const firebaseConfig = {
  apiKey:            "AIzaSy...",
  authDomain:        "yourproject.firebaseapp.com",
  databaseURL:       "https://yourproject-default-rtdb.firebaseio.com",
  projectId:         "yourproject",
  storageBucket:     "yourproject.appspot.com",
  messagingSenderId: "123456789",
  appId:             "1:123456789:web:abc123"
};
```

Copy these values into:
- `frontend/app.js` → `FIREBASE_CONFIG` object
- `firmware/esp32_sensor_node/data/config.json` → relevant fields
- `.env` (for local scripts)

## Database Schema

See `schema.json` for the full structure. Key paths:

| Path | Purpose |
|---|---|
| `/devices/{id}/latest` | Most recent sensor reading (overwritten each cycle) |
| `/devices/{id}/heartbeat` | Device health ping |
| `/devices/{id}/commands/relayOverride` | Dashboard → device control |
| `/devices/{id}/config` | Remote threshold configuration |
| `/readings/{id}/{pushId}` | Full time-series log (use for charts) |
| `/alerts/{id}/{pushId}` | Alert events (for timeline in dashboard) |

## Scaling to N Devices

No schema changes needed — each new device just adds a new key under
`/devices/` and `/readings/`. Firebase handles fan-out automatically.
The dashboard auto-discovers devices by listing `/devices/` keys.
