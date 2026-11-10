#!/usr/bin/env python3
"""The panel's reporter against the PC's real receiver (report_inbox), over loopback.

bhp-reporter and bhp-log-tee on one side, `python3 -m bhp_report_inbox serve`
on the other: a fault and a typed-free crash notice must land in the inbox's
reports/ with their files and hashes checked, and a status set by the agent's
command must come back. This pins the v1 API on both sides at once.
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPORTER, TEE, INBOX = sys.argv[1], sys.argv[2], sys.argv[3]
failures = 0


def expect(value: bool, label: str) -> None:
    global failures
    if not value:
        failures += 1
        print(f"FAIL: {label}", file=sys.stderr)


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def main() -> int:
    environment = dict(os.environ, PYTHONPATH=INBOX)
    with tempfile.TemporaryDirectory() as root:
        os.chmod(root, 0o700)
        port = free_port()
        inbox_root = os.path.join(root, "inbox")
        receiver = subprocess.Popen([sys.executable, "-m", "bhp_report_inbox", "--root", inbox_root, "serve",
                                     "--address", "127.0.0.1", "--port", str(port), "--insecure-loopback-for-tests"],
                                    env=environment, stderr=subprocess.PIPE, text=True)
        ingest = os.path.join(root, "ingest.sock")
        reporter = subprocess.Popen(
            [REPORTER, "--ui-socket", os.path.join(root, "ui.sock"), "--ui-uid", "65534", "--ingest-socket", ingest,
             "--ingest-uids", str(os.getuid()), "--outbox-dir", os.path.join(root, "outbox"),
             "--fault-dir", os.path.join(root, "faults"), "--inbox-url", f"http://127.0.0.1:{port}"],
            stderr=subprocess.PIPE, text=True)
        try:
            for _ in range(100):
                if os.path.exists(ingest):
                    break
                time.sleep(0.05)
            fault = json.dumps({"level": "error", "message": "queue order lost", "trace_id": "t-9",
                                "error_code": "MUSIC.INTERNAL", "reportable": "true",
                                "location": "music_service.cpp:tick"})
            subprocess.run([TEE, "--socket", ingest, "--service", "bhp-music-control"], input=fault + "\n",
                           capture_output=True, text=True, timeout=10, check=True)
            subprocess.run([TEE, "--socket", ingest, "--service", "bhp-panel-ui", "--exit-status", "0",
                            "--signal", "11", "--restarting"], capture_output=True, timeout=10, check=True)
            reports = Path(inbox_root) / "reports"

            def completed() -> list[Path]:
                # Only finished reports: a `.staging-<id>` directory is a completion in progress.
                return sorted(path for path in reports.iterdir()
                              if path.name.startswith("rpt-") and (path / "report.json").is_file()) \
                    if reports.exists() else []

            deadline = time.time() + 20
            while time.time() < deadline and len(completed()) < 2:
                time.sleep(0.1)
            landed = completed()
            expect(len(landed) == 2, f"two reports land in the inbox (got {len(landed)})")
            kinds = {}
            for report in landed:
                manifest = json.loads((report / "report.json").read_text())
                kinds[manifest["kind"]] = report.name
                expect((report / "logs.jsonl.gz").stat().st_size == manifest["files"][0]["bytes"], "the logs arrived whole")
            expect(set(kinds) == {"fault", "crash"}, "a fault and a crash")
            if "fault" in kinds:
                done = subprocess.run([sys.executable, "-m", "bhp_report_inbox", "--root", inbox_root, "set-status",
                                       kinds["fault"], "fix_in_pr", "--pr", "81"], env=environment)
                expect(done.returncode == 0, "the agent's set-status")
                listed = subprocess.run([sys.executable, "-m", "bhp_report_inbox", "--root", inbox_root, "list", "--new"],
                                        env=environment, capture_output=True, text=True)
                expect([row["kind"] for row in json.loads(listed.stdout)] == ["crash"], "only the crash is still new")
        finally:
            reporter.terminate()
            receiver.terminate()
            reporter.communicate(timeout=10)
            _, receiver_log = receiver.communicate(timeout=10)
        expect("queue order lost" not in receiver_log and "music_service.cpp" not in receiver_log,
               "the receiver's log carries codes and IDs, never report content")
    if failures == 0:
        print("bug_reporter_inbox_contract_test passed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
