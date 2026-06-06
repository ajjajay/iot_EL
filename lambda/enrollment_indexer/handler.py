"""
lambda/enrollment_indexer/handler.py
=====================================
Triggered by AWS IoT Rule on topic: iot/+/biometric/enroll

Flow:
  1. Download enrollment JPEG from Firebase Storage (storagePath in MQTT payload)
  2. Call Rekognition IndexFaces — stores face in the collection with ExternalImageId = userId
  3. Write rekognitionFaceId back to Firebase /users/{userId}
  4. Clean up JPEG from Firebase Storage

Required environment variables:
  FIREBASE_DATABASE_URL         — RTDB URL
  FIREBASE_STORAGE_BUCKET       — e.g. iot-fc8b3.firebasestorage.app
  FIREBASE_SERVICE_ACCOUNT_B64  — base64-encoded serviceAccountKey.json
  REKOGNITION_COLLECTION        — default: iot-biometric-faces
"""

import json
import os
import base64
import boto3
import firebase_admin
from firebase_admin import credentials, db, storage as fb_storage


def _init_firebase():
    if firebase_admin._apps:
        return
    sa_b64  = os.environ["FIREBASE_SERVICE_ACCOUNT_B64"]
    sa_dict = json.loads(base64.b64decode(sa_b64).decode("utf-8"))
    cred    = credentials.Certificate(sa_dict)
    firebase_admin.initialize_app(cred, {
        "databaseURL":   os.environ["FIREBASE_DATABASE_URL"],
        "storageBucket": os.environ["FIREBASE_STORAGE_BUCKET"],
    })


_init_firebase()

_REGION       = os.environ.get("AWS_DEFAULT_REGION", "ap-south-1")
rekognition   = boto3.client("rekognition", region_name=_REGION)
COLLECTION_ID = os.environ.get("REKOGNITION_COLLECTION", "iot-biometric-faces")


def handler(event, context):
    """
    MQTT payload: { deviceId, userId, name, storagePath, ts }
    """
    device_id    = event.get("deviceId", "unknown")
    user_id      = event.get("userId",   "")
    name         = event.get("name",     user_id)
    storage_path = event.get("storagePath", "")

    print(f"[ENROLL] device={device_id} user={user_id} path={storage_path!r}")

    if not user_id or not storage_path:
        print("[ENROLL] Missing userId or storagePath — skipping")
        return {"status": "skipped", "reason": "missing_fields"}

    # 1. Download image from Firebase Storage
    bucket      = fb_storage.bucket()
    blob        = bucket.blob(storage_path)
    image_bytes = blob.download_as_bytes()
    print(f"[ENROLL] Downloaded {len(image_bytes)} bytes")

    # 2. Index face in Rekognition — ExternalImageId links the face record to userId
    resp    = rekognition.index_faces(
        CollectionId=COLLECTION_ID,
        Image={"Bytes": image_bytes},
        ExternalImageId=user_id,
        DetectionAttributes=[],
        MaxFaces=1,
        QualityFilter="AUTO",
    )

    records = resp.get("FaceRecords", [])
    if not records:
        print(f"[ENROLL] No face detected in image for user={user_id}")
        blob.delete()
        return {"status": "no_face", "userId": user_id}

    face_id = records[0]["Face"]["FaceId"]
    print(f"[ENROLL] Indexed faceId={face_id} for user={user_id}")

    # 3. Write faceId to Firebase so the dashboard can display it
    db.reference(f"/users/{user_id}").update({
        "rekognitionFaceId": face_id,
        "name":              name,
        "deviceId":          device_id,
        "active":            True,
    })

    # 4. Clean up the enrollment JPEG from Storage
    blob.delete()

    return {"status": "indexed", "userId": user_id, "faceId": face_id}
