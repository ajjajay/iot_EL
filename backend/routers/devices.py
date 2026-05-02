"""
routers/devices.py — Device endpoints
======================================
GET /api/devices              → all devices (meta + latest reading + online status)
GET /api/devices/{device_id}  → single device full state
GET /api/summary              → fleet-level aggregate stats
"""

from fastapi import APIRouter, HTTPException
from firebase_client import get_all, get_ref

router = APIRouter(tags=["devices"])


@router.get("/devices")
def list_devices():
    """
    Returns all registered devices from /devices/{id}.
    Each entry includes meta, online status, latest sensor reading, and heartbeat.
    """
    devices_raw = get_all("/devices")
    if not devices_raw:
        return []

    result = []
    for device_id, device_data in devices_raw.items():
        if not isinstance(device_data, dict):
            continue
        result.append({
            "deviceId":  device_id,
            "meta":      device_data.get("meta", {}),
            "online":    device_data.get("online", False),
            "latest":    device_data.get("latest", {}),
            "heartbeat": device_data.get("heartbeat", {}),
            "config":    device_data.get("config", {}),
        })

    # Sort by deviceId for stable ordering
    result.sort(key=lambda d: d["deviceId"])
    return result


@router.get("/devices/{device_id}")
def get_device(device_id: str):
    """
    Returns the full state of a single device including commands and config.
    """
    device_data = get_ref(f"/devices/{device_id}").get()
    if not device_data:
        raise HTTPException(status_code=404, detail=f"Device '{device_id}' not found")

    return {
        "deviceId":  device_id,
        "meta":      device_data.get("meta", {}),
        "online":    device_data.get("online", False),
        "latest":    device_data.get("latest", {}),
        "heartbeat": device_data.get("heartbeat", {}),
        "config":    device_data.get("config", {}),
        "commands":  device_data.get("commands", {}),
    }


@router.get("/summary")
def fleet_summary():
    """
    Fleet-level aggregate: device count, online count, alert count, auth success rate.
    """
    devices_raw = get_all("/devices")
    alerts_raw  = get_all("/alerts")
    signins_raw = get_all("/signins")

    total_devices  = len(devices_raw)
    online_devices = sum(
        1 for d in devices_raw.values()
        if isinstance(d, dict) and d.get("online", False)
    )

    # Count all alerts across all devices
    total_alerts = 0
    for device_alerts in alerts_raw.values():
        if isinstance(device_alerts, dict):
            total_alerts += len(device_alerts)

    # Sign-in stats
    total_signins   = 0
    success_signins = 0
    for device_signins in signins_raw.values():
        if isinstance(device_signins, dict):
            for event in device_signins.values():
                if isinstance(event, dict):
                    total_signins += 1
                    if event.get("success", False):
                        success_signins += 1

    return {
        "totalDevices":   total_devices,
        "onlineDevices":  online_devices,
        "offlineDevices": total_devices - online_devices,
        "totalAlerts":    total_alerts,
        "totalSignins":   total_signins,
        "successSignins": success_signins,
        "failedSignins":  total_signins - success_signins,
        "successRate":    round(success_signins / total_signins, 3) if total_signins else None,
    }
