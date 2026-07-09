"""
biometric_alert_agent — Handles anomaly alerts from biometric_pipeline.
Publishes an SNS notification to the user and sends an ACK back to the
originating ESP32 device via AWS IoT Core so the firmware can log it.
"""
import json
import os
import boto3

sns_client = boto3.client("sns",      region_name=os.environ["AWS_REGION_NAME"])
iot_client = boto3.client("iot-data", region_name=os.environ["AWS_REGION_NAME"])

SNS_TOPIC_ARN = os.environ["ALERT_SNS_TOPIC_ARN"]

ALERT_LABELS = {
    "brute_force":       "Brute-Force Attempt",
    "suspicious_score":  "Suspicious Match Score",
    "env_threshold":     "Environmental Threshold Exceeded",
}


def lambda_handler(event, context):
    device_id    = event.get("deviceId", "unknown")
    user_id      = event.get("userId", "unknown")
    alert_type   = event.get("alertType", "unknown")
    anomaly_score = float(event.get("anomalyScore", 0.0))

    label = ALERT_LABELS.get(alert_type, alert_type)
    print(f"[biometric_alert_agent] device={device_id} user={user_id} "
          f"type={alert_type} score={anomaly_score:.2f}")

    # 1. Publish SNS notification
    subject = f"Security Alert — {label}"
    message = (
        f"IRIS BIOMETRIC ALERT\n"
        f"Device  : {device_id}\n"
        f"User    : {user_id}\n"
        f"Type    : {label}\n"
        f"Score   : {anomaly_score * 100:.0f}%\n\n"
        f"Log in to the dashboard to review the event."
    )
    sns_client.publish(TopicArn=SNS_TOPIC_ARN, Subject=subject, Message=message)
    print(f"[biometric_alert_agent] SNS published to {SNS_TOPIC_ARN}")

    # 2. Publish ACK back to device so firmware can set _agentAckPending
    ack_topic   = f"iot/{device_id}/ai/alerts"
    ack_payload = json.dumps({"userId": user_id, "alertType": alert_type, "ack": True})
    iot_client.publish(topic=ack_topic, qos=0, payload=ack_payload.encode())
    print(f"[biometric_alert_agent] ACK published to {ack_topic}")

    return {"statusCode": 200, "body": "alert dispatched"}
