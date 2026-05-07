import time
from dataclasses import dataclass
from typing import List

ANOMALY_WINDOW    = 20
BRUTE_FORCE_LIMIT = 5


@dataclass
class SignInEvent:
    user_id: str = ""
    match_score: float = 0.0
    success: bool = False
    timestamp_ms: float = 0.0
    valid: bool = False


class AnomalyDetector:
    def __init__(self, match_threshold: float):
        self._threshold        = match_threshold
        self._window: List[SignInEvent] = [SignInEvent() for _ in range(ANOMALY_WINDOW)]
        self._head             = 0
        self._count            = 0
        self._consecutive_fails = 0

    def record(self, result, success: bool) -> float:
        ev = SignInEvent(
            user_id      = result.user_id if result.user_id else "unknown",
            match_score  = result.score,
            success      = success,
            timestamp_ms = time.time() * 1000,
            valid        = True,
        )

        if success:
            self._consecutive_fails = 0
        else:
            self._consecutive_fails = min(self._consecutive_fails + 1, 255)

        self._window[self._head] = ev
        self._head  = (self._head + 1) % ANOMALY_WINDOW
        if self._count < ANOMALY_WINDOW:
            self._count += 1

        fail  = self._score_failure_rate()
        prox  = self._score_match_proximity(result.score)
        freq  = self._score_frequency(ev.timestamp_ms)
        score = max(0.0, min(1.0, 0.40 * fail + 0.35 * prox + 0.25 * freq))

        print(f"[ANOM] score={score:.3f}  fail={fail:.2f} prox={prox:.2f} freq={freq:.2f}")
        return score

    def brute_force_score(self) -> float:
        return self._score_failure_rate()

    def consecutive_failures(self) -> int:
        return self._consecutive_fails

    # ── Private ───────────────────────────────────────────────────────────────

    def _score_failure_rate(self) -> float:
        if self._consecutive_fails == 0:
            return 0.0
        return min(self._consecutive_fails / BRUTE_FORCE_LIMIT, 1.0)

    def _score_match_proximity(self, score: float) -> float:
        lo = self._threshold * 0.30
        hi = self._threshold
        if score < lo or score >= hi:
            return 0.0
        return min((score - lo) / (hi - lo), 1.0)

    def _score_frequency(self, now_ms: float) -> float:
        if self._count == 0:
            return 0.0
        window_ms  = 60_000.0
        normal_max = 5
        recent = sum(
            1 for ev in self._window
            if ev.valid and (now_ms - ev.timestamp_ms) < window_ms
        )
        if recent <= normal_max:
            return 0.0
        return min((recent - normal_max) / normal_max, 1.0)
