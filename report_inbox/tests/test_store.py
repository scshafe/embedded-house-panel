"""The inbox's files: uploads, completion, counts, statuses and retention."""

from __future__ import annotations

import datetime
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from unittest import mock
from pathlib import Path

from support import FINGERPRINT, REPORT_ID, bundle

from bhp_report_inbox.store import Store, StoreError


class StoreTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.store = Store(Path(self.temporary.name))

    def upload(self, files: dict[str, bytes], report_id: str = REPORT_ID) -> None:
        for name, data in files.items():
            for start in range(0, len(data), 10):
                chunk = data[start:start + 10]
                self.store.put_range(report_id, name, f"bytes {start}-{start + len(chunk) - 1}/{len(data)}", chunk)

    def assert_refused(self, code: str, call) -> None:
        with self.assertRaises(StoreError) as raised:
            call()
        self.assertEqual(raised.exception.code, code)

    def test_upload_resume_and_complete(self) -> None:
        manifest, files = bundle()
        data = files["logs.jsonl.gz"]
        self.assertIsNone(self.store.received_bytes(REPORT_ID, "logs.jsonl.gz"))
        self.assertEqual(self.store.put_range(REPORT_ID, "logs.jsonl.gz", f"bytes 0-9/{len(data)}", data[:10]), 10)
        self.assertEqual(self.store.put_range(REPORT_ID, "logs.jsonl.gz", f"bytes 0-9/{len(data)}", data[:10]), 10,
                         "a retried chunk changes nothing")
        self.assert_refused("REPORT_INBOX.RANGE_NOT_CONTIGUOUS", lambda: self.store.put_range(
            REPORT_ID, "logs.jsonl.gz", f"bytes 20-29/{len(data)}", data[20:30]))
        self.assert_refused("REPORT_INBOX.RANGE_INVALID", lambda: self.store.put_range(
            REPORT_ID, "logs.jsonl.gz", f"bytes 10-19/{len(data)}", data[10:15]))
        self.assert_refused("REPORT_INBOX.FILE_TOO_LARGE", lambda: self.store.put_range(
            REPORT_ID, "logs.jsonl.gz", "bytes 10-19/9999999", data[10:20]))
        self.assert_refused("REPORT_INBOX.FILE_NAME_INVALID", lambda: self.store.put_range(
            REPORT_ID, "../etc", "bytes 0-0/1", b"x"))
        self.assert_refused("REPORT_INBOX.BUNDLE_INCOMPLETE", lambda: self.store.complete(
            REPORT_ID, json.dumps(manifest).encode()))
        for start in range(10, len(data), 10):
            chunk = data[start:start + 10]
            self.store.put_range(REPORT_ID, "logs.jsonl.gz", f"bytes {start}-{start + len(chunk) - 1}/{len(data)}", chunk)
        self.assertTrue(self.store.complete(REPORT_ID, json.dumps(manifest).encode()))
        self.assertFalse(self.store.complete(REPORT_ID, json.dumps(manifest).encode()), "complete is idempotent")
        report = Path(self.temporary.name) / "reports" / REPORT_ID
        self.assertEqual((report / "logs.jsonl.gz").read_bytes(), data)
        self.assertEqual(stat.S_IMODE((report / "logs.jsonl.gz").stat().st_mode), 0o640)
        self.assertFalse((Path(self.temporary.name) / "incoming" / REPORT_ID).exists())
        self.assertEqual(self.store.status(REPORT_ID)["status"], "received")
        self.assertEqual(self.store.received_bytes(REPORT_ID, "logs.jsonl.gz"), len(data),
                         "a completed file reports its full size, so the panel sends nothing more")
        self.assertEqual(self.store.fingerprint(FINGERPRINT)["reports"], [REPORT_ID])

    @unittest.skipUnless(shutil.which("setfacl") and shutil.which("getfacl"), "needs the acl tools")
    def test_the_agents_default_acl_reaches_a_completed_report(self) -> None:
        # The setup doc gives the agent a default ACL on reports/; a completed report must inherit it.
        reports = Path(self.temporary.name) / "reports"
        if subprocess.run(["setfacl", "-d", "-m", "u:65534:rX", str(reports)], capture_output=True).returncode != 0:
            self.skipTest("this file system takes no ACLs")
        manifest, files = bundle()
        self.upload(files)
        self.store.complete(REPORT_ID, json.dumps(manifest).encode())
        for path in (reports / REPORT_ID, reports / REPORT_ID / "logs.jsonl.gz", reports / REPORT_ID / "report.json"):
            acl = subprocess.run(["getfacl", "-n", "-p", str(path)], capture_output=True, text=True).stdout
            self.assertRegex(acl, r"user:65534:r-[x-]", f"{path.name} carries the named user's entry")

    def test_a_corrupt_file_is_sent_again(self) -> None:
        manifest, files = bundle()
        corrupt = bytes([files["logs.jsonl.gz"][0] ^ 1]) + files["logs.jsonl.gz"][1:]
        self.upload({"logs.jsonl.gz": corrupt})
        self.assert_refused("REPORT_INBOX.BUNDLE_INCOMPLETE", lambda: self.store.complete(
            REPORT_ID, json.dumps(manifest).encode()))
        self.assertIsNone(self.store.received_bytes(REPORT_ID, "logs.jsonl.gz"), "the corrupt file is discarded")

    def test_invalid_manifests_name_the_field(self) -> None:
        manifest, files = bundle()
        self.upload(files)
        manifest["component"] = "Bad Name"
        self.assert_refused("REPORT_INBOX.MANIFEST_INVALID.COMPONENT", lambda: self.store.complete(
            REPORT_ID, json.dumps(manifest).encode()))
        self.assert_refused("REPORT_INBOX.MANIFEST_INVALID.JSON", lambda: self.store.complete(REPORT_ID, b"{"))

    def test_counts_statuses_and_retention(self) -> None:
        manifest, files = bundle()
        self.upload(files)
        self.store.complete(REPORT_ID, json.dumps(manifest).encode())
        self.assertEqual(self.store.add_count(FINGERPRINT, 3)["repeats"], 3)
        self.assert_refused("REPORT_INBOX.COUNT_INVALID", lambda: self.store.add_count(FINGERPRINT, 0))
        self.store.set_status(REPORT_ID, "fix_in_pr", 81, "skip handler ignored a 404")
        self.assertEqual(self.store.status(REPORT_ID)["pr"], 81)
        self.assert_refused("REPORT_INBOX.STATUS_INVALID", lambda: self.store.set_status(REPORT_ID, "deployed", None, None))
        self.assert_refused("REPORT_INBOX.REASON_INVALID", lambda: self.store.set_status(REPORT_ID, "merged", 81, "a\nb"))
        self.assert_refused("REPORT_INBOX.UNKNOWN_REPORT", lambda: self.store.set_status(
            "rpt-" + "f" * 32, "merged", None, None))
        self.store.set_status(REPORT_ID, "installed", 81, None)
        now = datetime.datetime.now(datetime.timezone.utc)
        self.assertEqual(self.store.apply_retention(now, 90), [], "a recently closed report is kept")
        self.assertEqual(self.store.apply_retention(now + datetime.timedelta(days=91), 90), [REPORT_ID])
        self.assertFalse((Path(self.temporary.name) / "reports" / REPORT_ID).exists())

    def test_audio_goes_once_transcribed_and_closed(self) -> None:
        manifest, files = bundle("voice", audio=b"RIFF0000WAVE")
        self.upload(files)
        self.store.complete(REPORT_ID, json.dumps(manifest).encode())
        (Path(self.temporary.name) / "transcripts" / f"{REPORT_ID}.txt").write_text("skip did nothing")
        self.store.set_status(REPORT_ID, "wont_fix", None, "works as designed")
        self.store.apply_retention(datetime.datetime.now(datetime.timezone.utc), 90)
        self.assertFalse((Path(self.temporary.name) / "reports" / REPORT_ID / "description.wav").exists())
        self.assertTrue((Path(self.temporary.name) / "reports" / REPORT_ID / "report.json").exists())


    def test_a_retried_count_update_adds_once(self) -> None:
        batch = "a" * 32
        self.assertEqual(self.store.add_count(FINGERPRINT, 3, batch)["repeats"], 3)
        self.assertEqual(self.store.add_count(FINGERPRINT, 3, batch)["repeats"], 3, "the same batch adds once")
        self.assertEqual(self.store.add_count(FINGERPRINT, 2, "b" * 32)["repeats"], 5)
        self.assertEqual(self.store.add_count(FINGERPRINT, 1)["repeats"], 6, "an update without a batch still adds")
        self.assert_refused("REPORT_INBOX.BATCH_INVALID", lambda: self.store.add_count(FINGERPRINT, 1, "A" * 32))
        self.assert_refused("REPORT_INBOX.COUNT_INVALID", lambda: self.store.add_count(FINGERPRINT, 10_001, "c" * 32))
        for n in range(70):
            self.store.add_count(FINGERPRINT, 1, f"{n:032x}")
        self.assertEqual(len(self.store.fingerprint(FINGERPRINT)["batches"]), 64, "only recent batches are kept")

    def test_a_malformed_closing_time_never_stops_retention(self) -> None:
        root = Path(self.temporary.name)
        later = "rpt-" + "d" * 32
        for report_id in (REPORT_ID, later):
            manifest, files = bundle(report_id=report_id)
            self.upload(files, report_id)
            self.store.complete(report_id, json.dumps(manifest).encode())
            self.store.set_status(report_id, "installed", 81, None)
        # The agent can write status/ directly; a hand-edited time must not break the others.
        record = json.loads((root / "status" / f"{REPORT_ID}.json").read_text())
        for bad in ("yesterday", "2026-13-45T99:99:99Z", 17):
            record["updated_at"] = bad
            (root / "status" / f"{REPORT_ID}.json").write_text(json.dumps(record))
            self.assertIsNone(self.store.status(REPORT_ID)["updated_at"])
            now = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=91)
            self.assertEqual(self.store.apply_retention(now, 90), [later] if bad == "yesterday" else [])
        self.assertTrue((root / "reports" / REPORT_ID).exists(), "the malformed one is kept, not deleted")

    def test_a_status_file_must_be_a_small_regular_file(self) -> None:
        manifest, files = bundle()
        self.upload(files)
        self.store.complete(REPORT_ID, json.dumps(manifest).encode())
        path = Path(self.temporary.name) / "status" / f"{REPORT_ID}.json"
        target = Path(self.temporary.name) / "elsewhere.json"
        target.write_text(json.dumps({"status": "merged", "pr": 9}))
        planted = {
            "a link": lambda: path.symlink_to(target),
            "a FIFO": lambda: os.mkfifo(path),
            "a directory": path.mkdir,
            "an oversized file": lambda: path.write_text(json.dumps({"status": "merged", "pad": "x" * 5000})),
        }
        for kind, plant in planted.items():
            if path.is_dir() and not path.is_symlink():
                path.rmdir()
            elif path.is_symlink() or path.exists():
                path.unlink()
            plant()
            self.assertEqual(self.store.status(REPORT_ID)["status"], "received", kind)

    def test_retention_clears_abandoned_uploads_and_old_fingerprints(self) -> None:
        root = Path(self.temporary.name)
        manifest, files = bundle()
        self.upload(files)
        self.store.complete(REPORT_ID, json.dumps(manifest).encode())
        self.store.set_status(REPORT_ID, "installed", 81, None)
        abandoned, fresh = "rpt-" + "a" * 32, "rpt-" + "b" * 32
        for report_id in (abandoned, fresh):
            self.store.put_range(report_id, "logs.jsonl.gz", "bytes 0-0/2", b"x")
        staging = root / "reports" / f".staging-{abandoned}"
        staging.mkdir()
        old = (datetime.datetime.now() - datetime.timedelta(days=8)).timestamp()
        for path in (root / "incoming" / abandoned, root / "incoming" / abandoned / "logs.jsonl.gz", staging):
            os.utime(path, (old, old))
        orphan = "f" * 64
        self.store.add_count(orphan, 1)
        now = datetime.datetime.now(datetime.timezone.utc)
        self.assertEqual(self.store.prune_leftovers(now), {"uploads_removed": 2, "fingerprints_removed": 0})
        self.assertFalse((root / "incoming" / abandoned).exists())
        self.assertFalse(staging.exists())
        self.assertTrue((root / "incoming" / fresh).exists(), "an upload still in progress stays")
        later = now + datetime.timedelta(days=91)
        self.assertEqual(self.store.prune_leftovers(later)["fingerprints_removed"], 1,
                         "an old fingerprint with no report goes; one whose report remains stays")
        self.assertIsNotNone(self.store.fingerprint(FINGERPRINT))
        self.store.apply_retention(later, 90)
        self.assertEqual(self.store.prune_leftovers(later)["fingerprints_removed"], 1)
        self.assertIsNone(self.store.fingerprint(FINGERPRINT))


    def test_an_upload_that_vanishes_during_retention_is_skipped(self) -> None:
        root = Path(self.temporary.name)
        for report_id in ("rpt-" + "a" * 32, "rpt-" + "b" * 32):
            self.store.put_range(report_id, "logs.jsonl.gz", "bytes 0-0/2", b"x")
            old = (datetime.datetime.now() - datetime.timedelta(days=8)).timestamp()
            for path in (root / "incoming" / report_id, root / "incoming" / report_id / "logs.jsonl.gz"):
                os.utime(path, (old, old))
        real = shutil.rmtree
        calls = []

        def vanishing(path, *args, **kwargs):  # the first one is completed by the receiver meanwhile
            calls.append(path)
            if len(calls) == 1:
                real(path)
                raise FileNotFoundError(path)
            return real(path, *args, **kwargs)

        with mock.patch("bhp_report_inbox.store.shutil.rmtree", side_effect=vanishing):
            result = self.store.prune_leftovers(datetime.datetime.now(datetime.timezone.utc))
        self.assertEqual(result["uploads_removed"], 1)
        self.assertFalse(any((root / "incoming").iterdir()))

if __name__ == "__main__":
    unittest.main()
