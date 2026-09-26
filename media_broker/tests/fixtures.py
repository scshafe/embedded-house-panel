"""Named semantic fixture inputs; no acoustic or production identity claim."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
DAY = {"display_mode": "day_active", "presence": True, "surface": "study", "idle": True,
       "muted": False, "consumer_connected": True, "active_context_nonce": "fixture-active-context",
       "render_generation": 1, "back_revealed": False, "history_available": False}
SYNTHETIC_AUDIO = b"synthetic-buffer-erasure-test-only"
PRIVATE_CANARY = "synthetic-private-utterance-must-not-escape"

class Clock:
    def __init__(self): self.value = 1_000_000_000
    def __call__(self): return self.value
    def advance_ms(self, milliseconds): self.value += milliseconds * 1_000_000
