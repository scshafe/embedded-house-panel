"""The inbox's files: uploads, completion, counts, statuses and retention."""

from __future__ import annotations

import datetime
import json
import shutil
import stat
import subprocess
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
