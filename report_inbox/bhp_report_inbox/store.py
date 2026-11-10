"""The inbox's files on the owner's PC (bug-reports-and-self-repair.md, section 5).

Layout under the root (the setup doc gives the modes and ACLs):

    incoming/<id>/<name>       uploads in progress
    reports/<id>/report.json   a completed report: its manifest and files
    status/<id>.json           its status: the agent writes, the receiver reads
    fingerprints/<fp>.json     repeats of an automatic report, and its reports
    transcripts/<id>.txt       a spoken description's transcript (decision 4)

Every write is a temporary file, fsync and rename, so a crash never leaves a
half-written record. Nothing here ever reaches Git.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import shutil
import stat
import tempfile
import threading
from pathlib import Path

from . import validate

#: The most bytes a status record may have; the agent's records are well under 1 KiB.
STATUS_LIMIT = 4096
#: Days an abandoned upload stays in incoming/ (the panel starts again from the first byte).
UPLOAD_DAYS = 7
#: Count-update batch IDs remembered per fingerprint, so a retried update adds once.
REMEMBERED_BATCHES = 64
#: The most repeats one count update may carry (bug_reporter kMaximumCountBatch).
MAXIMUM_COUNT = 10_000


class StoreError(Exception):
    def __init__(self, code: str, status: int) -> None:
        super().__init__(code)
        self.code = code
        self.status = status


def _now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _timestamp(value: object) -> datetime.datetime | None:
    """A `YYYY-MM-DDTHH:MM:SSZ` time, or None for anything else."""
    if not isinstance(value, str) or not validate.TIMESTAMP.fullmatch(value):
        return None
    try:
        return datetime.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    except ValueError:
        return None


def write_atomic(path: Path, data: bytes, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        if os.path.exists(temporary):
            os.unlink(temporary)
        raise


class Store:
    def __init__(self, root: Path) -> None:
        self.root = root
        # The receiver's threads read, change and write fingerprint records.
        self._fingerprint_lock = threading.Lock()
        for name in ("incoming", "reports", "status", "fingerprints", "transcripts"):
            (root / name).mkdir(parents=True, exist_ok=True)

    # -- uploads ------------------------------------------------------------------------

    def _check_ids(self, report_id: str, name: str | None = None) -> None:
        if not validate.REPORT_ID.fullmatch(report_id):
            raise StoreError("REPORT_INBOX.REPORT_ID_INVALID", 400)
        if name is not None and name not in validate.FILE_LIMITS:
            raise StoreError("REPORT_INBOX.FILE_NAME_INVALID", 400)

    def completed(self, report_id: str) -> bool:
        return (self.root / "reports" / report_id / "report.json").is_file()

    def received_bytes(self, report_id: str, name: str) -> int | None:
        self._check_ids(report_id, name)
        for base in (self.root / "reports" / report_id, self.root / "incoming" / report_id):
            if (base / name).is_file():
                return (base / name).stat().st_size
        return None

    def put_range(self, report_id: str, name: str, content_range: str | None, body: bytes) -> int:
        """Appends one chunk; returns the bytes now held. A chunk must start where the file ends."""
        self._check_ids(report_id, name)
        parsed = validate.parse_content_range(content_range)
        if parsed is None or parsed[1] - parsed[0] + 1 != len(body) or len(body) > 1024 * 1024:
            raise StoreError("REPORT_INBOX.RANGE_INVALID", 400)
        start, end, total = parsed
        if total > validate.FILE_LIMITS[name]:
            raise StoreError("REPORT_INBOX.FILE_TOO_LARGE", 413)
        if self.completed(report_id):
            held = self.received_bytes(report_id, name) or 0
            if end < held:
                return held  # a retry of a chunk the inbox already has
            raise StoreError("REPORT_INBOX.REPORT_COMPLETE", 409)
        path = self.root / "incoming" / report_id / name
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        held = path.stat().st_size if path.exists() else 0
        if end < held:
            return held  # already held: a retry after a lost answer changes nothing
        if start != held:
            raise StoreError("REPORT_INBOX.RANGE_NOT_CONTIGUOUS", 416)
        with open(path, "ab") as handle:
            handle.write(body)
            handle.flush()
            os.fsync(handle.fileno())
        return held + len(body)

    def complete(self, report_id: str, manifest_bytes: bytes) -> bool:
        """Moves a fully uploaded report into reports/. True when it is new, False when it was already there."""
        self._check_ids(report_id)
        if len(manifest_bytes) > validate.MANIFEST_LIMIT:
            raise StoreError("REPORT_INBOX.MANIFEST_TOO_LARGE", 413)
        try:
            manifest = validate.validate_manifest(json.loads(manifest_bytes), report_id)
        except (ValueError, validate.ValidationError) as error:
            field = error.field if isinstance(error, validate.ValidationError) else "json"
            raise StoreError(f"REPORT_INBOX.MANIFEST_INVALID.{field.upper().replace('.', '_')}", 400) from None
        final = self.root / "reports" / report_id
        if self.completed(report_id):
            return False
        incoming = self.root / "incoming" / report_id
        for item in manifest["files"]:
            path = incoming / item["name"]
            if not path.is_file() or path.stat().st_size != item["bytes"]:
                raise StoreError("REPORT_INBOX.BUNDLE_INCOMPLETE", 409)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != item["sha256"]:
                # A corrupt file is discarded so the panel uploads it again.
                path.unlink()
                raise StoreError("REPORT_INBOX.BUNDLE_INCOMPLETE", 409)
        # Built inside reports/ and copied, not moved: new entries there inherit the
        # directory's default ACL (the agent's read access); moved ones would not.
        staging = self.root / "reports" / f".staging-{report_id}"
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir()
        os.chmod(staging, 0o750)
        for item in manifest["files"]:
            write_atomic(staging / item["name"], (incoming / item["name"]).read_bytes())
        write_atomic(staging / "report.json", json.dumps(manifest, indent=2).encode() + b"\n")
        os.replace(staging, final)
        shutil.rmtree(incoming, ignore_errors=True)
        if not (self.root / "status" / f"{report_id}.json").exists():
            self.set_status(report_id, "received", None, None)
        if manifest["kind"] not in validate.MANUAL_KINDS:
            self._note_fingerprint(manifest["fingerprint"], report_id, manifest["created_at"], 0)
        return True

    def manifest(self, report_id: str) -> dict:
        self._check_ids(report_id)
        return json.loads((self.root / "reports" / report_id / "report.json").read_text())

    # -- repeats ------------------------------------------------------------------------

    def _note_fingerprint(self, fingerprint: str, report_id: str | None, seen_at: str, count: int,
                          batch: str | None = None) -> dict:
        with self._fingerprint_lock:
            path = self.root / "fingerprints" / f"{fingerprint}.json"
            record = json.loads(path.read_text()) if path.exists() else {
                "fingerprint": fingerprint, "reports": [], "repeats": 0, "first_seen": seen_at, "last_seen": seen_at}
            batches = record.setdefault("batches", [])
            if batch is not None and batch in batches:
                return record  # a retried update after a lost answer: already counted
            if report_id and report_id not in record["reports"]:
                record["reports"].append(report_id)
            if batch is not None:
                record["batches"] = (batches + [batch])[-REMEMBERED_BATCHES:]
            record["repeats"] += count
            record["first_seen"] = min(record["first_seen"], seen_at)
            record["last_seen"] = max(record["last_seen"], seen_at)
            write_atomic(path, json.dumps(record, indent=2).encode() + b"\n")
            return record

    def add_count(self, fingerprint: str, count: object, batch: object = None) -> dict:
        """Adds a count update's repeats once per `batch`; an update without one (older panels) always adds."""
        if not validate.FINGERPRINT.fullmatch(fingerprint):
            raise StoreError("REPORT_INBOX.FINGERPRINT_INVALID", 400)
        if not isinstance(count, int) or isinstance(count, bool) or not 1 <= count <= MAXIMUM_COUNT:
            raise StoreError("REPORT_INBOX.COUNT_INVALID", 400)
        if batch is not None and (not isinstance(batch, str) or not validate.BATCH_ID.fullmatch(batch)):
            raise StoreError("REPORT_INBOX.BATCH_INVALID", 400)
        return self._note_fingerprint(fingerprint, None, _now(), count, batch)

    def fingerprint(self, fingerprint: str) -> dict | None:
        if not validate.FINGERPRINT.fullmatch(fingerprint):
            raise StoreError("REPORT_INBOX.FINGERPRINT_INVALID", 400)
        path = self.root / "fingerprints" / f"{fingerprint}.json"
        return json.loads(path.read_text()) if path.exists() else None

    # -- statuses -----------------------------------------------------------------------

    def set_status(self, report_id: str, status: str, pr: int | None, reason: str | None) -> None:
        """The agent's record of a report: a status, its PR, and a private one-line reason."""
        self._check_ids(report_id)
        if status not in validate.STATUSES or status == "queued":
            raise StoreError("REPORT_INBOX.STATUS_INVALID", 400)
        if pr is not None and (not isinstance(pr, int) or pr <= 0):
            raise StoreError("REPORT_INBOX.PR_INVALID", 400)
        if reason is not None and (len(reason) > 200 or not validate.description_ok(reason) or "\n" in reason):
            raise StoreError("REPORT_INBOX.REASON_INVALID", 400)
        if not self.completed(report_id):
            raise StoreError("REPORT_INBOX.UNKNOWN_REPORT", 404)
        record = {"report_id": report_id, "status": status, "pr": pr, "reason": reason, "updated_at": _now()}
        write_atomic(self.root / "status" / f"{report_id}.json", json.dumps(record, indent=2).encode() + b"\n",
                     mode=0o660)

    def status(self, report_id: str) -> dict | None:
        self._check_ids(report_id)
        if not self.completed(report_id):
            return None
        record = self._read_status(self.root / "status" / f"{report_id}.json")
        if not isinstance(record, dict):
            return {"report_id": report_id, "status": "received", "pr": None}
        status = record.get("status") if record.get("status") in validate.STATUSES else "received"
        pr = record.get("pr") if isinstance(record.get("pr"), int) and record.get("pr") > 0 else None
        updated_at = record.get("updated_at")
        return {"report_id": report_id, "status": status, "pr": pr,
                "updated_at": updated_at if _timestamp(updated_at) else None}

    @staticmethod
    def _read_status(path: Path) -> object:
        """Reads a status record the agent wrote: a regular file of at most STATUS_LIMIT bytes, never a link or FIFO."""
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        except OSError:
            return None
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode) or info.st_size > STATUS_LIMIT:
                return None
            data = os.read(descriptor, STATUS_LIMIT + 1)
            return json.loads(data) if len(data) <= STATUS_LIMIT else None
        except (OSError, ValueError):
            return None
        finally:
            os.close(descriptor)

    def report_ids(self) -> list[str]:
        return sorted(p.name for p in (self.root / "reports").iterdir()
                      if validate.REPORT_ID.fullmatch(p.name) and (p / "report.json").is_file())

    # -- retention (decision 9) ------------------------------------------------------------

    def apply_retention(self, now: datetime.datetime, keep_days: int = 90) -> list[str]:
        """Deletes reports closed more than `keep_days` ago, and the audio of closed reports that has a transcript."""
        removed = []
        for report_id in self.report_ids():
            status = self.status(report_id) or {}
            closed = _timestamp(status.get("updated_at"))
            # A status the agent wrote with a malformed time is not closed: it never stops the others.
            if status.get("status") not in validate.TERMINAL_STATUSES or closed is None:
                continue
            audio = self.root / "reports" / report_id / "description.wav"
            if audio.exists() and (self.root / "transcripts" / f"{report_id}.txt").exists():
                audio.unlink()
            if now - closed > datetime.timedelta(days=keep_days):
                shutil.rmtree(self.root / "reports" / report_id)
                (self.root / "transcripts" / f"{report_id}.txt").unlink(missing_ok=True)
                (self.root / "status" / f"{report_id}.json").unlink(missing_ok=True)
                removed.append(report_id)
        return removed

    def prune_leftovers(self, now: datetime.datetime, keep_days: int = 90, upload_days: int = UPLOAD_DAYS
                        ) -> dict[str, int]:
        """Deletes uploads abandoned for `upload_days`, and fingerprints whose reports are gone and unseen for `keep_days`."""
        uploads = 0
        stale = now.timestamp() - upload_days * 86400
        leftovers = [p for p in (self.root / "incoming").iterdir() if validate.REPORT_ID.fullmatch(p.name)]
        leftovers += [p for p in (self.root / "reports").iterdir() if p.name.startswith(".staging-rpt-")]
        for directory in leftovers:
            if not directory.is_dir() or directory.is_symlink():
                continue
            newest = max([directory.stat().st_mtime] + [f.stat().st_mtime for f in directory.iterdir()])
            if newest < stale:
                shutil.rmtree(directory)
                uploads += 1
        fingerprints = 0
        for path in (self.root / "fingerprints").glob("*.json"):
            try:
                record = json.loads(path.read_text())
                last_seen = datetime.datetime.strptime(record["last_seen"], "%Y-%m-%dT%H:%M:%SZ").replace(
                    tzinfo=datetime.timezone.utc)
                reports = list(record["reports"])
            except (OSError, ValueError, KeyError, TypeError):
                continue
            if now - last_seen > datetime.timedelta(days=keep_days) and not any(
                    isinstance(r, str) and validate.REPORT_ID.fullmatch(r) and (self.root / "reports" / r).exists()
                    for r in reports):
                path.unlink()
                fingerprints += 1
        return {"uploads_removed": uploads, "fingerprints_removed": fingerprints}
