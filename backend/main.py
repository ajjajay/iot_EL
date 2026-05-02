"""
main.py — IoT Biometric Access Control · Backend API
=====================================================
FastAPI service deployed on Render via Docker.

Endpoints:
  GET  /health                        — liveness probe (Render uses this)
  POST /predict                       — ambient sensor ML inference
  GET  /api/devices                   — all devices from Firebase RTDB
  GET  /api/devices/{device_id}       — single device state + latest reading
  GET  /api/alerts/{device_id}        — last N alerts for a device
  GET  /api/signins/{device_id}       — last N sign-in events for a device
  GET  /api/users                     — all enrolled users
  GET  /api/summary                   — fleet-level summary stats
"""

import os
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from ml_model import AmbientRiskModel
from routers import devices, alerts, signins, users

# ── Startup / shutdown ────────────────────────────────────────────────────────

model: AmbientRiskModel | None = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global model
    model = AmbientRiskModel()
    print("[STARTUP] ML model loaded:", model.backend)
    # Store on app state so routers can access if needed
    app.state.model = model
    yield
    print("[SHUTDOWN] Cleaning up")


# ── App ────────────────────────────────────────────────────────────────────────

app = FastAPI(
    title="IoT Biometric Access Control — Backend API",
    version="1.0.0",
    description=(
        "REST API for the IoT biometric access control system. "
        "Provides ML inference, Firebase data aggregation, and device management."
    ),
    lifespan=lifespan,
)

# ── CORS ───────────────────────────────────────────────────────────────────────
# Allow the Vercel frontend + localhost dev server

ALLOWED_ORIGINS = [
    origin.strip()
    for origin in os.getenv(
        "ALLOWED_ORIGINS",
        "https://aditya-sridhar-git-iot2.vercel.app,http://localhost:5173,http://localhost:3000",
    ).split(",")
    if origin.strip()
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Routers ────────────────────────────────────────────────────────────────────

app.include_router(devices.router, prefix="/api")
app.include_router(alerts.router,  prefix="/api")
app.include_router(signins.router, prefix="/api")
app.include_router(users.router,   prefix="/api")


# ── Health ─────────────────────────────────────────────────────────────────────

@app.get("/health", tags=["infra"])
def health():
    """Render liveness probe — must return 200 for the deploy to be considered healthy."""
    return {
        "status": "ok",
        "model_backend": app.state.model.backend if hasattr(app.state, "model") else "unknown",
    }


# ── Predict ────────────────────────────────────────────────────────────────────

from pydantic import BaseModel, Field


class SensorReading(BaseModel):
    temperature_c: float = Field(..., ge=-10, le=60,  example=28.5,  description="Temperature in Celsius")
    humidity_pct:  float = Field(..., ge=0,   le=100, example=65.0,  description="Relative humidity %")
    light_norm:    float = Field(..., ge=0,   le=1,   example=0.45,  description="Normalised light (0–1)")


class PredictResponse(BaseModel):
    risk_score: float = Field(..., description="Composite risk score in [0.0, 1.0]")
    label:      str   = Field(..., description="normal | warning | critical")
    label_int:  int   = Field(..., description="0=normal, 1=warning, 2=critical")
    backend:    str   = Field(..., description="tflite | threshold_rules")


@app.post("/predict", response_model=PredictResponse, tags=["inference"])
def predict(reading: SensorReading):
    """
    Run ambient environmental risk inference.

    Mirrors the logic in the ESP32's MLInference.h:
    - If a trained model.tflite exists in the container it is used.
    - Otherwise falls back to the same threshold rules that labelled the training data.
    """
    result = app.state.model.predict(
        reading.temperature_c,
        reading.humidity_pct,
        reading.light_norm,
    )
    return result


# ── Root ───────────────────────────────────────────────────────────────────────

@app.get("/", tags=["infra"])
def root():
    return {
        "service": "IoT Biometric Backend",
        "docs":    "/docs",
        "health":  "/health",
    }
