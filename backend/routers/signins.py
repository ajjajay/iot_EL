"""
routers/signins.py — Biometric sign-in event endpoints
=======================================================
GET /api/signins                  → all sign-in events across all devices (last 100)
GET /api/signins/{device_id}      → last N sign-in events for a specific device
"""

from fastapi import APIRouter, Query
from firebase_client import get_all, get_last_n

router = APIRouter(tags=["sign-ins"])


@router.get("/signins")
def list_all_signins(limit: int = Query(100, ge=1, le=500)):
    """
    Returns the most recent biometric sign-in events across ALL devices,
    flattened and sorted newest-first.
    """
    signins_raw = get_all("/signins")
    all_signins = []

    for device_id, device_signins in signins_raw.items():
        if not isinstance(device_signins, dict):
            continue
        for push_key, event in device_signins.items():
            if isinstance(event, dict):
                all_signins.append({**event, "_pushKey": push_key, "_deviceId": device_id})

    all_signins.sort(key=lambda e: e.get("ts", 0), reverse=True)
    return all_signins[:limit]


@router.get("/signins/{device_id}")
def list_device_signins(
    device_id: str,
    limit: int = Query(20, ge=1, le=100),
):
    """
    Returns the last `limit` sign-in events for a given device, newest first.
    Fields per event (from Firebase schema):
      userId, userName, deviceId, matchScore, success, anomalyScore, ts
    """
    events = get_last_n(f"/signins/{device_id}", n=limit)
    return events or []
