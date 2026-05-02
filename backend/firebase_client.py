"""
firebase_client.py — Firebase Admin SDK client
===============================================
Initialises the Firebase Admin app once at import time.
Credentials are loaded from environment variables — NEVER from committed files.

Environment variables (set in Render dashboard):
  FIREBASE_SERVICE_ACCOUNT_JSON  — full service account JSON as a single-line string
                                   (paste the entire JSON, Render stores it securely)
  FIREBASE_DATABASE_URL          — e.g. https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app
"""

import os
import json
import base64
from functools import lru_cache

import firebase_admin
from firebase_admin import credentials, db


# ── Initialise ────────────────────────────────────────────────────────────────

def _init_firebase() -> None:
    """
    Initialise the Firebase Admin SDK from env vars.

    Credential loading order (most reliable first):
      1. FIREBASE_SERVICE_ACCOUNT_B64  — base64-encoded JSON  ← RECOMMENDED for Render
      2. FIREBASE_SERVICE_ACCOUNT_JSON — raw JSON string

    Render can mangle raw JSON (newlines in private key, quote escaping).
    Base64 is a plain alphanumeric string — always safe.
    """
    if firebase_admin._apps:
        return  # already initialised

    database_url = os.getenv("FIREBASE_DATABASE_URL", "").strip()
    if not database_url:
        raise EnvironmentError(
            "FIREBASE_DATABASE_URL env var is not set. "
            "Add it in Render → Environment → Environment Variables."
        )

    sa_json = ""

    # 1. Try base64 first — most reliable when pasting into Render
    sa_b64 = os.getenv("FIREBASE_SERVICE_ACCOUNT_B64", "").strip()
    if sa_b64:
        try:
            sa_json = base64.b64decode(sa_b64).decode("utf-8")
            print("[FIREBASE] Using base64-encoded service account credentials")
        except Exception as e:
            raise EnvironmentError(
                f"FIREBASE_SERVICE_ACCOUNT_B64 is set but could not be base64-decoded: {e}"
            )

    # 2. Fall back to raw JSON string
    if not sa_json:
        sa_json = os.getenv("FIREBASE_SERVICE_ACCOUNT_JSON", "").strip()
        if sa_json:
            print("[FIREBASE] Using raw JSON service account credentials")

    if not sa_json:
        raise EnvironmentError(
            "Firebase credentials not found. Set one of these in Render → Environment:\n"
            "  FIREBASE_SERVICE_ACCOUNT_B64  — RECOMMENDED: base64-encode your serviceAccountKey.json\n"
            "    PowerShell: [Convert]::ToBase64String([IO.File]::ReadAllBytes('serviceAccountKey.json'))\n"
            "    Linux/Mac:  base64 -w 0 serviceAccountKey.json\n"
            "  FIREBASE_SERVICE_ACCOUNT_JSON — paste the raw JSON (may fail if Render mangles quotes/newlines)"
        )

    try:
        sa_dict = json.loads(sa_json)
    except json.JSONDecodeError as e:
        raise EnvironmentError(
            f"Firebase service account JSON is invalid: {e}\n"
            "Tip: Use FIREBASE_SERVICE_ACCOUNT_B64 (base64-encoded) instead of raw JSON "
            "to avoid Render mangling the private key newlines and quotes."
        )

    cred = credentials.Certificate(sa_dict)
    firebase_admin.initialize_app(cred, {"databaseURL": database_url})
    print(f"[FIREBASE] Initialised for project: {sa_dict.get('project_id', '?')}")


# Initialise on module load
_init_firebase()


# ── Helpers ───────────────────────────────────────────────────────────────────

def get_ref(path: str):
    """Return a Firebase RTDB reference for the given path."""
    return db.reference(path)


def get_all(path: str) -> dict:
    """Fetch all children at `path`. Returns {} if node doesn't exist."""
    val = get_ref(path).get()
    return val if isinstance(val, dict) else {}


def get_last_n(path: str, n: int = 20) -> list[dict]:
    """
    Fetch the last N push-key children from `path`.
    Firebase push keys are time-ordered, so order_by_key + limit_to_last gives newest-last.
    Returns a list of values sorted newest-first for API convenience.
    """
    ref  = get_ref(path)
    data = ref.order_by_key().limit_to_last(n).get()
    if not data:
        return []
    # data is a dict {pushKey: value} — sort newest first
    return [v for _, v in sorted(data.items(), reverse=True) if v is not None]
