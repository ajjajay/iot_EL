# Slide Deck Context Pack — Iris Biometric Access Control System

> **How to use this file:** Upload or paste this entire file into Claude on **claude.ai**
> (with the **Canva connector** enabled), or into the **Claude for Chrome** extension while
> Canva is open. Then say: *"Build this as a Canva presentation using the design direction
> and the slide-by-slide content below."*
>
> It contains everything needed: design system, per-slide copy, and speaker notes.
>
> **Also upload these 3 images** (in `deck_assets/`): `auth.png`, `alerts.png`, `hardware.png` —
> the slides marked "USE SCREENSHOT" tell Canva where each goes.

---

## 1. Design Direction (give this to Canva)

- **Mood:** modern, dark, high-tech, "edge-AI / cybersecurity product launch."
- **Theme:** dark mode. Background deep navy `#0B0F1A`; cards/panels `#141B2E`.
- **Accent palette:**
  - Teal `#35D0BA` (primary / edge)
  - Violet `#7C5CFF` (cloud)
  - Pink `#FF6B9D` (alerts / AWS)
  - Gold `#FFC85C` (highlights / roadmap)
- **Text:** near-white `#F2F5FA` for headings; muted slate `#9AA6BF` for body.
- **Fonts:** clean geometric sans for headings (e.g. *Poppins / Montserrat / Space Grotesk*);
  monospace for code & topic strings (e.g. *JetBrains Mono / Fira Code*).
- **Components:** rounded cards with a thin colored accent bar on the left edge; small pill
  "chips" for tags; numbered step circles; subtle radial glows behind the title/closing slides.
- **Aspect ratio:** 16:9.
- **Iconography:** line icons — eye/iris, microchip, cloud, shield, lock, bell, chart.

---

## 2. Slide-by-Slide Content

### Slide 1 — Title
- **Eyebrow chip:** IoT · EDGE AI · BIOMETRICS
- **Title:** Iris Biometric Access Control System
- **Subtitle:** A production-grade edge-to-cloud security platform — on-device iris matching,
  anomaly detection, and an AI agent alert loop across ESP32, Firebase & AWS.
- **Footer stat cards (4):**
  - ESP32-CAM — Edge inference
  - TinyML — Ambient risk
  - AWS Bedrock — AI alert agent
  - Firebase — Realtime sync
- *Notes:* Open with the one-liner: an access-control system where the matching happens
  on the device, not the cloud — privacy + low latency, with cloud only for alerting & ops.

### Slide 2 — System Overview (3 layers)
- **Title:** Three layers, one secure access loop
- **Card EDGE (teal) — ESP32-CAM Firmware:** captures iris → 64-element descriptor; on-device
  matching vs SPIFFS templates; sliding-window anomaly scorer; relay/LED actuation + TinyML monitor.
- **Card CLOUD (violet) — Firebase + AWS:** Firebase RTDB for state/sign-ins/users; AWS IoT Core
  routes alerts via Rules; Lambda → Bedrock Agent → SNS; ACK published back to device.
- **Card FRONTEND (pink) — Web Dashboard:** live sign-in logs & enrolled users; security alerts +
  ambient charts; enrollment command panel; manual relay override (5-min auto-expiry).
- *Notes:* Stress the separation of concerns — edge does the security-critical work; cloud does
  notification & fleet ops; dashboard is the human control surface.

### Slide 3 — Why Iris? (advantages, user perspective)
- **Title:** What you actually get out of it
- **6 advantage cards:**
  - **Privacy by design** — your iris template never leaves the device; no central face database to breach.
  - **You're never locked out** — recognition fails? Fall back to a PIN, then a one-time emailed QR code.
  - **It sees attacks, not just logins** — spoof & brute-force flagged in real time; a replayed photo hits 100% anomaly.
  - **AI triages the noise** — a Bedrock agent decides which anomalies deserve a human, instead of 50 raw alerts/day.
  - **Cheap, off-the-shelf hardware** — ~$10 ESP32-CAM + common modules, not a proprietary $500 reader.
  - **Every attempt is on the record** — full audit trail: who, when, match score, deny reason, anomaly %.
- *Notes:* This is the "why buy it" slide — lead with privacy + resilience + cost.

### Slide 4 — Who it's for / user journey
- **Title:** One door, three ways in — no one gets stranded
- **3-step journey:** (1) Look at the camera — iris matched on-device in <1 s, score <0.30 → unlock.
  (2) Lighting bad? Press * and enter a 4-digit PIN. (3) Visitor/emergency? Generate a one-time QR
  code emailed to an authorised recipient who shows it on their phone.
- **Built for:** Offices & co-working (no badges to lose) · Labs & server rooms (audit trails) ·
  Small business / home (affordable + attack detection) · Multi-site fleets (one binary, central dashboard).

### Slide — Architecture (flow diagram)
- **Title:** Edge-to-cloud data flow
- **Flow:** ESP32-CAM Node —(HTTPS / MQTT-TLS 8883)→ { Firebase RTDB } and { AWS IoT Core }.
  AWS IoT Core → Lambda → Bedrock Agent → SNS → User Notification → ACK back to device.
  Firebase RTDB ↔ Web Dashboard (WebSocket).
- **MQTT Topics box:**
  - `iot/{thing}/biometric/signin`
  - `iot/{thing}/biometric/alert`
  - `iot/{thing}/ai/alerts`  (Bedrock ACK)
- *Notes:* The AI alert loop is the differentiator: an anomaly doesn't just log — it routes to a
  Bedrock agent that decides + notifies a human, then confirms back to the device.

### Slide 4 — Biometric Pipeline
- **Title:** From photons to a pass / fail decision
- **4 numbered steps:**
  1. **Capture** — OV2640 grayscale frame, 160×120, JPEG-decoded.
  2. **Extract** — 8×8 zonal grid → mean intensity per cell → float[64].
  3. **Match** — normalised RMS distance vs ≤5 templates/user; closest below threshold wins.
  4. **Decide** — score < 0.30 → PASS; else REJECTED.
- **Side panel "Why 8×8 zonal?":** zero external dependencies; fits on-chip SRAM; no segmentation
  needed. Production path: Gabor wavelets or a CNN embedding (AWS Rekognition).
- *Notes:* Be honest that it's a proof-of-concept descriptor tuned for consistent lighting; the
  architecture is built so the matcher can be swapped for a CNN without touching the rest.

### Slide 5 — Anomaly Detection
- **Title:** Catching brute-force & spoofing in real time
- **Formula (mono, hero):**
  `score = 0.40 × failure_rate + 0.35 × score_proximity + 0.25 × frequency_spike`
- **3 component cards:**
  - **0.40 failure_rate** — fraction of failed attempts in the 20-event window.
  - **0.35 score_proximity** — a match just below threshold ⇒ likely partial replay (spoof signal).
  - **0.25 frequency_spike** — > 5 events in 60 s → 1.0.
- **Footer:** exceed `anomalyScoreThreshold` (0.60) → AlertManager fires to AWS IoT + Firebase,
  with 30 s duplicate suppression.
- *Notes:* score_proximity is the clever bit — a replayed/partial iris tends to land *just* under
  threshold, which is itself suspicious.

### Slide — The Dashboard (USE 2 SCREENSHOTS)
- **Title:** See every sign-in and threat, live
- **Images:** place `deck_assets/auth.png` (left) and `deck_assets/alerts.png` (right) in rounded
  framed cards with captions: "Auth & QR fallback" and "Security alerts — spoof attempts at 100% anomaly".
- **Footer:** spoof attempts, brute-force and granted/denied results stream in real time; each row
  carries match score, deny reason and an anomaly percentage.

### Slide — Firmware State Machine
- **Title:** Ten states, one legal transition table
- **States:** INIT, CONNECTING, READY, MONITORING, ENROLLING, AUTHENTICATING, AUTHENTICATED,
  REJECTED, ALERT, ERROR.
- **Actuator legend:** AUTHENTICATED → relay ON, LED solid. REJECTED → 3 fast flashes.
  ALERT → relay ON + fast blink. ERROR → SOS pattern, then restart.
- *Notes:* Every transition is validated against a legal-transition table and logged to serial
  with elapsed time — makes field debugging tractable.

### Slide 7 — TinyML Ambient Monitor
- **Title:** A second pair of eyes on the environment
- **Model card:** Input(3) → Dense(16, ReLU) → Dense(8, ReLU) → Softmax(3). ~700 params,
  ~4.2 KB TFLite, ~8 KB tensor arena. Inputs: temperature, humidity, light. Output:
  normal / warning / critical.
- **Pipeline card:** mock_data_generator.py (5000 samples) → model_training.py (Keras→TFLite)
  → model_converter.py (C hex array) → firmware GetModel()→Invoke().
- **Footer:** `riskScore ≥ mlRiskThreshold` triggers ALERT — secondary to biometrics, runs in the
  background loop.
- *Notes:* Shows the system isn't single-purpose — same node also does environmental risk inference.

### Slide — The Hardware (USE SCREENSHOT)
- **Title:** Off-the-shelf parts, one custom enclosure
- **Image:** place `deck_assets/hardware.png` (left, framed) — caption "Prototype node — keypad, LCD,
  gas & DHT sensors".
- **Component list (right):** ESP32-CAM (brains + iris camera) · 4×4 keypad (PIN & QR unlock) ·
  16×2 LCD (live status) · DHT + gas sensor (feeds ambient TinyML) · Relay (door lock, 5-min override).

### Slide — Scaling Strategy
- **Title:** From 2 to 20+ devices — no schema changes
- **6 cards:**
  - Single binary, per-device config — flash same firmware, only config.json changes.
  - Auto-discovery — dashboard `onValue('/devices')` picks up new nodes instantly.
  - Read-cost control — per-device listeners + `.limitToLast(50)` on sign-ins.
  - Template backup — optional blob upload to Firebase Storage; restore after SPIFFS format.
  - OTA updates — ArduinoOTA via `/devices/{id}/commands/otaUrl`; templates survive.
  - Cloud Functions — server-side aggregation offloads logic from devices.

### Slide 9 — Roadmap & Close
- **Title:** Where it goes next
- **Roadmap cards:**
  - Liveness detection — blink challenge defeats photo replay.
  - Daugman segmentation — greatly improves FAR/FRR.
  - CNN embeddings — production-grade matching via Rekognition.
  - Multi-factor (iris + PIN) — higher security for sensitive areas.
- **Closing block:** "Thank you." · Iris Biometric Access Control · github.com/ajjajay/iot_EL

---

## 3. One-paragraph project summary (for the AI to ground itself)

A production-grade IoT biometric access control system in three layers. **Edge:** ESP32-CAM
firmware captures iris images via an OV2640, extracts a 64-element zonal feature vector, matches
against templates stored in SPIFFS using normalised RMS distance, and runs a sliding-window
anomaly detector for brute-force/spoofing. It controls a relay (door lock) + status LED and runs
an ambient TinyML environmental-risk model as a secondary monitor. **Cloud:** Firebase Realtime
Database stores device state, sign-in history, enrolled users and alerts; AWS IoT Core routes
biometric alerts via IoT Rules → Lambda → AWS Bedrock Agent → SNS, then publishes an
acknowledgement back to the device. **Frontend:** a web dashboard shows live sign-in logs,
enrolled users, security alerts and ambient sensor charts, sends enrollment commands, and offers
a manual relay override with 5-minute auto-expiry.
