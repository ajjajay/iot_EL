# Firebase Setup

## What Firebase Provides

| Feature | Used for |
|---|---|
| Realtime Database | Device state, sign-in history, enrollment registry, security alerts |
| Email/Password Auth | One account per device node (ESP32 or RPi) + one dashboard account |
| Hosting (optional) | Static dashboard deployment |

## Step-by-step Setup

### 1. Create Firebase Project

1. Go to [console.firebase.google.com](https://console.firebase.google.com)
2. **Add project** → give it a name (e.g. `iris-access-control`)
3. Disable Google Analytics (not needed for IoT)

### 2. Enable Realtime Database

1. Firebase Console → **Build → Realtime Database → Create Database**
2. Choose the region closest to your deployment
3. Start in **test mode** (you will apply proper rules in the next step)

### 3. Enable Authentication

1. Firebase Console → **Build → Authentication → Get started**
2. Enable **Email/Password** sign-in provider
3. Create one account per device node:
   - `esp32-node-01@yourdomain.com` / `<strong-password>` (for ESP32)
   - `rpi-node-01@yourdomain.com` / `<strong-password>` (for RPi)
   - `dashboard@yourdomain.com` / `<strong-password>` (for web dashboard)

### 4. Apply Database Rules

**Development** (while building and testing):

Paste the contents of `dev.rules` into Firebase Console → Realtime Database → Rules.

**Production** (before going live):

```bash
npm install -g firebase-tools
firebase login
firebase init database   # select your project, accept defaults

# Copy prod.rules content into database.rules.json, then:
firebase deploy --only database
```

### 5. Get Your Config Keys

Firebase Console → **Project settings (gear icon) → General → Your apps**

Click **Add app → Web (`</>`)** to get a config object:

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

| File | Field(s) |
|---|---|
| `frontend/app.js` | `FIREBASE_CONFIG` object |
| `firmware/esp32_sensor_node/data/config.json` | `firebaseApiKey`, `firebaseUrl`, `firebaseEmail`, `firebasePass` |
| `firmware/rpi_sensor_node/data/config.json` | same fields |

### 6. (Optional) Deploy Dashboard to Firebase Hosting

```bash
npm install -g firebase-tools
firebase login
firebase init hosting
# Set public directory to: frontend
# Configure as single-page app: No
firebase deploy
```

---

## Database Schema

See `ARCHITECTURE.md` or `API_REFERENCE.md` for the full path reference. Key paths:

| Path | Purpose |
|---|---|
| `/devices/{id}/latest` | Most recent ambient reading (overwritten each cycle) |
| `/devices/{id}/heartbeat` | Device alive ping |
| `/devices/{id}/commands/relayOverride` | Dashboard → device relay control |
| `/devices/{id}/commands/enroll` | Dashboard → device enrollment trigger |
| `/readings/{id}/{pushId}` | Full ambient time-series (use `.limitToLast(50)` for charts) |
| `/signins/{id}/{pushId}` | Biometric authentication event log |
| `/users/{userId}` | Enrolled user registry |
| `/alerts/{id}/{pushId}` | Security alert events |

---

## Scaling to N Devices

No schema changes needed for additional devices. Each new ESP32 or RPi node:

1. Creates a Firebase Auth user (`esp32-node-02@...` or `rpi-node-02@...`)
2. Sets `deviceId` in its `config.json` (e.g., `esp32_node_02` or `rpi_node_02`)
3. Flashes/runs with the same firmware — it auto-registers under its ID

The dashboard auto-discovers all devices by listing `/devices/` keys.

For fleets > 20 devices, switch to per-device Firebase listeners and limit time-series reads with `.limitToLast(1)` on `/devices/{id}/latest` to control read costs.

---

## Security Rules Summary

`dev.rules` — authenticated read/write for all paths (development only).

`prod.rules` — enforces:
- Devices can only write to their own `/devices/{id}/`, `/signins/{id}/`, `/alerts/{id}/`, and `/readings/{id}/` paths (matched by `auth.uid`)
- Dashboard user can read all paths and write to `/devices/{id}/commands/`
- `/users/` is readable by all authenticated users, writable only by devices
