"""
routers/auth.py — Webcam face authentication via AWS Rekognition
POST /api/auth  { deviceId, imageBase64 } → { granted, userId, userName, matchScore, reason }
"""

import base64
import datetime
import json
import os

import boto3
from fastapi import APIRouter
from pydantic import BaseModel

from firebase_client import get_ref

router = APIRouter()

_REGION         = os.getenv("AWS_DEFAULT_REGION", "ap-south-1")
_COLLECTION_ID  = os.getenv("REKOGNITION_COLLECTION", "iot-biometric-faces")
_MATCH_THRESH   = float(os.getenv("REKOGNITION_MATCH_THRESHOLD", "80"))
_SNS_TOPIC_ARN  = os.getenv("SNS_TOPIC_ARN", "")
_IST            = datetime.timezone(datetime.timedelta(hours=5, minutes=30))

from botocore.config import Config as BotoConfig

_BOTO_CFG    = BotoConfig(connect_timeout=8, read_timeout=10, retries={"max_attempts": 1})
rekognition  = boto3.client("rekognition", region_name=_REGION, config=_BOTO_CFG)
sns          = boto3.client("sns",         region_name=_REGION)


class AuthBody(BaseModel):
    deviceId:    str
    imageBase64: str

class EnrollBody(BaseModel):
    deviceId:    str
    userId:      str
    name:        str
    imageBase64: str


@router.post("/auth")
def auth(body: AuthBody):
    ts = int(datetime.datetime.utcnow().timestamp() * 1000)

    try:
        image_bytes = base64.b64decode(body.imageBase64)
    except Exception:
        return {"granted": False, "reason": "invalid_image"}

    print(f"[AUTH] device={body.deviceId} image_size={len(image_bytes)}")

    # Rekognition face search
    try:
        matches = rekognition.search_faces_by_image(
            CollectionId=_COLLECTION_ID,
            Image={"Bytes": image_bytes},
            MaxFaces=1,
            FaceMatchThreshold=_MATCH_THRESH,
        ).get("FaceMatches", [])
    except Exception as e:
        print(f"[AUTH] Rekognition error: {e}")
        matches = []

    if not matches:
        print(f"[AUTH] No face match for device={body.deviceId}")
        _log_signin(body.deviceId, "unknown", "Unknown", 1.0, False, 1.0, ts)
        _set_relay(body.deviceId, "OFF")
        return {"granted": False, "reason": "no_match"}

    best       = matches[0]
    similarity = best["Similarity"]
    user_id    = best["Face"].get("ExternalImageId", "unknown")
    user_data  = get_ref(f"/users/{user_id}").get() or {}
    user_name  = user_data.get("name", user_id)
    score      = round(1.0 - similarity / 100.0, 4)

    print(f"[AUTH] Matched user={user_id} similarity={similarity:.1f}%")

    # Anomaly check
    is_anomaly, anomaly_score = _anomaly_check(body.deviceId, ts)

    if is_anomaly:
        print(f"[AUTH] Anomaly detected for user={user_id} score={anomaly_score}")
        _log_signin(body.deviceId, user_id, user_name, score, False, anomaly_score, ts)
        _set_relay(body.deviceId, "OFF")
        if _SNS_TOPIC_ARN:
            _sns_alert(body.deviceId, user_id, f"Anomalous access attempt by {user_name}", ts)
        return {"granted": False, "reason": "anomaly", "userId": user_id,
                "userName": user_name, "anomalyScore": anomaly_score}

    _log_signin(body.deviceId, user_id, user_name, score, True, 0.0, ts)
    _set_relay(body.deviceId, "ON")
    print(f"[AUTH] Access granted for user={user_id}")
    return {"granted": True, "userId": user_id, "userName": user_name, "matchScore": score}


@router.post("/enroll")
def enroll(body: EnrollBody):
    try:
        image_bytes = base64.b64decode(body.imageBase64)
    except Exception:
        return {"status": "error", "reason": "invalid_image"}

    print(f"[ENROLL] user={body.userId} device={body.deviceId} image_size={len(image_bytes)}")

    try:
        response = rekognition.index_faces(
            CollectionId=_COLLECTION_ID,
            Image={"Bytes": image_bytes},
            ExternalImageId=body.userId,
            MaxFaces=1,
            DetectionAttributes=[],
        )
    except Exception as e:
        print(f"[ENROLL] Rekognition error: {e}")
        return {"status": "error", "reason": str(e)}

    faces = response.get("FaceRecords", [])
    if not faces:
        print(f"[ENROLL] No face detected for user={body.userId}")
        return {"status": "no_face"}

    face_id = faces[0]["Face"]["FaceId"]
    print(f"[ENROLL] Indexed faceId={face_id} for user={body.userId}")

    ts = int(datetime.datetime.utcnow().timestamp() * 1000)
    get_ref(f"/users/{body.userId}").set({
        "userId":     body.userId,
        "name":       body.name,
        "deviceId":   body.deviceId,
        "enrolledAt": ts,
        "active":     True,
        "faceId":     face_id,
    })

    return {"status": "ok", "faceId": face_id, "userId": body.userId}


def _anomaly_check(device_id: str, ts_ms: int):
    score = 0.0
    ist_time = datetime.datetime.fromtimestamp(ts_ms / 1000, tz=_IST)
    if ist_time.hour < 6 or ist_time.hour >= 22:
        score += 0.5
    try:
        cutoff = ts_ms - 5 * 60 * 1000
        recent = get_ref(f"/signins/{device_id}").order_by_child("ts").start_at(cutoff).get() or {}
        if len(recent) > 3:
            score += min(len(recent) / 6.0, 0.5)
    except Exception:
        pass
    score = min(score, 1.0)
    return score >= 0.5, round(score, 4)


def _log_signin(device_id, user_id, user_name, match_score, success, anomaly_score, ts):
    get_ref(f"/signins/{device_id}").push({
        "userId": user_id, "userName": user_name, "deviceId": device_id,
        "matchScore": match_score, "success": success,
        "anomalyScore": anomaly_score, "source": "rekognition_webcam", "ts": ts,
    })


def _set_relay(device_id: str, state: str):
    get_ref(f"/devices/{device_id}/commands/relayOverride").set(state)


def _sns_alert(device_id, user_id, message, ts):
    sns.publish(
        TopicArn=_SNS_TOPIC_ARN,
        Subject=f"[IoT Security Alert] {device_id}",
        Message=json.dumps({"deviceId": device_id, "userId": user_id, "message": message, "ts": ts}),
    )
