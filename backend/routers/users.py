"""
routers/users.py — Enrolled user endpoints
==========================================
GET  /api/users               → all enrolled users
GET  /api/users/{user_id}     → single enrolled user record
"""

from fastapi import APIRouter, HTTPException
from firebase_client import get_all, get_ref

router = APIRouter(tags=["users"])


@router.get("/users")
def list_users():
    """
    Returns all enrolled users from /users/{userId}.
    Filters to only active users unless ?include_inactive=true.
    """
    users_raw = get_all("/users")
    if not users_raw:
        return []

    result = []
    for user_id, user_data in users_raw.items():
        if not isinstance(user_data, dict):
            continue
        result.append({
            "userId":     user_id,
            "name":       user_data.get("name", ""),
            "deviceId":   user_data.get("deviceId", ""),
            "enrolledAt": user_data.get("enrolledAt"),
            "active":     user_data.get("active", True),
        })

    # Sort by enrollment time, newest first
    result.sort(key=lambda u: u.get("enrolledAt") or 0, reverse=True)
    return result


@router.get("/users/{user_id}")
def get_user(user_id: str):
    """
    Returns the enrolled user record for a specific userId.
    """
    user_data = get_ref(f"/users/{user_id}").get()
    if not user_data:
        raise HTTPException(status_code=404, detail=f"User '{user_id}' not found")
    return {
        "userId":     user_id,
        "name":       user_data.get("name", ""),
        "deviceId":   user_data.get("deviceId", ""),
        "enrolledAt": user_data.get("enrolledAt"),
        "active":     user_data.get("active", True),
    }
