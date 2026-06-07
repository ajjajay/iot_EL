"""
routers/qr.py — QR Code fallback authentication
=================================================
POST /api/qr/request  — generate a one-time token, return QR as base64 PNG
POST /api/qr/verify   — verify the token and write Access Granted to Firebase
"""

import base64
import io
import os
import time
import uuid

import qrcode
from fastapi  import APIRouter
from pydantic import BaseModel

from firebase_client import get_ref

router = APIRouter()

_TOKEN_TTL = int(os.getenv("QR_TOKEN_TTL_SECS", "300"))   # 5 minutes


# ── Request body models ───────────────────────────────────────────────────────

class QrRequestBody(BaseModel):
    deviceId: str

class QrVerifyBody(BaseModel):
    token:    str
    deviceId: str


# ── Helpers ───────────────────────────────────────────────────────────────────

def _make_token() -> str:
    return uuid.uuid4().hex[:8].upper()

def _make_qr_b64(token: str) -> str:
    buf = io.BytesIO()
    qrcode.make(token).save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode()


# ── Endpoints ─────────────────────────────────────────────────────────────────

@router.post("/qr/request")
def qr_request(body: QrRequestBody):
    """
    Generate a one-time QR token, store it in Firebase, and return the QR
    image as a base64 PNG so the dashboard can display it directly.
    """
    token  = _make_token()
    expiry = int(time.time()) + _TOKEN_TTL

    get_ref(f"/qrTokens/{token}").set({
        "deviceId": body.deviceId,
        "expiry":   expiry,
        "used":     False,
        "ts":       int(time.time() * 1000),
    })

    get_ref(f"/devices/{body.deviceId}/commands/qrRequest").set({
        "pending": False,
        "ts":      int(time.time() * 1000),
    })

    print(f"[QR] Token {token} generated for device={body.deviceId}")
    return {
        "status":    "ok",
        "token":     token,
        "qrBase64":  _make_qr_b64(token),
        "expiresIn": _TOKEN_TTL,
    }


@router.post("/qr/verify")
def qr_verify(body: QrVerifyBody):
    """
    Verify the token. On success, write a signin record + set relayOverride ON.
    """
    token = body.token.strip().upper()
    ref   = get_ref(f"/qrTokens/{token}")
    data  = ref.get()

    if not data:
        return {"granted": False, "reason": "invalid_token"}

    if data.get("used"):
        return {"granted": False, "reason": "already_used"}

    if int(time.time()) > data.get("expiry", 0):
        return {"granted": False, "reason": "expired"}

    if data.get("deviceId") != body.deviceId:
        return {"granted": False, "reason": "device_mismatch"}

    # Consume the token
    ref.update({"used": True, "usedAt": int(time.time() * 1000)})

    # Grant access — same writes as a successful facial scan
    ts = int(time.time() * 1000)
    get_ref(f"/signins/{body.deviceId}").push({
        "userId":       "qr_auth",
        "userName":     "QR Code Auth",
        "deviceId":     body.deviceId,
        "matchScore":   0.0,
        "success":      True,
        "anomalyScore": 0.0,
        "source":       "qr_fallback",
        "ts":           ts,
    })
    get_ref(f"/devices/{body.deviceId}/commands/relayOverride").set("ON")

    print(f"[QR] Access granted via QR for device={body.deviceId}")
    return {"granted": True, "deviceId": body.deviceId}
