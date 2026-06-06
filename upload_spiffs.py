"""
upload_spiffs.py
Builds a SPIFFS image from firmware/esp32_sensor_node/data/
and flashes it to the ESP32 (Huge APP partition scheme).
"""

import os
import subprocess
import sys

MKSPIFFS  = r"C:\Users\AjayG\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"
DATA_DIR  = r"C:\Users\AjayG\iot_EL\firmware\esp32_sensor_node\data"
IMG       = r"C:\Users\AjayG\iot_EL\spiffs.bin"
PORT      = "COM5"
BAUD      = "921600"

# Huge APP partition: SPIFFS @ 0x310000, size 0xE0000 (896 KB)
OFFSET    = "0x310000"
SIZE      = 0xE0000
BLOCK     = 4096
PAGE      = 256

def run(cmd):
    print(">>", " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout: print(r.stdout)
    if r.stderr: print(r.stderr)
    if r.returncode != 0:
        print(f"ERROR: exited with code {r.returncode}")
        sys.exit(r.returncode)

print("=== Step 1: Build SPIFFS image ===")
run([MKSPIFFS, "-c", DATA_DIR, "-b", str(BLOCK), "-p", str(PAGE),
     "-s", str(SIZE), IMG])
print(f"Image created: {IMG}  ({os.path.getsize(IMG):,} bytes)\n")

print("=== Step 2: Flash SPIFFS to ESP32 ===")
run([sys.executable, "-m", "esptool",
     "--chip", "esp32",
     "--port", PORT,
     "--baud", BAUD,
     "write_flash", OFFSET, IMG])

print("\nDone — SPIFFS uploaded. Now re-upload the sketch in Arduino IDE.")
