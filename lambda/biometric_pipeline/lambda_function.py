"""
biometric_pipeline — IoT Rule target for iot/+/biometric/signin
Receives sign-in telemetry from ESP32, invokes Rekognition face verification
if a storage path is provided, and forwards anomaly events to the alert agent.
"""
import json
import os
import boto3

iot_client    = boto3.client("iot-data", region_name=os.environ["AWS_REGION_NAME"])
rekognition   = boto3.client("rekognition", region_name=os.environ["AWS_REGION_NAME"])
lambda_client = boto3.client("lambda",      region_name=os.environ["AWS_REGION_NAME"])

COLLECTION_ID   = os.environ["REKOGNITION_COLLECTION_ID"]
ALERT_LAMBDA    = os.environ["ALERT_LAMBDA_NAME"]
ANOMALY_THRESH  = float(os.environ.get("ANOMALY_THRESHOLD", "0.60"))


def lambda_handler(event, context):
    device_id    = event.get("deviceId", "unknown")
    user_id      = event.get("userId", "")
    match_score  = float(event.get("matchScore", 1.0))
    success      = bool(event.get("success", False))
    anomaly_score = float(event.get("anomalyScore", 0.0))
    storage_path  = event.get("storagePath", "")

    print(f"[biometric_pipeline] device={device_id} user={user_id} "
          f"success={success} score={match_score:.3f} anomaly={anomaly_score:.3f}")

    # Forward to alert agent when anomaly score is high
    if anomaly_score >= ANOMALY_THRESH:
        alert_payload = {
            "deviceId":    device_id,
            "userId":      user_id,
            "alertType":   "suspicious_score" if success else "brute_force",
            "anomalyScore": anomaly_score,
            "matchScore":   match_score,
        }
        lambda_client.invoke(
            FunctionName=ALERT_LAMBDA,
            InvocationType="Event",
            Payload=json.dumps(alert_payload).encode(),
        )
        print(f"[biometric_pipeline] Forwarded anomaly alert to {ALERT_LAMBDA}")

    return {"statusCode": 200, "body": "ok"}
