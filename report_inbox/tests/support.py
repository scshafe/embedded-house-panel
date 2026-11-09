"""Shared helpers for the report inbox tests: a valid bundle, as the panel makes it."""

from __future__ import annotations

import gzip
import hashlib
import io
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

REPORT_ID = "rpt-0123456789abcdef0123456789abcdef"
FINGERPRINT = hashlib.sha256(b"bhp-music-control\nMUSIC.INTERNAL\nmusic_service.cpp:tick").hexdigest()


def wav(rate: int = 16000, channels: int = 1, seconds: float = 0.1) -> bytes:
    """A silent 16-bit PCM WAV; the panel's descriptions are 16 kHz mono."""
    out = io.BytesIO()
    with wave.open(out, "wb") as audio:
        audio.setnchannels(channels)
        audio.setsampwidth(2)
        audio.setframerate(rate)
        audio.writeframes(b"\0\0" * channels * int(rate * seconds))
    return out.getvalue()


def bundle(kind: str = "fault", report_id: str = REPORT_ID, description: str | None = None,
           audio: bytes | None = None) -> tuple[dict, dict[str, bytes]]:
    logs = gzip.compress(b'{"level":"info","trace_id":"t-1"}\n')
    files = {"logs.jsonl.gz": logs}
    if audio is not None:
        files["description.wav"] = audio
    manual = kind in ("text", "voice")
    manifest = {
        "schema": "bhp.bug-report/v1", "report_id": report_id, "kind": kind, "created_at": "2026-11-08T12:00:00Z",
        "fingerprint": hashlib.sha256(report_id.encode()).hexdigest() if manual else FINGERPRINT,
        "component": "panel_ui" if manual else "bhp-music-control",
        "error_code": None if manual else "MUSIC.INTERNAL",
        "location": None if manual else "music_service.cpp:tick",
        "build": {"board_id": "frdm-imx8mp", "project_commit": "abc123", "buildroot_version": "2026.05.1"},
        "screen": {"surface": "music", "state": "playing", "display_mode": "day_active", "presence": "present",
                   "solar": "day", "system": "active"},
        "trace_ids": ["t-1"], "log_lines": 1, "description_text": description,
        "files": [{"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in files.items()],
    }
    return manifest, files
