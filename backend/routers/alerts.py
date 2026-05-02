"""
routers/alerts.py — Security alert endpoints
=============================================
GET /api/alerts                   → all alerts across all devices (last 50)
GET /api/alerts/{device_id}       → last N alerts for a specific device
PATCH /api/alerts/{device_id}/{alert_id}/acknowledge  → mark alert as acknowledged
"""

from fastapi import APIRouter, HTTPException, Query
from firebase_client import get_all, get_last_n, get_ref

router = APIRouter(tags=["alerts"])


@router.get("/alerts")
def list_all_alerts(limit: int = Query(50, ge=1, le=200)):
    """
    Returns the most recent alerts across ALL devices, flattened and sorted newest-first.
    """
    alerts_raw = get_all("/alerts")
    all_alerts = []

    for device_id, device_alerts in alerts_raw.items():
        if not isinstance(device_alerts, dict):
            continue
        for push_key, alert in device_alerts.items():
            if isinstance(alert, dict):
                all_alerts.append({**alert, "_pushKey": push_key, "_deviceId": device_id})

    # Sort by timestamp descending
    all_alerts.sort(key=lambda a: a.get("ts", 0), reverse=True)
    return all_alerts[:limit]


@router.get("/alerts/{device_id}")
def list_device_alerts(
    device_id: str,
    limit: int = Query(20, ge=1, le=100),
):
    """
    Returns the last `limit` alerts for a given device, newest first.
    """
    alerts = get_last_n(f"/alerts/{device_id}", n=limit)
    if alerts is None:
        raise HTTPException(status_code=404, detail=f"No alerts found for device '{device_id}'")
    return alerts


@router.patch("/alerts/{device_id}/{alert_id}/acknowledge")
def acknowledge_alert(device_id: str, alert_id: str):
    """
    Mark a specific alert as acknowledged in Firebase RTDB.
    The dashboard calls this when a security officer reviews an alert.
    """
    ref  = get_ref(f"/alerts/{device_id}/{alert_id}")
    data = ref.get()
    if not data:
        raise HTTPException(
            status_code=404,
            detail=f"Alert '{alert_id}' not found for device '{device_id}'"
        )
    ref.update({"acknowledged": True})
    return {"acknowledged": True, "alertId": alert_id, "deviceId": device_id}
