"""
enrollment_indexer — IoT Rule target for iot/+/biometric/enroll
Indexes a user face image into the Rekognition collection when the ESP32
publishes an enrollment event with a Firebase Storage path.
"""
import json
import os
import boto3

rekognition  = boto3.client("rekognition", region_name=os.environ["AWS_REGION_NAME"])
s3_client    = boto3.client("s3",          region_name=os.environ["AWS_REGION_NAME"])

COLLECTION_ID = os.environ["REKOGNITION_COLLECTION_ID"]
S3_BUCKET     = os.environ.get("FACE_IMAGE_BUCKET", "")


def lambda_handler(event, context):
    device_id    = event.get("deviceId", "unknown")
    user_id      = event.get("userId", "")
    user_name    = event.get("name", "")
    storage_path = event.get("storagePath", "")

    print(f"[enrollment_indexer] device={device_id} user={user_id} "
          f"name={user_name} path={storage_path}")

    if not storage_path or not S3_BUCKET:
        print("[enrollment_indexer] No storage path or bucket configured — skipping Rekognition index")
        return {"statusCode": 200, "body": "skipped"}

    try:
        response = rekognition.index_faces(
            CollectionId=COLLECTION_ID,
            Image={"S3Object": {"Bucket": S3_BUCKET, "Name": storage_path}},
            ExternalImageId=user_id,
            DetectionAttributes=["DEFAULT"],
        )
        face_records = response.get("FaceRecords", [])
        print(f"[enrollment_indexer] Indexed {len(face_records)} face(s) for user {user_id}")
        return {"statusCode": 200, "faceCount": len(face_records)}
    except Exception as exc:
        print(f"[enrollment_indexer] Rekognition error: {exc}")
        return {"statusCode": 500, "error": str(exc)}
