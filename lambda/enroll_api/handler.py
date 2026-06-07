"""
lambda/enroll_api/handler.py
==============================
Triggered by API Gateway: POST /enroll

Body JSON:
  { "deviceId": "...", "userId": "...", "name": "...", "imageBase64": "<base64 JPEG>" }

Flow:
  1. Decode base64 image bytes
  2. Rekognition IndexFaces — stores face in the collection with ExternalImageId = userId
     (ExternalImageId links every face record back to the user for SearchFacesByImage)
  3. Write/update user record in Firebase /users/{userId}
  4. Return { status: "indexed", userId, faceId }

If a face cannot be detected in the image the Lambda returns { status: "no_face" } with
a 200 so the frontend can prompt the user to try again with better lighting.

Re-enrollment: calling this endpoint again for the same userId adds a new face to the
collection (Rekognition allows multiple faces per ExternalImageId). Old faces remain.
To replace rather than accumulate, call rekognition.delete_faces() first with the stored
faceId from /users/{userId}/rekognitionFaceId before re-indexing.

Required env vars:
  FIREBASE_DATABASE_URL         e.g. https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app
  FIREBASE_SERVICE_ACCOUNT_B64  base64-encoded serviceAccountKey.json
  REKOGNITION_COLLECTION        default: iot-biometric-faces
  AWS_DEFAULT_REGION            default: ap-south-1
"""

import json
import os
import base64
import datetime

import boto3
import firebase_admin
from firebase_admin import credentials, db


# ── Firebase init ─────────────────────────────────────────────────────────────

def _init_firebase():
    if firebase_admin._apps:
        return
    sa_dict = json.loads(base64.b64decode(os.environ["FIREBASE_SERVICE_ACCOUNT_B64"]).decode())
    firebase_admin.initialize_app(
        credentials.Certificate(sa_dict),
        {"databaseURL": os.environ["FIREBASE_DATABASE_URL"]},
    )


_init_firebase()

_REGION       = os.environ.get("AWS_DEFAULT_REGION", "ap-south-1")
rekognition   = boto3.client("rekognition", region_name=_REGION)
COLLECTION_ID = os.environ.get("REKOGNITION_COLLECTION", "iot-biometric-faces")

CORS = {
    "Access-Control-Allow-Origin":  "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "POST,OPTIONS",
}


# ── Handler ───────────────────────────────────────────────────────────────────

def handler(event, context):
    if event.get("httpMethod") == "OPTIONS":
        return {"statusCode": 200, "headers": CORS, "body": ""}

    try:
        body = json.loads(event.get("body") or "{}")
    except Exception:
        return _resp(400, {"error": "Invalid JSON body"})

    device_id = body.get("deviceId", "")
    user_id   = body.get("userId", "").strip().replace(" ", "_")
    name      = body.get("name", user_id).strip()
    img_b64   = body.get("imageBase64", "")

    if not all([device_id, user_id, img_b64]):
        return _resp(400, {"error": "deviceId, userId, and imageBase64 are required"})

    try:
        image_bytes = base64.b64decode(img_b64)
    except Exception:
        return _resp(400, {"error": "Invalid base64 image data"})

    print(f"[ENROLL] device={device_id} user={user_id} name={name!r} bytes={len(image_bytes)}")

    # 1. Delete previous face records for this userId so re-enrollment replaces rather than accumulates
    _delete_existing_faces(user_id)

    # 2. Index face in Rekognition
    try:
        resp    = rekognition.index_faces(
            CollectionId=COLLECTION_ID,
            Image={"Bytes": image_bytes},
            ExternalImageId=user_id,
            DetectionAttributes=[],
            MaxFaces=1,
            QualityFilter="AUTO",
        )
        records = resp.get("FaceRecords", [])
    except (rekognition.exceptions.InvalidParameterException,
            rekognition.exceptions.InvalidImageFormatException):
        print(f"[ENROLL] Invalid image for user={user_id}")
        return _resp(200, {"status": "no_face", "userId": user_id})

    if not records:
        print(f"[ENROLL] No face detected for user={user_id}")
        return _resp(200, {"status": "no_face", "userId": user_id})

    face_id = records[0]["Face"]["FaceId"]
    print(f"[ENROLL] Indexed faceId={face_id} for user={user_id}")

    # 3. Write user record to Firebase
    db.reference(f"/users/{user_id}").set({
        "userId":            user_id,
        "name":              name,
        "deviceId":          device_id,
        "enrolledAt":        int(datetime.datetime.utcnow().timestamp() * 1000),
        "active":            True,
        "rekognitionFaceId": face_id,
    })

    return _resp(200, {"status": "indexed", "userId": user_id, "faceId": face_id})


# ── Helpers ───────────────────────────────────────────────────────────────────

def _delete_existing_faces(user_id: str):
    """Remove any previously indexed faces for this user so re-enrollment is clean."""
    existing = db.reference(f"/users/{user_id}").get() or {}
    old_face_id = existing.get("rekognitionFaceId")
    if not old_face_id:
        return
    try:
        rekognition.delete_faces(
            CollectionId=COLLECTION_ID,
            FaceIds=[old_face_id],
        )
        print(f"[ENROLL] Deleted old faceId={old_face_id} for user={user_id}")
    except Exception as e:
        print(f"[ENROLL] Could not delete old face (may already be gone): {e}")


def _resp(status: int, body: dict):
    return {"statusCode": status, "headers": CORS, "body": json.dumps(body)}
