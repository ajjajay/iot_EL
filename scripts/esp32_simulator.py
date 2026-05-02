import time
import json
import random
import requests
from datetime import datetime
import argparse
import sys
import threading

try:
    import paho.mqtt.client as mqtt
    import ssl
except ImportError:
    print("Please install paho-mqtt: pip install paho-mqtt")
    sys.exit(1)

import os
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# Configuration
BACKEND_URL = "http://localhost:8000/predict"
FIREBASE_DB_URL = "https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app"
FIREBASE_AUTH_URL = f"https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=AIzaSyBsbx7C15g-Ws6yIYtNo3zd7vU5geQwS8g"
DEVICE_EMAIL = os.environ.get("DEVICE_EMAIL")
DEVICE_PASSWORD = os.environ.get("DEVICE_PASSWORD")

AWS_ENDPOINT = os.environ.get("AWS_ENDPOINT")
AWS_PORT = int(os.environ.get("AWS_PORT", 8883))
AWS_THING_NAME = os.environ.get("AWS_THING_NAME", "esp32_node_01")
AWS_ROOT_CA = os.environ.get("AWS_ROOT_CA", "certs/AmazonRootCA1.pem")
AWS_CERT = os.environ.get("AWS_CERT", "certs/device.crt")
AWS_KEY = os.environ.get("AWS_KEY", "certs/private.key")

def get_firebase_token():
    """Authenticate with Firebase to get an ID token (like the ESP32 does)."""
    payload = {
        "email": DEVICE_EMAIL,
        "password": DEVICE_PASSWORD,
        "returnSecureToken": True
    }
    try:
        res = requests.post(FIREBASE_AUTH_URL, json=payload)
        res.raise_for_status()
        return res.json()["idToken"]
    except Exception as e:
        print(f"[Firebase Auth] Failed to authenticate: {e}")
        return None

def update_firebase(path, data, id_token):
    """Update Firebase RTDB."""
    url = f"{FIREBASE_DB_URL}/{path}.json?auth={id_token}"
    try:
        res = requests.patch(url, json=data) # patch updates specific keys without overwriting everything
        res.raise_for_status()
    except Exception as e:
        print(f"[Firebase RTDB] Failed to update {path}: {e}")

def push_firebase(path, data, id_token):
    """Push new record to Firebase RTDB."""
    url = f"{FIREBASE_DB_URL}/{path}.json?auth={id_token}"
    try:
        res = requests.post(url, json=data)
        res.raise_for_status()
    except Exception as e:
        print(f"[Firebase RTDB] Failed to push to {path}: {e}")

def setup_mqtt():
    """Setup AWS IoT MQTT Client."""
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"[AWS MQTT] Connected to {AWS_ENDPOINT}")
            # Subscribe to the commands topic so we can receive messages from AWS
            topic = f"iot/{AWS_THING_NAME}/commands"
            client.subscribe(topic)
            print(f"[AWS MQTT] Subscribed to {topic} for incoming messages")
        else:
            print(f"[AWS MQTT] Connection failed with code {rc}")

    def on_message(client, userdata, msg):
        print(f"\n[AWS MQTT] 📥 RECEIVED MESSAGE on topic '{msg.topic}':")
        print(f"         {msg.payload.decode('utf-8')}\n")

    def on_disconnect(client, userdata, rc):
        print(f"[AWS MQTT] Disconnected with result code: {rc}")

    def on_log(client, userdata, level, buf):
        print(f"[AWS MQTT LOG] {buf}")

    # Use VERSION1 to suppress deprecation warning and match our callback signatures
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=AWS_THING_NAME)
    except AttributeError:
        # Fallback for older paho-mqtt versions
        client = mqtt.Client(client_id=AWS_THING_NAME)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.on_log = on_log
    
    try:
        client.tls_set(ca_certs=AWS_ROOT_CA,
                       certfile=AWS_CERT,
                       keyfile=AWS_KEY,
                       tls_version=ssl.PROTOCOL_TLSv1_2,
                       ciphers=None)
        client.connect(AWS_ENDPOINT, AWS_PORT, keepalive=60)
        client.loop_start()
        return client
    except Exception as e:
        print(f"[AWS MQTT] Connection failed (check certificates in certs/ folder): {e}")
        print("[AWS MQTT] Running without AWS connection...")
        return None

def simulate():
    print("Starting ESP32-CAM Simulator...")
    id_token = get_firebase_token()
    mqtt_client = setup_mqtt()

    # Base state
    temperature_c = 25.0
    humidity_pct = 50.0
    
    while True:
        # 1. Synthesize Sensor Data
        temperature_c += random.uniform(-1.0, 1.0)
        humidity_pct += random.uniform(-2.0, 2.0)
        light_norm = random.uniform(0.3, 0.8)
        
        temperature_c = max(-10, min(60, temperature_c))
        humidity_pct = max(0, min(100, humidity_pct))

        sensor_payload = {
            "temperature_c": round(temperature_c, 2),
            "humidity_pct": round(humidity_pct, 2),
            "light_norm": round(light_norm, 4)
        }

        # 2. Send to ML Model (Backend API)
        print(f"\n[Simulator] Sending to model: {sensor_payload}")
        ml_result = {"risk_score": 0.0, "label": "normal", "label_int": 0, "pNormal": 1.0, "pWarning": 0.0, "pCritical": 0.0}
        try:
            res = requests.post(BACKEND_URL, json=sensor_payload)
            if res.status_code == 200:
                ml_res = res.json()
                ml_result["risk_score"] = ml_res.get("risk_score", 0.0)
                ml_result["label"] = ml_res.get("label", "normal")
                ml_result["label_int"] = ml_res.get("label_int", 0)
                # Mock probabilities based on label
                if ml_result["label_int"] == 0: ml_result["pNormal"] = 0.9; ml_result["pWarning"] = 0.1; ml_result["pCritical"] = 0.0
                elif ml_result["label_int"] == 1: ml_result["pNormal"] = 0.1; ml_result["pWarning"] = 0.8; ml_result["pCritical"] = 0.1
                else: ml_result["pNormal"] = 0.0; ml_result["pWarning"] = 0.1; ml_result["pCritical"] = 0.9
                print(f"[Backend ML] Received inference: {ml_res}")
            else:
                print(f"[Backend ML] Failed with status {res.status_code}")
        except Exception as e:
            print(f"[Backend ML] Could not connect to backend: {e}")

        timestamp_ms = int(time.time() * 1000)
        timestamp_s = int(time.time())

        # Construct combined telemetry payload
        telemetry_payload = {
            "temperatureC": sensor_payload["temperature_c"],
            "humidityPct": sensor_payload["humidity_pct"],
            "lightRaw": int(sensor_payload["light_norm"] * 4095),
            "lightNorm": sensor_payload["light_norm"],
            "riskScore": ml_result["risk_score"],
            "mlLabel": ml_result["label_int"],
            "pNormal": ml_result["pNormal"],
            "pWarning": ml_result["pWarning"],
            "pCritical": ml_result["pCritical"],
            "state": "MONITORING" if ml_result["label_int"] == 0 else "ALERT",
            "ts": timestamp_ms
        }

        # 3. Publish to AWS IoT Core
        if mqtt_client:
            topic = f"iot/{AWS_THING_NAME}/telemetry"
            mqtt_client.publish(topic, json.dumps(telemetry_payload))
            print(f"[AWS MQTT] Published telemetry to {topic}")
            
            # Publish heartbeat
            hb_topic = f"iot/{AWS_THING_NAME}/heartbeat"
            hb_payload = {
                "deviceId": AWS_THING_NAME,
                "state": telemetry_payload["state"],
                "heapFree": 200000 + random.randint(-1000, 1000),
                "uptime": timestamp_s
            }
            mqtt_client.publish(hb_topic, json.dumps(hb_payload))

        # 4. Update Firebase (To un-hardcode the frontend)
        if id_token:
            # Update latest state in devices
            update_firebase(f"devices/{AWS_THING_NAME}/latest", telemetry_payload, id_token)
            # Push reading to history
            push_firebase(f"readings/{AWS_THING_NAME}", telemetry_payload, id_token)
            # Update heartbeat
            update_firebase(f"devices/{AWS_THING_NAME}/heartbeat", {
                "ts": timestamp_s,
                "state": telemetry_payload["state"],
                "heapFree": 200000,
                "uptime": timestamp_s
            }, id_token)
            update_firebase(f"devices/{AWS_THING_NAME}", {"online": True}, id_token)

        # 5. Simulate occasional Biometric Sign-in
        if random.random() < 0.2: # 20% chance every iteration
            success = random.random() > 0.3
            user_id = "john_doe" if success else "unknown"
            score = random.uniform(0.1, 0.29) if success else random.uniform(0.4, 0.9)
            
            signin_payload = {
                "userId": user_id,
                "userName": "John Doe" if success else "Unknown",
                "matchScore": round(score, 3),
                "success": success,
                "anomalyScore": round(random.uniform(0.0, 0.3) if success else random.uniform(0.5, 0.9), 3),
                "ts": timestamp_s
            }
            
            print(f"[Simulator] Biometric Sign-in Event: {signin_payload}")
            
            if mqtt_client:
                mqtt_client.publish(f"iot/{AWS_THING_NAME}/biometric/signin", json.dumps(signin_payload))
                if not success:
                    alert_payload = {
                        "deviceId": AWS_THING_NAME,
                        "alertType": "brute_force" if random.random() > 0.5 else "unrecognized_face",
                        "userId": user_id,
                        "anomalyScore": signin_payload["anomalyScore"],
                        "detail": "Failed biometric authentication",
                        "ts": timestamp_s,
                        "acknowledged": False
                    }
                    mqtt_client.publish(f"iot/{AWS_THING_NAME}/biometric/alert", json.dumps(alert_payload))
            
            if id_token:
                push_firebase(f"signins/{AWS_THING_NAME}", signin_payload, id_token)
                if not success:
                    alert_payload = {
                        "deviceId": AWS_THING_NAME,
                        "alertType": "brute_force",
                        "userId": user_id,
                        "anomalyScore": signin_payload["anomalyScore"],
                        "detail": "Failed biometric authentication",
                        "ts": timestamp_s,
                        "acknowledged": False
                    }
                    push_firebase(f"alerts/{AWS_THING_NAME}", alert_payload, id_token)

        time.sleep(5)

if __name__ == "__main__":
    simulate()
