"""
lambda/biometric_pipeline/handler.py
=====================================
Triggered by AWS IoT Rule on topic: iot/+/biometric/signin

Flow (replacing S3 with Firebase Storage):
  1. Download JPEG from Firebase Storage using storagePath in the MQTT payload
  2. Call Rekognition SearchFacesByImage against the enrolled-faces collection
  3a. MATCH   → retrieve user from Firebase RTDB → anomaly check
       - No anomaly  → log access to Firebase → ACK ESP32 (grant)
       - Anomaly     → log alert to Firebase → SNS → ACK ESP32 (grant + alerted)
  3b. NO MATCH → log unmatched event → SNS alert → ACK ESP32 (deny)

Required environment variables (set in Lambda console):
  FIREBASE_DATABASE_URL         — e.g. https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app
  FIREBASE_STORAGE_BUCKET       — e.g. iot-fc8b3.firebasestorage.app
  FIREBASE_SERVICE_ACCOUNT_B64  — base64-encoded serviceAccountKey.json
  SNS_TOPIC_ARN                 — e.g. arn:aws:sns:ap-south-1:123456789:iot-biometric-alerts
  REKOGNITION_COLLECTION        — default: iot-biometric-faces
  REKOGNITION_MATCH_THRESHOLD   — default: 80  (similarity %)
  AWS_IOT_ENDPOINT              — a39l1ahbq3uw0h-ats.iot.ap-south-1.amazonaws.com
"""

import json
import os
import base64
import datetime
import boto3
import firebase_admin
from firebase_admin import credentials, db, storage as fb_storage

# ── Firebase init (runs once per Lambda container) ────────────────────────────

def _init_firebase():
    if firebase_admin._apps:
        return
    sa_b64   = os.environ["FIREBASE_SERVICE_ACCOUNT_B64"]
    sa_dict  = json.loads(base64.b64decode(sa_b64).decode("utf-8"))
    cred     = credentials.Certificate(sa_dict)
    firebase_admin.initialize_app(cred, {
        "databaseURL":   os.environ["FIREBASE_DATABASE_URL"],
        "storageBucket": os.environ["FIREBASE_STORAGE_BUCKET"],
    })

_init_firebase()

# ── AWS clients ───────────────────────────────────────────────────────────────

_REGION         = os.environ.get("AWS_DEFAULT_REGION", "ap-south-1")
rekognition     = boto3.client("rekognition", region_name=_REGION)
sns             = boto3.client("sns",         region_name=_REGION)
iot_data        = boto3.client("iot-data",    region_name=_REGION,
                               endpoint_url=f"https://{os.environ.get('AWS_IOT_ENDPOINT', '')}")

COLLECTION_ID   = os.environ.get("REKOGNITION_COLLECTION",      "iot-biometric-faces")
SNS_TOPIC_ARN   = os.environ["SNS_TOPIC_ARN"]
MATCH_THRESHOLD = float(os.environ.get("REKOGNITION_MATCH_THRESHOLD", "80"))


# ── Main handler ──────────────────────────────────────────────────────────────

def handler(event, context):
    """
    IoT Rule passes the full MQTT JSON payload as the event.
    Expected fields: deviceId, userId, matchScore, success, storagePath, ts
    """
    device_id    = event.get("deviceId", "unknown")
    storage_path = event.get("storagePath", "")
    ts           = event.get("ts", int(datetime.datetime.utcnow().timestamp() * 1000))

    print(f"[PIPELINE] device={device_id} path={storage_path!r}")

    if not storage_path:
        # No image — rely on local ESP32 result only; just log and exit
        print("[PIPELINE] No storagePath — skipping Rekognition, logging local result")
        _log_signin(device_id, event.get("userId", "unknown"),
                    event.get("userId", "Unknown"),
                    event.get("matchScore", 1.0),
                    event.get("success", False), 0.0, ts, source="local_only")
        return {"status": "local_only"}

    # 1. Download image from Firebase Storage
    bucket     = fb_storage.bucket()
    blob       = bucket.blob(storage_path)
    image_bytes = blob.download_as_bytes()
    print(f"[PIPELINE] Downloaded {len(image_bytes)} bytes from Firebase Storage")

    # 2. Rekognition — search enrolled faces
    try:
        reko_resp = rekognition.search_faces_by_image(
            CollectionId=COLLECTION_ID,
            Image={"Bytes": image_bytes},
            MaxFaces=1,
            FaceMatchThreshold=MATCH_THRESHOLD,
        )
        matches = reko_resp.get("FaceMatches", [])
    except rekognition.exceptions.InvalidParameterException as e:
        print(f"[PIPELINE] Rekognition error (no face?): {e}")
        matches = []

    if not matches:
        # NO MATCH
        print("[PIPELINE] No face match")
        _record_alert(device_id, "no_face_match", "unknown", ts, 1.0)
        _sns_alert(device_id, "unknown",
                   f"Unrecognised face at device {device_id}", ts)
        _ack_device(device_id, granted=False, user_id="unknown")
        _delete_storage_object(blob)
        return {"status": "no_match"}

    # MATCH
    best        = matches[0]
    similarity  = best["Similarity"]
    user_id     = best["Face"].get("ExternalImageId", "unknown")
    print(f"[PIPELINE] Matched user={user_id} similarity={similarity:.1f}%")

    # 3. Retrieve user metadata from Firebase
    user_data  = db.reference(f"/users/{user_id}").get() or {}
    user_name  = user_data.get("name", user_id)

    # 4. Anomaly check
    anomaly = _anomaly_check(device_id, user_id, ts)

    match_score = round(1.0 - similarity / 100.0, 4)  # convert % similarity → distance

    if anomaly:
        print(f"[PIPELINE] Anomaly detected for user={user_id}")
        _log_signin(device_id, user_id, user_name, match_score, True, 0.85, ts,
                    source="rekognition")
        _record_alert(device_id, "anomaly_detected", user_id, ts, 0.85)
        _sns_alert(device_id, user_id,
                   f"Anomalous access by {user_name} at device {device_id}", ts)
        _ack_device(device_id, granted=True, user_id=user_id)
    else:
        print(f"[PIPELINE] Clean access for user={user_id}")
        _log_signin(device_id, user_id, user_name, match_score, True, 0.0, ts,
                    source="rekognition")
        _ack_device(device_id, granted=True, user_id=user_id)

    _delete_storage_object(blob)
    return {"status": "matched", "userId": user_id, "similarity": similarity}


# ── Helpers ───────────────────────────────────────────────────────────────────

def _anomaly_check(device_id: str, user_id: str, ts_ms: int) -> bool:
    """
    Returns True if access looks anomalous:
    - Outside 06:00–22:00 UTC
    - More than 3 sign-ins for this device in the last 5 minutes
    """
    hour = datetime.datetime.utcfromtimestamp(ts_ms / 1000).hour
    if hour < 6 or hour >= 22:
        return True

    five_min_ago = ts_ms - 5 * 60 * 1000
    recent = (
        db.reference(f"/signins/{device_id}")
          .order_by_child("ts")
          .start_at(five_min_ago)
          .get()
    ) or {}
    if len(recent) > 3:
        return True

    return False


def _log_signin(device_id, user_id, user_name, match_score,
                success, anomaly_score, ts, source="rekognition"):
    db.reference(f"/signins/{device_id}").push({
        "userId":       user_id,
        "userName":     user_name,
        "matchScore":   match_score,
        "success":      success,
        "anomalyScore": anomaly_score,
        "source":       source,
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
    print(f"[PIPELINE] SNS alert published: {message}")


def _ack_device(device_id: str, granted: bool, user_id: str):
    """Publish result ACK back to the ESP32 via IoT Core MQTT."""
    topic   = f"iot/{device_id}/ai/alerts"
    payload = json.dumps({
        "userId":    user_id,
        "alertType": "rekognition_result",
        "granted":   granted,
        "ack":       True,
    }).encode()
    try:
        iot_data.publish(topic=topic, qos=0, payload=payload)
        print(f"[PIPELINE] ACK sent to {topic}: granted={granted}")
    except Exception as e:
        print(f"[PIPELINE] ACK publish failed: {e}")


def _delete_storage_object(blob):
    """Delete the JPEG after processing to keep Storage clean."""
    try:
        blob.delete()
    except Exception as e:
        print(f"[PIPELINE] Storage cleanup failed: {e}")
