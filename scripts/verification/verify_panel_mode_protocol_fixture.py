#!/usr/bin/env python3
"""Verify the checked-in protocol-v2 vectors independently of the C++ codec."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import sys
from typing import Any


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = PROJECT_ROOT / "docs/testing/fixtures/panel-mode-protocol-v2-golden.json"
SCHEMA = PROJECT_ROOT / "docs/interfaces/panel-mode-protocol-v2.schema.json"
MAX_SAFE_INTEGER = 9_007_199_254_740_991
DISPLAY = {"day_active": 1, "night_red": 2, "suspended": 3, "diagnostic": 4}
PRESENCE = {"unknown": 0, "present": 1, "grace_period": 2, "absent": 3}
SOLAR = {"unknown": 0, "day": 1, "night": 2}
SYSTEM = {
    "starting": 0,
    "active": 1,
    "suspended": 2,
    "recomputing_after_resume": 3,
    "faulted": 4,
}
SNAPSHOT_KEYS = {
    "appliance_release_id",
    "supervisor_session_id",
    "connection_challenge",
    "event_sequence",
    "display_mode",
    "presence_state",
    "solar_state",
    "system_state",
    "effective_brightness_percent",
    "observed_at_unix_milliseconds",
}


def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON member: {key}")
        result[key] = value
    return result


def load_json(path: pathlib.Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)


def require(condition: bool, code: str) -> None:
    if not condition:
        raise ValueError(code)


def expected_snapshot_frame(snapshot: dict[str, Any], snapshot_id: str) -> dict[str, Any]:
    return {
        "protocol_version": 2,
        "message_type": "lifecycle_snapshot",
        "appliance_release_id": snapshot["appliance_release_id"],
        "supervisor_session_id": snapshot["supervisor_session_id"],
        "connection_challenge": snapshot["connection_challenge"],
        "event_sequence": snapshot["event_sequence"],
        "snapshot_id": snapshot_id,
        "display_mode": snapshot["display_mode"],
        "presence_state": snapshot["presence_state"],
        "solar_state": snapshot["solar_state"],
        "system_state": snapshot["system_state"],
        "effective_brightness_percent": snapshot["effective_brightness_percent"],
        "observed_at_unix_milliseconds": snapshot["observed_at_unix_milliseconds"],
    }


def validate_line_frame(frame: str, label: str) -> dict[str, Any]:
    encoded = frame.encode("ascii")
    require(encoded.endswith(b"\n"), f"{label}.line_feed_missing")
    require(len(encoded) <= 4096, f"{label}.frame_too_large")
    require(b"\r" not in encoded and encoded.count(b"\n") == 1, f"{label}.framing_invalid")
    return json.loads(frame[:-1], object_pairs_hook=reject_duplicates)


def validate_vector(vector: dict[str, Any]) -> None:
    require(
        set(vector)
        == {
            "name",
            "snapshot",
            "preimage_hex",
            "snapshot_id",
            "snapshot_frame",
            "acknowledgement_frame",
        },
        "vector.members_invalid",
    )
    snapshot = vector["snapshot"]
    require(set(snapshot) == SNAPSHOT_KEYS, "snapshot.members_invalid")
    release = snapshot["appliance_release_id"]
    require(
        isinstance(release, str)
        and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}", release) is not None,
        "snapshot.release_invalid",
    )
    session = snapshot["supervisor_session_id"]
    challenge = snapshot["connection_challenge"]
    for identity in (session, challenge):
        require(
            isinstance(identity, str)
            and re.fullmatch(r"[0-9a-f]{32}", identity) is not None
            and int(identity, 16) != 0,
            "snapshot.identity_invalid",
        )
    sequence = snapshot["event_sequence"]
    brightness = snapshot["effective_brightness_percent"]
    observed = snapshot["observed_at_unix_milliseconds"]
    require(type(sequence) is int and 1 <= sequence <= MAX_SAFE_INTEGER, "snapshot.sequence_invalid")
    require(type(brightness) is int and 0 <= brightness <= 100, "snapshot.brightness_invalid")
    require(
        type(observed) is int and -5_364_662_400_000 <= observed < 4_133_980_800_000,
        "snapshot.time_invalid",
    )

    preimage = b"bhp.panel_mode.snapshot.v2\0"
    preimage += struct.pack(">BH", 2, len(release.encode("ascii")))
    preimage += release.encode("ascii") + bytes.fromhex(session) + bytes.fromhex(challenge)
    preimage += struct.pack(
        ">QBBBBBq",
        sequence,
        DISPLAY[snapshot["display_mode"]],
        PRESENCE[snapshot["presence_state"]],
        SOLAR[snapshot["solar_state"]],
        SYSTEM[snapshot["system_state"]],
        brightness,
        observed,
    )
    require(preimage.hex() == vector["preimage_hex"], "vector.preimage_mismatch")
    snapshot_id = "sha256:" + hashlib.sha256(preimage).hexdigest()
    require(snapshot_id == vector["snapshot_id"], "vector.digest_mismatch")

    snapshot_frame = validate_line_frame(vector["snapshot_frame"], "snapshot")
    require(
        snapshot_frame == expected_snapshot_frame(snapshot, snapshot_id),
        "vector.snapshot_frame_mismatch",
    )
    acknowledgement = validate_line_frame(
        vector["acknowledgement_frame"], "acknowledgement"
    )
    require(
        set(acknowledgement)
        == {
            "protocol_version",
            "message_type",
            "appliance_release_id",
            "supervisor_session_id",
            "connection_challenge",
            "event_sequence",
            "snapshot_id",
            "display_mode",
            "render_generation",
            "presentation_result",
        },
        "acknowledgement.members_invalid",
    )
    for field in (
        "appliance_release_id",
        "supervisor_session_id",
        "connection_challenge",
        "event_sequence",
        "snapshot_id",
        "display_mode",
    ):
        require(acknowledgement[field] == snapshot_frame[field], f"acknowledgement.{field}_mismatch")
    require(
        acknowledgement["protocol_version"] == 2
        and acknowledgement["message_type"] == "presentation_acknowledgement"
        and type(acknowledgement["render_generation"]) is int
        and 1 <= acknowledgement["render_generation"] <= MAX_SAFE_INTEGER
        and acknowledgement["presentation_result"] in {"committed", "failed"},
        "acknowledgement.value_invalid",
    )


def main() -> int:
    fixture = load_json(FIXTURE)
    schema = load_json(SCHEMA)
    require(fixture.get("schema_version") == 1, "fixture.version_invalid")
    require(fixture.get("digest_algorithm") == "sha256", "fixture.algorithm_invalid")
    require(fixture.get("preimage_contract") == "bhp.panel_mode.snapshot.v2", "fixture.domain_invalid")
    require(isinstance(fixture.get("vectors"), list) and len(fixture["vectors"]) == 2, "fixture.vector_count_invalid")
    require(schema.get("$schema") == "https://json-schema.org/draft/2020-12/schema", "schema.dialect_invalid")
    for vector in fixture["vectors"]:
        validate_vector(vector)
    print(f"panel_mode_protocol_fixture_verification_complete vectors={len(fixture['vectors'])}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, TypeError, ValueError, UnicodeError) as error:
        print(f"PANEL_MODE_PROTOCOL_FIXTURE.INVALID:{error}", file=sys.stderr)
        raise SystemExit(1) from error
