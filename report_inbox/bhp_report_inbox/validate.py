"""The inbox's checks of what a panel sends (bhp.bug-report/v1).

The same rules as bug_reporter/report_bundle.h on the panel, checked again
here, because the inbox trusts nothing it receives. Every failure is a
ValidationError naming a field, never quoting the value.
"""

from __future__ import annotations

import re

SCHEMA = "bhp.bug-report/v1"
REPORT_ID = re.compile(r"rpt-[0-9a-f]{32}")
FINGERPRINT = re.compile(r"[0-9a-f]{64}")
SHA256 = re.compile(r"[0-9a-f]{64}")
TOKEN = re.compile(r"[a-z0-9_.-]{0,32}")
COMPONENT = re.compile(r"[a-z0-9_.-]{1,64}")
ERROR_CODE = re.compile(r"[A-Z][A-Z0-9_]*(\.[A-Z0-9_]+)+")
TRACE_ID = re.compile(r"[A-Za-z0-9._:-]{1,96}")
BUILD_KEY = re.compile(r"[a-z][a-z0-9_]{0,63}")
BUILD_VALUE = re.compile(r"[ -~]{0,200}")
TIMESTAMP = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z")
KINDS = {"voice", "text", "crash", "restart", "fault"}
MANUAL_KINDS = {"voice", "text"}
STATUSES = ("queued", "received", "investigating", "fix_in_pr", "merged", "update_ready", "installed", "wont_fix")
TERMINAL_STATUSES = {"installed", "wont_fix"}
#: The most bytes the inbox takes for each bundle file.
FILE_LIMITS = {"logs.jsonl.gz": 1024 * 1024, "description.wav": 2_000_000}
MANIFEST_LIMIT = 64 * 1024
#: A spoken description: the broker's 16 kHz capture, one channel, 60 s at most (bug_reporter/description_audio.h).
DESCRIPTION_RATE = 16000
DESCRIPTION_MAXIMUM_FRAMES = 60 * DESCRIPTION_RATE
SCREEN_FIELDS = ("surface", "state", "display_mode", "presence", "solar", "system")
MANIFEST_KEYS = {"schema", "report_id", "kind", "created_at", "fingerprint", "component", "error_code", "location",
                 "build", "screen", "trace_ids", "log_lines", "description_text", "files"}


class ValidationError(Exception):
    def __init__(self, field: str) -> None:
        super().__init__(f"REPORT_INBOX.MANIFEST_INVALID: {field}")
        self.field = field


def _require(condition: bool, field: str) -> None:
    if not condition:
        raise ValidationError(field)


def description_ok(text: str) -> bool:
    return len(text) <= 500 and all(ch == "\n" or (ord(ch) >= 0x20 and ord(ch) != 0x7F) for ch in text)


def validate_manifest(manifest: object, report_id: str) -> dict:
    """Returns the manifest when it follows bhp.bug-report/v1 for `report_id`."""
    _require(isinstance(manifest, dict) and set(manifest) == MANIFEST_KEYS, "keys")
    assert isinstance(manifest, dict)
    _require(manifest["schema"] == SCHEMA, "schema")
    _require(isinstance(manifest["report_id"], str) and REPORT_ID.fullmatch(manifest["report_id"]) is not None
             and manifest["report_id"] == report_id, "report_id")
    kind = manifest["kind"]
    _require(kind in KINDS, "kind")
    manual = kind in MANUAL_KINDS
    _require(isinstance(manifest["created_at"], str) and TIMESTAMP.fullmatch(manifest["created_at"]) is not None,
             "created_at")
    _require(isinstance(manifest["fingerprint"], str) and FINGERPRINT.fullmatch(manifest["fingerprint"]) is not None,
             "fingerprint")
    _require(isinstance(manifest["component"], str) and COMPONENT.fullmatch(manifest["component"]) is not None,
             "component")
    code = manifest["error_code"]
    _require(code is None if manual else (isinstance(code, str) and len(code) <= 96
                                           and ERROR_CODE.fullmatch(code) is not None), "error_code")
    location = manifest["location"]
    _require(location is None or (isinstance(location, str) and len(location) <= 256), "location")
    build = manifest["build"]
    _require(isinstance(build, dict) and 0 < len(build) <= 32 and all(
        isinstance(k, str) and BUILD_KEY.fullmatch(k) and isinstance(v, str) and BUILD_VALUE.fullmatch(v)
        for k, v in build.items()), "build")
    screen = manifest["screen"]
    _require(isinstance(screen, dict) and set(screen) == set(SCREEN_FIELDS) and all(
        isinstance(screen[f], str) and TOKEN.fullmatch(screen[f]) for f in SCREEN_FIELDS), "screen")
    traces = manifest["trace_ids"]
    _require(isinstance(traces, list) and len(traces) <= 64 and all(
        isinstance(t, str) and TRACE_ID.fullmatch(t) for t in traces), "trace_ids")
    _require(isinstance(manifest["log_lines"], int) and not isinstance(manifest["log_lines"], bool)
             and manifest["log_lines"] >= 0, "log_lines")
    text = manifest["description_text"]
    _require(text is None or (manual and isinstance(text, str) and description_ok(text)), "description_text")
    files = manifest["files"]
    _require(isinstance(files, list) and 1 <= len(files) <= 2, "files")
    names = set()
    for item in files:
        _require(isinstance(item, dict) and set(item) == {"name", "bytes", "sha256"}, "files")
        name = item["name"]
        _require(name in FILE_LIMITS and name not in names, "files.name")
        names.add(name)
        _require(isinstance(item["bytes"], int) and not isinstance(item["bytes"], bool)
                 and 0 < item["bytes"] <= FILE_LIMITS[name], "files.bytes")
        _require(isinstance(item["sha256"], str) and SHA256.fullmatch(item["sha256"]) is not None, "files.sha256")
    _require("logs.jsonl.gz" in names, "files.logs")
    _require("description.wav" not in names or kind == "voice", "files.audio")
    return manifest


def parse_content_range(header: str | None) -> tuple[int, int, int] | None:
    """`bytes A-B/TOTAL` as (start, end inclusive, total), or None."""
    match = re.fullmatch(r"bytes (\d{1,12})-(\d{1,12})/(\d{1,12})", header or "")
    if not match:
        return None
    start, end, total = (int(group) for group in match.groups())
    if end < start or end >= total:
        return None
    return start, end, total
