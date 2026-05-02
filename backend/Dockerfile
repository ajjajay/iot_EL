# ─────────────────────────────────────────────────────────────────────────────
# Dockerfile for the IoT Biometric Backend (FastAPI)
#
# Build context: REPO ROOT (not backend/)
# Dockerfile path: backend/Dockerfile
#
# In Render:
#   Root Directory: (leave blank)
#   Dockerfile Path: backend/Dockerfile
#
# To build locally (from repo root):
#   docker build -f backend/Dockerfile -t iot-backend .
#
# To run locally:
#   docker run -p 8000:8000 \
#     -e FIREBASE_DATABASE_URL="https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app" \
#     -e FIREBASE_SERVICE_ACCOUNT_JSON='{"type":"service_account",...}' \
#     iot-backend
# ─────────────────────────────────────────────────────────────────────────────

FROM python:3.11-slim

# ── System deps ───────────────────────────────────────────────────────────────
# libgomp1 is required by tflite-runtime for multithreading
RUN apt-get update \
 && apt-get install -y --no-install-recommends libgomp1 \
 && rm -rf /var/lib/apt/lists/*

# ── Working directory ─────────────────────────────────────────────────────────
WORKDIR /app

# ── Python dependencies ───────────────────────────────────────────────────────
# Copy requirements first so Docker can cache this layer separately from code
COPY backend/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

# ── Application code ──────────────────────────────────────────────────────────
COPY backend/ .

# ── ML model artifacts (optional — app falls back to threshold rules if absent)
COPY models/ ./models/

# ── Port ──────────────────────────────────────────────────────────────────────
# Render injects PORT env var; uvicorn reads it via the shell command below
EXPOSE 8000

# ── Entrypoint ────────────────────────────────────────────────────────────────
# Use shell form so $PORT is expanded. Render sets PORT=10000 by default.
CMD uvicorn main:app --host 0.0.0.0 --port ${PORT:-8000}
