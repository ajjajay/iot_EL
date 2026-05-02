import time
import sys
import ssl

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Please install paho-mqtt: pip install paho-mqtt")
    sys.exit(1)

import os
from dotenv import load_dotenv

load_dotenv()

# AWS Configuration
AWS_ENDPOINT = os.environ.get("AWS_ENDPOINT")
AWS_PORT = int(os.environ.get("AWS_PORT", 8883))
AWS_THING_NAME = "my_desktop_listener"
AWS_ROOT_CA = os.environ.get("AWS_ROOT_CA", "certs/AmazonRootCA1.pem")
AWS_CERT = os.environ.get("AWS_CERT", "certs/device.crt")
AWS_KEY = os.environ.get("AWS_KEY", "certs/private.key")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"✅ Successfully Connected to AWS IoT Core!")
        print(f"✅ Subscribing to ALL topics ('#') to listen for data...\n")
        client.subscribe("#")
    else:
        print(f"❌ Connection failed with code {rc}")

def on_message(client, userdata, msg):
    print(f"📥 [RECEIVED on {msg.topic}]")
    print(f"   {msg.payload.decode('utf-8')}\n")

print("Starting AWS IoT Listener (Alternative to the AWS web console)...")

# Setup Client
try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=AWS_THING_NAME)
except AttributeError:
    client = mqtt.Client(client_id=AWS_THING_NAME)

client.on_connect = on_connect
client.on_message = on_message

try:
    client.tls_set(ca_certs=AWS_ROOT_CA,
                   certfile=AWS_CERT,
                   keyfile=AWS_KEY,
                   tls_version=ssl.PROTOCOL_TLSv1_2)
    client.connect(AWS_ENDPOINT, AWS_PORT, keepalive=60)
    
    # Loop forever to keep listening
    client.loop_forever()
    
except Exception as e:
    print(f"Error connecting: {e}")
