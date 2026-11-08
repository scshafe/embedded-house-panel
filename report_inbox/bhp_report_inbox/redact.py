"""What may be public about a report (bug-reports-and-self-repair.md, section 6).

An allowlist, not a denylist: the public summary is built only from closed,
machine-made fields, each checked again against its pattern here. Free text
(descriptions, transcripts, log lines, locations, context values), presence,
trace IDs and addresses are never read into it. A field that fails its
pattern stops the summary (RedactionError) rather than being passed through.
"""

from __future__ import annotations

import re

from . import validate

PUBLIC_BUILD_KEYS = ("board_id", "project_version", "project_commit", "buildroot_version")
PUBLIC_BUILD_VALUE = re.compile(r"[A-Za-z0-9._-]{1,64}")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")


class RedactionError(Exception):
    def __init__(self, field: str) -> None:
        super().__init__(f"REPORT_INBOX.REDACTION_REFUSED: {field}")
        self.field = field


def _checked(value: object, pattern: re.Pattern[str], field: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise RedactionError(field)
    return value


def public_summary(manifest: dict, fingerprint_record: dict | None) -> dict:
    """The fields of section 6, and nothing else."""
    kind = manifest.get("kind")
    if kind not in validate.KINDS:
        raise RedactionError("kind")
    summary = {
        "report_id": _checked(manifest.get("report_id"), validate.REPORT_ID, "report_id"),
        "kind": kind,
        "component": _checked(manifest.get("component"), validate.COMPONENT, "component"),
        "error_code": None if kind in validate.MANUAL_KINDS else _checked(
            manifest.get("error_code"), validate.ERROR_CODE, "error_code"),
        "surface": _checked((manifest.get("screen") or {}).get("surface"), validate.TOKEN, "surface"),
        "state": _checked((manifest.get("screen") or {}).get("state"), validate.TOKEN, "state"),
        "fingerprint": _checked(manifest.get("fingerprint"), validate.FINGERPRINT, "fingerprint"),
        "build": {},
        "count": 1,
        "first_seen": _checked((manifest.get("created_at") or "")[:10], DATE, "created_at"),
        "last_seen": _checked((manifest.get("created_at") or "")[:10], DATE, "created_at"),
    }
    build = manifest.get("build") or {}
    for key in PUBLIC_BUILD_KEYS:
        if key in build:
            summary["build"][key] = _checked(build[key], PUBLIC_BUILD_VALUE, f"build.{key}")
    if fingerprint_record and kind not in validate.MANUAL_KINDS:
        repeats = fingerprint_record.get("repeats", 0)
        reports = fingerprint_record.get("reports", [])
        if not isinstance(repeats, int) or not isinstance(reports, list):
            raise RedactionError("fingerprint_record")
        summary["count"] = len(reports) + repeats
        summary["first_seen"] = _checked(str(fingerprint_record.get("first_seen", ""))[:10], DATE, "first_seen")
        summary["last_seen"] = _checked(str(fingerprint_record.get("last_seen", ""))[:10], DATE, "last_seen")
    return summary


def issue_title(summary: dict) -> str:
    if summary["error_code"]:
        return f"Panel report: {summary['error_code']} in {summary['component']}"
    return f"Panel report: {summary['kind']} report on {summary['surface'] or 'home'}"


def issue_body(summary: dict) -> str:
    """Markdown for a public issue. Every line comes from the summary's checked fields."""
    build = ", ".join(f"{key} `{value}`" for key, value in summary["build"].items()) or "unknown"
    lines = [
        "A redacted summary of a private panel report. The bundle (logs, screen, description) stays in the",
        "owner's private inbox; this issue holds only closed codes.",
        "",
        f"- **Report:** `{summary['report_id']}` ({summary['kind']})",
        f"- **Component:** `{summary['component']}`",
        f"- **Error code:** `{summary['error_code']}`" if summary["error_code"] else "- **Error code:** none (manual report)",
        f"- **Screen:** `{summary['surface'] or 'home'}` / `{summary['state'] or '-'}`",
        f"- **Build:** {build}",
        f"- **Seen:** {summary['count']} time(s), {summary['first_seen']} to {summary['last_seen']}",
        f"- **Fingerprint:** `{summary['fingerprint']}`",
    ]
    return "\n".join(lines) + "\n"
