"""
main.py — IoT Biometric Access Control · Backend API

Endpoints:
  GET  /health                        — liveness probe
  GET  /api/predict                   — ML inference (temp + humidity + smoke)
  GET  /api/devices                   — all devices from Firebase RTDB
  GET  /api/devices/{device_id}       — single device state + latest reading
  GET  /api/alerts/{device_id}        — last N alerts for a device
  GET  /api/signins/{device_id}       — last N sign-in events for a device
  GET  /api/users                     — all enrolled users
  POST /api/voice-update              — AI spoken security briefing
"""

import os
from contextlib import asynccontextmanager

from dotenv import load_dotenv
load_dotenv()

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from ml_model import AmbientRiskModel
from routers import devices, alerts, signins, users, voice, predict

# ── Startup ───────────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    model = AmbientRiskModel()
    print("[STARTUP] ML model loaded:", model.backend)
    app.state.model = model
    yield
    print("[SHUTDOWN] Cleaning up")

# ── App ───────────────────────────────────────────────────────────────────────

app = FastAPI(
    title="IoT Biometric Access Control — Backend API",
    version="1.0.0",
    lifespan=lifespan,
)

# ── CORS ──────────────────────────────────────────────────────────────────────

ALLOWED_ORIGINS = [
    o.strip()
    for o in os.getenv(
        "ALLOWED_ORIGINS",
        "https://aditya-sridhar-git-iot2.vercel.app,http://localhost:5173,https://localhost:5173,http://localhost:3000",
    ).split(",")
    if o.strip()
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Routers (all under /api) ──────────────────────────────────────────────────

app.include_router(predict.router, prefix="/api")
app.include_router(devices.router, prefix="/api")
app.include_router(alerts.router,  prefix="/api")
app.include_router(signins.router, prefix="/api")
app.include_router(users.router,   prefix="/api")
app.include_router(voice.router,   prefix="/api")

# ── Health ────────────────────────────────────────────────────────────────────

@app.get("/health", tags=["infra"])
def health():
    return {
        "status": "ok",
        "model_backend": app.state.model.backend if hasattr(app.state, "model") else "unknown",
    }

@app.get("/", tags=["infra"])
def root():
    return {"service": "IoT Biometric Backend", "docs": "/docs", "health": "/health"}
