#!/usr/bin/env python3
"""bhp-reporter and bhp-log-tee end to end, with a loopback fake inbox.

A service's output runs through bhp-log-tee into the reporter, which turns a
fault() record and an exit notice into reports and sends them to the fake
inbox. The tee also copies every line to its own stdout untouched. No network
beyond 127.0.0.1.
"""
from __future__ import annotations

import gzip
import hashlib
import http.server
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time

REPORTER, TEE = sys.argv[1], sys.argv[2]
failures = 0


def expect(value: bool, label: str) -> None:
    global failures
    if not value:
        failures += 1
        print(f"FAIL: {label}", file=sys.stderr)


class Inbox(http.server.BaseHTTPRequestHandler):
    files: dict[tuple[str, str], bytes] = {}
    completed: dict[str, dict] = {}

    def reply(self, status: int, body: dict) -> None:
        payload = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:  # noqa: N802
        match = re.fullmatch(r"/v1/reports/(rpt-[0-9a-f]{32})/files/([a-z.]+)", self.path)
        if match:
            key = (match[1], match[2])
            return self.reply(200, {"received_bytes": len(self.files[key])}) if key in self.files else self.reply(404, {})
        if self.path.startswith("/v1/reports/status?ids="):
            ids = self.path.split("=", 1)[1].split(",")
            return self.reply(200, {"reports": [{"report_id": i, "status": "received", "pr": None}
                                                for i in ids if i in self.completed]})
        self.reply(404, {})

    def do_PUT(self) -> None:  # noqa: N802
        match = re.fullmatch(r"/v1/reports/(rpt-[0-9a-f]{32})/files/([a-z.]+)", self.path)
        body = self.rfile.read(int(self.headers["Content-Length"]))
        start = int(re.fullmatch(r"bytes (\d+)-\d+/\d+", self.headers["Content-Range"])[1])
        key = (match[1], match[2])
        held = self.files.get(key, b"")
        if start != len(held):
            return self.reply(416, {})
        self.files[key] = held + body
        self.reply(200, {"received_bytes": len(self.files[key])})

    def do_POST(self) -> None:  # noqa: N802
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        match = re.fullmatch(r"/v1/reports/(rpt-[0-9a-f]{32})/complete", self.path)
        if match:
            manifest = json.loads(body)
            for f in manifest["files"]:
                held = self.files.get((match[1], f["name"]), b"")
                if len(held) != f["bytes"] or hashlib.sha256(held).hexdigest() != f["sha256"]:
                    return self.reply(409, {})
            self.completed[match[1]] = manifest
            return self.reply(201, {"status": "received"})
        if re.fullmatch(r"/v1/fingerprints/[0-9a-f]{64}/count", self.path):
            return self.reply(200, {"count": json.loads(body)["count"]})
        self.reply(404, {})

    def log_message(self, *args: object) -> None:
        pass


def main() -> int:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Inbox)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory() as root:
        os.chmod(root, 0o700)
        ingest = os.path.join(root, "ingest.sock")
        with open(os.path.join(root, "image-version"), "w", encoding="utf-8") as version:
            version.write("board_id=frdm-imx8mp\ncommit=abc123\n")
        def start() -> subprocess.Popen:
            started = subprocess.Popen(
                [REPORTER, "--ui-socket", os.path.join(root, "ui.sock"), "--ui-uid", "65534", "--ingest-socket", ingest,
                 "--ingest-uids", str(os.getuid()), "--outbox-dir", os.path.join(root, "outbox"),
                 "--fault-dir", os.path.join(root, "faults"),
                 "--inbox-url", f"http://127.0.0.1:{server.server_address[1]}",
                 "--build-info", os.path.join(root, "image-version")],
                stderr=subprocess.PIPE, text=True)
            for _ in range(100):
                if os.path.exists(ingest) or started.poll() is not None:
                    break
                time.sleep(0.05)
            return started

        # A reporter killed outright leaves its sockets; the next one binds again instead of exiting.
        killed = start()
        killed.kill()
        killed.communicate(timeout=10)
        expect(os.path.exists(ingest), "a killed reporter leaves a stale ingest socket")
        reporter = start()
        time.sleep(0.5)
        expect(reporter.poll() is None, "a reporter restarted after a kill replaces the stale sockets and runs")
        second = start()
        expect(second.wait(timeout=10) != 0, "a second reporter never takes a live reporter's sockets")
        second.communicate(timeout=10)
        fault = json.dumps({"timestamp": "2026-11-08T12:00:00Z", "level": "error", "message": "queue order lost",
                            "service": "music", "trace_id": "t-9", "error_code": "MUSIC.INTERNAL",
                            "error_message": "x", "stack_trace": "", "reportable": "true",
                            "location": "music_service.cpp:tick"})
        service_output = "\n".join(['{"level":"info","trace_id":"t-1"}', "plain text", fault]) + "\n"
        tee = subprocess.run([TEE, "--socket", ingest, "--service", "bhp-music-control"], input=service_output,
                             capture_output=True, text=True, timeout=10)
        expect(tee.returncode == 0 and tee.stdout == service_output, "the tee copies every line to stdout untouched")
        notice = subprocess.run([TEE, "--socket", ingest, "--service", "bhp-panel-ui", "--exit-status", "0",
                                 "--signal", "11", "--restarting"], capture_output=True, timeout=10)
        expect(notice.returncode == 0, "the restart wrapper's exit notice is sent")
        deadline = time.time() + 15
        while time.time() < deadline and len(Inbox.completed) < 2:
            time.sleep(0.1)
        reporter.terminate()
        _, errors = reporter.communicate(timeout=10)
        if os.environ.get("BHP_TEST_VERBOSE"):
            print(errors, file=sys.stderr)
        kinds = {m["kind"]: m for m in Inbox.completed.values()}
        expect(set(kinds) == {"fault", "crash"}, f"a fault and a crash reach the inbox (got {sorted(kinds)})")
        if "fault" in kinds:
            manifest = kinds["fault"]
            expect(manifest["component"] == "bhp-music-control" and manifest["error_code"] == "MUSIC.INTERNAL"
                   and manifest["build"]["commit"] == "abc123", "the fault's manifest")
            logs = gzip.decompress(Inbox.files[(manifest["report_id"], "logs.jsonl.gz")]).decode()
            expect("plain text" in logs and '"t-1"' in logs, "the logs hold the service's lines")
        if "crash" in kinds:
            expect(kinds["crash"]["error_code"] == "PROCESS.SIGNAL_11" and kinds["crash"]["component"] == "bhp-panel-ui",
                   "the crash's manifest")
        expect("queue order lost" not in errors and "plain text" not in errors,
               "the reporter's own log carries codes, never the lines")
        expect(set(os.listdir(os.path.join(root, "outbox"))) <= {"outbox.json", "outbox.lock"},
               "received bundles leave the panel")
        usage = subprocess.run([TEE, "--socket", ingest, "--service", "Bad Name"], capture_output=True)
        expect(usage.returncode == 2, "a bad service name is a usage error")
    server.shutdown()
    if failures == 0:
        print("bug_reporter_daemon_test passed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
