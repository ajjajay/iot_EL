"""
routers/predict.py — ML inference endpoint
POST /api/predict  →  { risk_score, label, label_int, backend }

Inputs (all from the ESP32 / live Firebase readings):
  temperature_c  — degrees Celsius        (range -10 to 60)
  humidity_pct   — relative humidity %    (range 0 to 100)
  light_norm     — normalised smoke value (range 0 to 1, = smokePct / 100)
"""

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

router = APIRouter()


class SensorReading(BaseModel):
    temperature_c: float = Field(..., ge=-10, le=60,  example=28.5)
    humidity_pct:  float = Field(..., ge=0,   le=100, example=65.0)
    light_norm:    float = Field(..., ge=0,   le=1,   example=0.15,
                                 description="Normalised smoke (smokePct / 100) — replaces LDR")


class PredictResponse(BaseModel):
    risk_score: float = Field(..., description="Composite risk score 0.0–1.0")
    label:      str   = Field(..., description="normal | warning | critical")
    label_int:  int   = Field(..., description="0=normal 1=warning 2=critical")
    backend:    str   = Field(..., description="tflite | threshold_rules")


@router.post("/predict", response_model=PredictResponse, tags=["inference"])
def predict(reading: SensorReading, request: Request):
    """
    Run ambient environmental risk inference using the loaded ML model.
    Falls back to threshold rules if no trained .tflite file is present.

    Input mapping from dashboard:
      temperature_c = devices[id].latest.temperatureC
      humidity_pct  = devices[id].latest.humidityPct
      light_norm    = devices[id].latest.smokePct / 100
    """
    model = request.app.state.model
    result = model.predict(
        reading.temperature_c,
        reading.humidity_pct,
        reading.light_norm,
    )
    return result
