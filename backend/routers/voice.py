"""
routers/voice.py — Voice update endpoint
POST /api/voice-update  →  { text, timestamp, source }

Tries Gemini first. Falls back to rule-based briefing if the API is
unavailable, over quota, or not configured — so voice always works.
"""

import os
import logging
import traceback
from datetime import datetime, timezone

from fastapi import APIRouter
from firebase_client import get_all, get_last_n

router = APIRouter()
logger = logging.getLogger(__name__)


# ── Helpers ────────────────────────────────────────────────────────────────────

def _fmt(val, decimals=1, suffix="") -> str:
    if val is None:
        return "N/A"
    try:
        return f"{round(float(val), decimals)}{suffix}"
    except (TypeError, ValueError):
        return str(val)


def _safe_dict(val) -> dict:
    return val if isinstance(val, dict) else {}


# ── Rule-based fallback ────────────────────────────────────────────────────────

def _rule_briefing(devices: dict, users: dict, signins: list, alerts: list) -> str:
    parts = []

    # 1. Alerts
    if alerts:
        types = list({a.get("alertType", "unknown") for a in alerts[:3]})
        noun  = "alerts" if len(alerts) > 1 else "alert"
        parts.append(
            f"Attention: {len(alerts)} active security {noun} detected "
            f"({', '.join(types)})."
        )

    # 2. Failed auth
    denied = [s for s in signins if not s.get("success")]
    if denied:
        n = len(denied)
        parts.append(
            f"There {'have been' if n > 1 else 'has been'} "
            f"{n} failed authentication attempt{'s' if n > 1 else ''} recently."
        )

    # 3. High smoke
    smoky = []
    for did, dev in devices.items():
        dev = _safe_dict(dev)
        smoke = _safe_dict(dev.get("latest")).get("smokePct")
        try:
            if smoke is not None and float(smoke) > 30:
                smoky.append(did)
        except (TypeError, ValueError):
            pass
    if smoky:
        parts.append(f"Elevated smoke detected on {', '.join(smoky)}.")

    # 4. Device status
    online_count  = sum(1 for d in devices.values() if _safe_dict(d).get("online"))
    total         = len(devices)
    offline_count = total - online_count
    if total == 0:
        parts.append("No devices registered.")
    elif offline_count:
        parts.append(
            f"{online_count} of {total} device{'s are' if total > 1 else ' is'} online; "
            f"{offline_count} offline."
        )
    else:
        parts.append(
            f"All {total} device{'s are' if total > 1 else ' is'} online and operational."
        )

    # 5. Users + close
    u = len(users)
    parts.append(
        f"{u} user{'s' if u != 1 else ''} enrolled. "
        + ("No further issues at this time." if not alerts and not denied else "Stay alert.")
    )

    return " ".join(parts)


# ── Gemini call (optional) ─────────────────────────────────────────────────────

def _gemini_briefing(api_key: str, context: str) -> str:
    from google import genai  # imported here so missing package doesn't break startup

    prompt = (
        "You are a security system voice assistant giving a live spoken status briefing "
        "to a security operator. Respond in 2 to 4 fluent spoken sentences. "
        "Lead with anything urgent (active alerts, denied access, high smoke). "
        "Then briefly summarise normal device status. "
        "Plain conversational speech only — no lists, no markdown, no headers. "
        "Keep it under 65 words.\n\n"
        + context
    )
    client   = genai.Client(api_key=api_key)
    response = client.models.generate_content(
        model="gemini-2.0-flash-lite",
        contents=prompt,
    )
    return response.text.strip()


# ── Endpoint ───────────────────────────────────────────────────────────────────

@router.post("/voice-update", tags=["voice"])
def voice_update():
    now = datetime.now(timezone.utc).strftime("%H:%M UTC")

    try:
        # ── Firebase data ──────────────────────────────────────────────────────
        devices = get_all("/devices")
        users   = get_all("/users")

        all_signins = []
        for dev_id in devices:
            for item in get_last_n(f"/signins/{dev_id}", 3):
                if isinstance(item, dict):
                    all_signins.append(item)
        all_signins.sort(key=lambda x: x.get("ts", 0) or 0, reverse=True)

        all_alerts = []
        for dev_id in devices:
            for item in get_last_n(f"/alerts/{dev_id}", 3):
                if isinstance(item, dict):
                    all_alerts.append(item)
        all_alerts.sort(key=lambda x: x.get("ts", 0) or 0, reverse=True)

        # ── Try Gemini ─────────────────────────────────────────────────────────
        api_key = os.getenv("GEMINI_API_KEY", "").strip()
        if api_key:
            # Build context string for Gemini
            device_lines = []
            for dev_id, dev in devices.items():
                dev    = _safe_dict(dev)
                latest = _safe_dict(dev.get("latest"))
                loc    = _safe_dict(dev.get("meta")).get("location", "unknown")
                device_lines.append(
                    f"- {dev_id} ({loc}): {'ONLINE' if dev.get('online') else 'OFFLINE'}, "
                    f"state={latest.get('state','?')}, "
                    f"temp={_fmt(latest.get('temperatureC'), suffix='°C')}, "
                    f"smoke={_fmt(latest.get('smokePct'), suffix='%')}"
                )
            signin_lines = [
                f"- {s.get('userName','?')}: {'GRANTED' if s.get('success') else 'DENIED'}"
                for s in all_signins[:5]
            ]
            alert_lines = [
                f"- {a.get('alertType','?')} on {a.get('deviceId','?')}"
                for a in all_alerts[:3]
            ]
            context = "\n".join([
                f"Time: {now}  |  Enrolled users: {len(users)}",
                "DEVICES:", *(device_lines or ["  none"]),
                "SIGN-INS:", *(signin_lines or ["  none"]),
                "ALERTS:", *(alert_lines or ["  none"]),
            ])

            try:
                text = _gemini_briefing(api_key, context)
                logger.info("[voice] Gemini briefing OK (%d chars)", len(text))
                return {"text": text, "timestamp": now, "source": "gemini"}
            except Exception as e:
                logger.warning("[voice] Gemini failed (%s) — using rule-based fallback", e)

        # ── Rule-based fallback ────────────────────────────────────────────────
        text = _rule_briefing(devices, users, all_signins[:6], all_alerts[:5])
        logger.info("[voice] Rule-based briefing OK")
        return {"text": text, "timestamp": now, "source": "rules"}

    except Exception:
        logger.error("[voice] Fatal error:\n%s", traceback.format_exc())
        return {
            "text": "Security system status unavailable. Please check the server connection.",
            "timestamp": now,
            "source": "error",
        }
