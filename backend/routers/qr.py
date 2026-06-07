"""
routers/qr.py — QR Code fallback authentication
=================================================
POST /api/qr/request  — generate a one-time token, email it as a QR code
POST /api/qr/verify   — verify the token and write Access Granted to Firebase
"""

import os
import uuid
import time
import io
import smtplib
from email.mime.multipart import MIMEMultipart
from email.mime.text      import MIMEText
from email.mime.image     import MIMEImage

import qrcode
from fastapi   import APIRouter
from pydantic  import BaseModel

from firebase_client import get_ref

router = APIRouter()

_GMAIL_USER     = os.getenv("GMAIL_USER",         "ajaygirish23@gmail.com")
_GMAIL_PASS     = os.getenv("GMAIL_APP_PASSWORD",  "")
_NOTIFY_EMAIL   = os.getenv("QR_NOTIFY_EMAIL",     "ajaygirish23@gmail.com")
_TOKEN_TTL      = int(os.getenv("QR_TOKEN_TTL_SECS", "300"))   # 5 minutes


# ── Request body models ───────────────────────────────────────────────────────

class QrRequestBody(BaseModel):
    deviceId: str

class QrVerifyBody(BaseModel):
    token:    str
    deviceId: str


# ── Helpers ───────────────────────────────────────────────────────────────────

def _make_token() -> str:
    return uuid.uuid4().hex[:8].upper()   # e.g. "A3K9F2M7"

def _make_qr_png(token: str) -> bytes:
    img = qrcode.make(token)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()

def _send_email(token: str, device_id: str, qr_png: bytes) -> None:
    msg = MIMEMultipart("related")
    msg["Subject"] = f"[IoT Access] QR Code — {device_id}"
    msg["From"]    = _GMAIL_USER
    msg["To"]      = _NOTIFY_EMAIL

    html = f"""
    <html><body style="font-family:sans-serif;max-width:480px;margin:auto">
      <h2 style="color:#1a1a2e">IoT Access — QR Code</h2>
      <p>A QR fallback request was triggered for device <b>{device_id}</b>.</p>
      <p>Scan the QR code below in the dashboard, or type the code manually:</p>
      <p style="font-size:2em;letter-spacing:4px;font-weight:bold;color:#0f3460">{token}</p>
      <img src="cid:qrcode" width="200" height="200" style="display:block;margin:16px 0" />
      <p style="color:#888;font-size:0.85em">
        This code expires in 5 minutes and can only be used once.
      </p>
    </body></html>
    """
    msg.attach(MIMEText(html, "html"))

    img_part = MIMEImage(qr_png, name="qr.png")
    img_part.add_header("Content-ID",          "<qrcode>")
    img_part.add_header("Content-Disposition", "inline", filename="qr.png")
    msg.attach(img_part)

    with smtplib.SMTP_SSL("smtp.gmail.com", 465) as smtp:
        smtp.login(_GMAIL_USER, _GMAIL_PASS)
        smtp.sendmail(_GMAIL_USER, _NOTIFY_EMAIL, msg.as_string())


# ── Endpoints ─────────────────────────────────────────────────────────────────

@router.post("/qr/request")
def qr_request(body: QrRequestBody):
    """
    Generate a one-time QR token, store it in Firebase /qrTokens/{token},
    and email it to the configured address.
    Also clears the qrRequest pending flag on the device.
    """
    token  = _make_token()
    expiry = int(time.time()) + _TOKEN_TTL

    get_ref(f"/qrTokens/{token}").set({
        "deviceId": body.deviceId,
        "expiry":   expiry,
        "used":     False,
        "ts":       int(time.time() * 1000),
    })

    # Clear the pending flag so the ESP32 / dashboard stops waiting
    get_ref(f"/devices/{body.deviceId}/commands/qrRequest").set({
        "pending": False,
        "ts":      int(time.time() * 1000),
    })

    qr_png = _make_qr_png(token)
    email_sent = False
    try:
        _send_email(token, body.deviceId, qr_png)
        email_sent = True
        print(f"[QR] Token {token} emailed for device={body.deviceId}")
    except Exception as e:
        print(f"[QR] Email failed: {e}")

    return {
        "status":    "sent" if email_sent else "stored_no_email",
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
