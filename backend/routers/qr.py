"""
routers/qr.py — QR Code fallback authentication
=================================================
POST /api/qr/request  — generate a one-time token, email QR + return base64 PNG
POST /api/qr/verify   — verify the token and write Access Granted to Firebase
"""

import base64
import io
import os
import smtplib
import time
import uuid
from email.mime.image     import MIMEImage
from email.mime.multipart import MIMEMultipart
from email.mime.text      import MIMEText

import qrcode
from fastapi  import APIRouter
from pydantic import BaseModel

from firebase_client import get_ref

router = APIRouter()

_GMAIL_USER   = os.getenv("GMAIL_USER",         "ajaygirish23@gmail.com")
_GMAIL_PASS   = os.getenv("GMAIL_APP_PASSWORD",  "")
_NOTIFY_EMAIL = os.getenv("QR_NOTIFY_EMAIL",     "ajaygirish23@gmail.com")
_TOKEN_TTL    = int(os.getenv("QR_TOKEN_TTL_SECS", "300"))


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

def _send_email(token: str, device_id: str, qr_b64: str) -> None:
    qr_png = base64.b64decode(qr_b64)
    msg = MIMEMultipart("related")
    msg["Subject"] = f"[IoT Access] QR Code — {device_id}"
    msg["From"]    = _GMAIL_USER
    msg["To"]      = _NOTIFY_EMAIL
    msg.attach(MIMEText(f"""
    <html><body style="font-family:sans-serif;max-width:480px;margin:auto">
      <h2 style="color:#1a1a2e">IoT Access — QR Code</h2>
      <p>Fallback access requested for device <b>{device_id}</b>.</p>
      <p style="font-size:2em;letter-spacing:4px;font-weight:bold;color:#0f3460">{token}</p>
      <img src="cid:qrcode" width="200" height="200" style="display:block;margin:16px 0"/>
      <p style="color:#888;font-size:0.85em">Expires in 5 minutes. One-time use only.</p>
    </body></html>""", "html"))
    img = MIMEImage(qr_png, name="qr.png")
    img.add_header("Content-ID",          "<qrcode>")
    img.add_header("Content-Disposition", "inline", filename="qr.png")
    msg.attach(img)
    with smtplib.SMTP_SSL("smtp.gmail.com", 465) as smtp:
        smtp.login(_GMAIL_USER, _GMAIL_PASS)
        smtp.sendmail(_GMAIL_USER, _NOTIFY_EMAIL, msg.as_string())


# ── Endpoints ─────────────────────────────────────────────────────────────────

@router.post("/qr/request")
def qr_request(body: QrRequestBody):
    """
    Generate a one-time QR token, store it in Firebase, email it via Gmail
    (if GMAIL_APP_PASSWORD is set), and always return the QR as base64 PNG
    so the dashboard can display it regardless of email status.
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

    qr_b64     = _make_qr_b64(token)
    email_sent = False
    if _GMAIL_PASS:
        try:
            _send_email(token, body.deviceId, qr_b64)
            email_sent = True
            print(f"[QR] Token {token} emailed for device={body.deviceId}")
        except Exception as e:
            print(f"[QR] Email failed: {e}")
    else:
        print(f"[QR] GMAIL_APP_PASSWORD not set — skipping email, token={token}")

    return {
        "status":    "ok",
        "token":     token,
        "qrBase64":  qr_b64,
        "emailSent": email_sent,
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
