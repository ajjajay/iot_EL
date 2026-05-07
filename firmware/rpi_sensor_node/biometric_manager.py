import os
import json
import numpy as np
from dataclasses import dataclass, field
from typing import List, Optional

from iris_camera import IrisCapture, IRIS_FEAT_DIM

BIO_MAX_USERS     = 16
BIO_MAX_TEMPLATES = 5
_BIO_DIR          = os.path.join(os.path.dirname(__file__), "bio")
_REGISTRY_PATH    = os.path.join(_BIO_DIR, "users.json")


@dataclass
class UserRecord:
    user_id: str = ""
    name: str = ""
    template_count: int = 0
    active: bool = False


@dataclass
class MatchResult:
    matched: bool = False
    user_id: str = ""
    user_name: str = ""
    score: float = 1e9
    template_idx: int = 0


class BiometricManager:
    def __init__(self):
        self._users: List[UserRecord] = []
        self._templates: List[List[Optional[np.ndarray]]] = []
        self._template_counts: List[int] = []

    def begin(self) -> bool:
        os.makedirs(_BIO_DIR, exist_ok=True)
        self._load_all()
        print(f"[BIO] Ready — {len(self._users)} user(s) enrolled")
        return True

    def enroll(self, user_id: str, name: str, iris: IrisCapture) -> bool:
        if not iris.valid:
            print("[BIO] enroll() rejected: invalid capture")
            return False

        idx = self._find_user(user_id)
        if idx < 0:
            if len(self._users) >= BIO_MAX_USERS:
                print("[BIO] Max users reached")
                return False
            idx = len(self._users)
            self._users.append(UserRecord(user_id=user_id, name=name, active=True))
            self._templates.append([None] * BIO_MAX_TEMPLATES)
            self._template_counts.append(0)

        # Ring-buffer overwrite when at capacity
        t_idx = self._template_counts[idx] % BIO_MAX_TEMPLATES
        self._templates[idx][t_idx] = iris.features.copy()

        if not self._save_template(idx, t_idx):
            print(f"[BIO] Failed to save template {t_idx} for '{user_id}'")
            return False

        if self._template_counts[idx] < BIO_MAX_TEMPLATES:
            self._template_counts[idx] += 1
        self._users[idx].template_count = self._template_counts[idx]

        self._save_registry()
        print(f"[BIO] Enrolled '{user_id}' template {t_idx + 1}/{self._template_counts[idx]}")
        return True

    def match(self, iris: IrisCapture, threshold: float) -> MatchResult:
        best = MatchResult()
        if not iris.valid or not self._users:
            return best

        for u_idx, user in enumerate(self._users):
            if not user.active or self._template_counts[u_idx] == 0:
                continue
            for t_idx in range(self._template_counts[u_idx]):
                tmpl = self._templates[u_idx][t_idx]
                if tmpl is None:
                    continue
                d = self._rms(iris.features, tmpl)
                if d < best.score:
                    best.score       = d
                    best.template_idx = t_idx
                    best.user_id     = user.user_id
                    best.user_name   = user.name

        best.matched = best.score < threshold
        status = "PASS" if best.matched else "FAIL"
        print(f"[BIO] Match: {best.user_id} score={best.score:.4f} thresh={threshold:.4f} → {status}")
        return best

    def user_count(self) -> int:
        return sum(1 for u in self._users if u.active)

    def is_enrolled(self, user_id: str) -> bool:
        return self._find_user(user_id) >= 0

    def remove_user(self, user_id: str) -> bool:
        idx = self._find_user(user_id)
        if idx < 0:
            return False
        for t_idx in range(BIO_MAX_TEMPLATES):
            p = self._template_path(idx, t_idx)
            if os.path.exists(p):
                os.remove(p)
        self._users[idx].active        = False
        self._template_counts[idx]     = 0
        self._save_registry()
        print(f"[BIO] Removed user '{user_id}'")
        return True

    # ── Private ───────────────────────────────────────────────────────────────

    def _rms(self, a: np.ndarray, b: np.ndarray) -> float:
        """Normalised RMS distance — matches BiometricManager::_rms() exactly."""
        return float(np.sqrt(np.mean((a - b) ** 2)))

    def _find_user(self, user_id: str) -> int:
        for i, u in enumerate(self._users):
            if u.active and u.user_id == user_id:
                return i
        return -1

    def _template_path(self, u_idx: int, t_idx: int) -> str:
        return os.path.join(_BIO_DIR, f"{self._users[u_idx].user_id}_t{t_idx}.npy")

    def _save_template(self, u_idx: int, t_idx: int) -> bool:
        try:
            np.save(self._template_path(u_idx, t_idx), self._templates[u_idx][t_idx])
            return True
        except Exception as e:
            print(f"[BIO] Save error: {e}")
            return False

    def _load_all(self):
        if not os.path.exists(_REGISTRY_PATH):
            return
        try:
            with open(_REGISTRY_PATH, "r") as f:
                data = json.load(f)
        except Exception as e:
            print(f"[BIO] Registry parse error: {e}")
            return

        for u in data.get("users", []):
            if len(self._users) >= BIO_MAX_USERS:
                break
            user = UserRecord(
                user_id=u.get("id", ""),
                name=u.get("name", ""),
                template_count=u.get("count", 0),
                active=u.get("active", False),
            )
            self._users.append(user)
            slots: List[Optional[np.ndarray]] = [None] * BIO_MAX_TEMPLATES
            loaded = 0
            for t_idx in range(user.template_count):
                p = os.path.join(_BIO_DIR, f"{user.user_id}_t{t_idx}.npy")
                if os.path.exists(p):
                    try:
                        slots[t_idx] = np.load(p)
                        loaded += 1
                    except Exception:
                        pass
            self._templates.append(slots)
            self._template_counts.append(loaded)

    def _save_registry(self):
        data = {"users": [
            {"id": u.user_id, "name": u.name,
             "count": u.template_count, "active": u.active}
            for u in self._users
        ]}
        try:
            with open(_REGISTRY_PATH, "w") as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"[BIO] Registry save error: {e}")
