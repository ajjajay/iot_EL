"""
rekognition_verifier — On-demand face search against the Rekognition collection.
Called directly (not via IoT Rule) by other Lambdas or the dashboard backend
to cross-verify an iris match with a secondary cloud face-check.
"""
import json
import os
import boto3

rekognition = boto3.client("rekognition", region_name=os.environ["AWS_REGION_NAME"])

COLLECTION_ID   = os.environ["REKOGNITION_COLLECTION_ID"]
FACE_MATCH_THRESH = float(os.environ.get("FACE_MATCH_THRESHOLD", "80.0"))
S3_BUCKET       = os.environ.get("FACE_IMAGE_BUCKET", "")


def lambda_handler(event, context):
    """
    Input: { "storagePath": "s3-key", "expectedUserId": "john_doe" }
    Output: { "matched": bool, "confidence": float, "faceId": str }
    """
    storage_path    = event.get("storagePath", "")
    expected_user   = event.get("expectedUserId", "")

    if not storage_path or not S3_BUCKET:
        return {"matched": False, "reason": "no storage path or bucket"}

    try:
        response = rekognition.search_faces_by_image(
            CollectionId=COLLECTION_ID,
            Image={"S3Object": {"Bucket": S3_BUCKET, "Name": storage_path}},
            FaceMatchThreshold=FACE_MATCH_THRESH,
            MaxFaces=1,
        )
        matches = response.get("FaceMatches", [])
        if not matches:
            print(f"[rekognition_verifier] No match found for {storage_path}")
            return {"matched": False, "confidence": 0.0, "faceId": ""}

        top = matches[0]
        face_id    = top["Face"]["FaceId"]
        ext_img_id = top["Face"].get("ExternalImageId", "")
        confidence = top["Similarity"]

        matched = confidence >= FACE_MATCH_THRESH and ext_img_id == expected_user
        print(f"[rekognition_verifier] faceId={face_id} extId={ext_img_id} "
              f"confidence={confidence:.1f}% matched={matched}")

        return {"matched": matched, "confidence": confidence, "faceId": face_id,
                "rekognitionUserId": ext_img_id}

    except Exception as exc:
        print(f"[rekognition_verifier] Error: {exc}")
        return {"matched": False, "error": str(exc)}
