"""
routers/stream.py — Server-Sent Events stream of live device + sensor data
==========================================================================
GET /api/stream   → SSE; emits a JSON snapshot every 2 s pulled fresh from Firebase.

Connect from the browser:
    const es = new EventSource('/api/stream');
    es.onmessage = e => console.log(JSON.parse(e.data));

Each event payload is a list of device objects:
  [
    {
      "deviceId":  "esp32_node_01",
      "online":    true,
      "latest":    { temperatureC, humidityPct, smokeRaw, smokePct, distanceCm,
                     riskScore, mlLabel, state, ts },
      "heartbeat": { ts, state, heapFree, uptime },
      "readings":  [ ...last 20 readings, newest-first ]
    },
    ...
  ]
"""

import asyncio
import json
from fastapi import APIRouter
from fastapi.responses import StreamingResponse
from firebase_client import get_all, get_last_n

router = APIRouter(tags=["stream"])

POLL_INTERVAL_S = 2


async def _device_stream():
    """Async generator: fetches Firebase once per POLL_INTERVAL_S, yields SSE frames."""
    while True:
        try:
            devices_raw = get_all("/devices")
            payload = []
            for device_id, device_data in devices_raw.items():
                if not isinstance(device_data, dict):
                    continue
                readings = get_last_n(f"/readings/{device_id}", n=20)
                payload.append({
                    "deviceId":  device_id,
                    "online":    device_data.get("online", False),
                    "latest":    device_data.get("latest", {}),
                    "heartbeat": device_data.get("heartbeat", {}),
                    "readings":  readings,
                })
            yield f"data: {json.dumps(payload)}\n\n"
        except Exception as exc:
            yield f"data: {json.dumps({'error': str(exc)})}\n\n"

        await asyncio.sleep(POLL_INTERVAL_S)


@router.get("/stream")
async def stream_devices():
    """
    Server-Sent Events endpoint.
    Emits a JSON array of all device states every 2 seconds, pulled from Firebase.
    Use this to drive a real-time frontend without a direct Firebase SDK connection.
    """
    return StreamingResponse(
        _device_stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control":    "no-cache",
            "Connection":       "keep-alive",
            "X-Accel-Buffering": "no",   # disable nginx buffering
        },
    )
