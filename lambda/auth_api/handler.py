"""
lambda/auth_api/handler.py
===========================
Triggered by API Gateway: POST /auth

Body JSON:
  { "deviceId": "esp32_node_01", "imageBase64": "<base64-encoded JPEG>" }

Flow:
  1. Decode base64 image bytes
  2. Rekognition SearchFacesByImage against the iot-biometric-faces collection
  3a. NO MATCH  -> log denied signin -> set relayOverride OFF -> return {granted:false, reason:"no_match"}
  3b. MATCH + anomaly  -> log denied signin + alert -> set relayOverride OFF -> return {granted:false, reason:"anomaly"}
  3c. MATCH + no anomaly -> log success signin -> set relayOverride ON  -> return {granted:true}

The ESP32 polls /devices/{deviceId}/commands/relayOverride from Firebase to open/close the door.

Anomaly rules (rule-based, no ML):
  Rule 1 — Time-of-day: access outside 06:00-22:00 UTC scores 0.5
  Rule 2 — Frequency spike: >3 sign-ins for this device in the last 5 min adds up to 0.5
  Composite score >= 0.5 -> anomaly -> deny

Required env vars:
  FIREBASE_DATABASE_URL         e.g. https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app
  FIREBASE_SERVICE_ACCOUNT_B64  base64-encoded serviceAccountKey.json
  REKOGNITION_COLLECTION        default: iot-biometric-faces
  REKOGNITION_MATCH_THRESHOLD   default: 80  (similarity %)
  SNS_TOPIC_ARN                 optional — omit to skip SNS alerts
  AWS_DEFAULT_REGION            default: ap-south-1
"""

import json
import os
import base64
import datetime

import boto3
import firebase_admin
from firebase_admin import credentials, db


# ── Firebase init (once per Lambda container) ─────────────────────────────────

def _init_firebase():
    if firebase_admin._apps:
        return
    sa_dict = json.loads(base64.b64decode(os.environ["FIREBASE_SERVICE_ACCOUNT_B64"]).decode())
    firebase_admin.initialize_app(
        credentials.Certificate(sa_dict),
        {"databaseURL": os.environ["FIREBASE_DATABASE_URL"]},
    )


_init_firebase()

# ── AWS clients ───────────────────────────────────────────────────────────────

_REGION         = os.environ.get("AWS_DEFAULT_REGION", "ap-south-1")
rekognition     = boto3.client("rekognition", region_name=_REGION)
sns             = boto3.client("sns",         region_name=_REGION)

COLLECTION_ID   = os.environ.get("REKOGNITION_COLLECTION",      "iot-biometric-faces")
SNS_TOPIC_ARN   = os.environ.get("SNS_TOPIC_ARN",               "")
MATCH_THRESHOLD = float(os.environ.get("REKOGNITION_MATCH_THRESHOLD", "80"))

CORS = {
    "Access-Control-Allow-Origin":  "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "POST,OPTIONS",
}


# ── Handler ───────────────────────────────────────────────────────────────────

def handler(event, context):
    # CORS preflight
    if event.get("httpMethod") == "OPTIONS":
        return {"statusCode": 200, "headers": CORS, "body": ""}

    try:
        body = json.loads(event.get("body") or "{}")
    except Exception:
        return _resp(400, {"error": "Invalid JSON body"})

    device_id = body.get("deviceId", "")
    img_b64   = body.get("imageBase64", "")
    ts        = int(datetime.datetime.utcnow().timestamp() * 1000)

    if not device_id or not img_b64:
        return _resp(400, {"error": "deviceId and imageBase64 are required"})

    try:
        image_bytes = base64.b64decode(img_b64)
    except Exception:
        return _resp(400, {"error": "Invalid base64 image data"})

    print(f"[AUTH] device={device_id} image_size={len(image_bytes)}")

    # 1. Rekognition face search
    try:
        matches = rekognition.search_faces_by_image(
            CollectionId=COLLECTION_ID,
            Image={"Bytes": image_bytes},
            MaxFaces=1,
            FaceMatchThreshold=MATCH_THRESHOLD,
        ).get("FaceMatches", [])
    except (rekognition.exceptions.InvalidParameterException,
            rekognition.exceptions.InvalidImageFormatException):
        # No face detected or image format unrecognised — treat as no match
        matches = []

    # 2a. No match -> deny, log, alert
    if not matches:
        print(f"[AUTH] No face match for device={device_id}")
        _log_signin(device_id, "unknown", "Unknown", 1.0, False, 1.0, ts)
        _record_alert(device_id, "no_face_match", "unknown", ts, 1.0)
        if SNS_TOPIC_ARN:
            _sns_alert(device_id, "unknown",
                       f"Unrecognised face attempt at device {device_id}", ts)
        _set_relay(device_id, "OFF")
        return _resp(200, {"granted": False, "reason": "no_match"})

    # 2b. Match found
    best       = matches[0]
    similarity = best["Similarity"]
    user_id    = best["Face"].get("ExternalImageId", "unknown")
    print(f"[AUTH] Matched user={user_id} similarity={similarity:.1f}%")

    user_data  = db.reference(f"/users/{user_id}").get() or {}
    user_name  = user_data.get("name", user_id)
    score      = round(1.0 - similarity / 100.0, 4)   # convert % similarity -> RMS-style distance

    # 3. Rule-based anomaly check
    is_anomaly, anomaly_score = _anomaly_check(device_id, user_id, ts)

    if is_anomaly:
        print(f"[AUTH] Anomaly detected for user={user_id} score={anomaly_score}")
        _log_signin(device_id, user_id, user_name, score, False, anomaly_score, ts)
        _record_alert(device_id, "anomaly_detected", user_id, ts, anomaly_score)
        if SNS_TOPIC_ARN:
            _sns_alert(device_id, user_id,
                       f"Anomalous access attempt by {user_name} at device {device_id}", ts)
        _set_relay(device_id, "OFF")
        return _resp(200, {
            "granted":      False,
            "reason":       "anomaly",
            "userId":       user_id,
            "userName":     user_name,
            "anomalyScore": anomaly_score,
        })

    # 4. Clean match -> grant access
    print(f"[AUTH] Access granted for user={user_id}")
    _log_signin(device_id, user_id, user_name, score, True, 0.0, ts)
    _set_relay(device_id, "ON")
    return _resp(200, {
        "granted":    True,
        "userId":     user_id,
        "userName":   user_name,
        "matchScore": score,
    })


# ── Anomaly rules ─────────────────────────────────────────────────────────────

_IST = datetime.timezone(datetime.timedelta(hours=5, minutes=30))

def _anomaly_check(device_id: str, user_id: str, ts_ms: int):
    """Returns (is_anomaly: bool, composite_score: float). Pure rule-based logic."""
    score = 0.0
    ist_time = datetime.datetime.fromtimestamp(ts_ms / 1000, tz=_IST)
    hour = ist_time.hour

    # Rule 1: outside 06:00-22:00 IST
    if hour < 6 or hour >= 22:
        score += 0.5

    # Rule 2: >3 sign-ins in the last 5 minutes for this device (brute force / frequency spike)
    # Requires .indexOn ["ts"] in Firebase rules — skipped gracefully if index is missing
    try:
        cutoff = ts_ms - 5 * 60 * 1000
        recent = (
            db.reference(f"/signins/{device_id}")
              .order_by_child("ts")
              .start_at(cutoff)
              .get()
        ) or {}
        if len(recent) > 3:
            score += min(len(recent) / 6.0, 0.5)
    except Exception as e:
        print(f"[AUTH] Frequency check skipped (add .indexOn ts to Firebase rules): {e}")

    score = min(score, 1.0)
    return score >= 0.5, round(score, 4)


# ── Firebase helpers ──────────────────────────────────────────────────────────

def _log_signin(device_id, user_id, user_name, match_score, success, anomaly_score, ts):
    db.reference(f"/signins/{device_id}").push({
        "userId":       user_id,
        "userName":     user_name,
        "deviceId":     device_id,
        "matchScore":   match_score,
        "success":      success,
        "anomalyScore": anomaly_score,
        "source":       "rekognition_api",
        "ts":           ts,
    })


def _record_alert(device_id, alert_type, user_id, ts, anomaly_score):
    db.reference(f"/alerts/{device_id}").push({
        "deviceId":     device_id,
        "alertType":    alert_type,
        "userId":       user_id,
        "anomalyScore": anomaly_score,
        "acknowledged": False,
        "ts":           ts,
    })


def _sns_alert(device_id, user_id, message, ts):
    sns.publish(
        TopicArn=SNS_TOPIC_ARN,
        Subject=f"[IoT Security Alert] {device_id}",
        Message=json.dumps({
            "deviceId": device_id,
            "userId":   user_id,
            "message":  message,
            "ts":       ts,
        }),
    )
    print(f"[AUTH] SNS alert sent: {message}")


def _set_relay(device_id: str, state: str):
    """Write relay command to Firebase. ESP32 polls /devices/{id}/commands/relayOverride."""
    db.reference(f"/devices/{device_id}/commands/relayOverride").set(state)
    print(f"[AUTH] relayOverride={state} written for device={device_id}")


def _resp(status: int, body: dict):
    return {"statusCode": status, "headers": CORS, "body": json.dumps(body)}
