"""The report inbox's HTTP receiver: the v1 API of bug_reporter/inbox_client.h.

It listens on the PC's tailnet address with TLS from `tailscale cert`, and
admits a request only from a tailnet node carrying the panel's tag, asked of
`tailscale whois` per peer address. It writes reports into the store and runs
the optional transcription command for spoken descriptions. It never logs a
report's content: its log lines carry codes, report IDs and sizes.
"""

from __future__ import annotations

import http.server
import json
import queue
import re
import shlex
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path
from typing import Callable

from . import validate
from .store import Store, StoreError

FILE_PATH = re.compile(r"/v1/reports/(rpt-[0-9a-f]{32})/files/([a-z.]+)")
COMPLETE_PATH = re.compile(r"/v1/reports/(rpt-[0-9a-f]{32})/complete")
COUNT_PATH = re.compile(r"/v1/fingerprints/([0-9a-f]{64})/count")
MAXIMUM_BODY = 2 * 1024 * 1024
MAXIMUM_STATUS_IDS = 20


def tailscale_whois_authenticator(tailscale: str, allowed_tag: str, cache_seconds: float = 60.0
                                  ) -> Callable[[str], bool]:
    """Admits a peer address whose tailnet node carries `allowed_tag`."""
    cache: dict[str, tuple[float, bool]] = {}
    lock = threading.Lock()

    def check(address: str) -> bool:
        with lock:
            cached = cache.get(address)
            if cached and time.monotonic() - cached[0] < cache_seconds:
                return cached[1]
        try:
            result = subprocess.run([tailscale, "whois", "--json", address], capture_output=True, text=True,
                                    timeout=5, check=False)
            node = json.loads(result.stdout).get("Node", {}) if result.returncode == 0 else {}
            allowed = allowed_tag in (node.get("Tags") or [])
        except (OSError, ValueError, subprocess.TimeoutExpired):
            allowed = False
        with lock:
            cache[address] = (time.monotonic(), allowed)
        return allowed

    return check


class Transcriber:
    """Runs the owner's speech-to-text command (decision 4) for each spoken description, one at a time.

    `command` is a template with `{input}` and `{output_base}`; for whisper.cpp:
    `whisper-cli -m MODEL -f {input} -otxt -of {output_base}`, which writes `{output_base}.txt`.
    """

    def __init__(self, store: Store, command: str | None, log: Callable[[str, dict], None]) -> None:
        self.store, self.command, self.log = store, command, log
        self.jobs: queue.Queue[str] = queue.Queue()
        if command:
            threading.Thread(target=self._run, daemon=True).start()

    def submit(self, report_id: str) -> None:
        if self.command and (self.store.root / "reports" / report_id / "description.wav").exists():
            self.jobs.put(report_id)

    def _run(self) -> None:
        while True:
            report_id = self.jobs.get()
            audio = self.store.root / "reports" / report_id / "description.wav"
            base = self.store.root / "transcripts" / report_id
            argv = [part.format(input=str(audio), output_base=str(base)) for part in shlex.split(self.command or "")]
            try:
                result = subprocess.run(argv, capture_output=True, timeout=600, check=False)
                ok = result.returncode == 0 and Path(f"{base}.txt").exists()
            except (OSError, subprocess.TimeoutExpired):
                ok = False
            if ok:
                Path(f"{base}.txt").chmod(0o640)
            self.log("transcription_finished" if ok else "transcription_failed", {"report_id": report_id})


def make_handler(store: Store, authenticate: Callable[[str], bool], transcriber: Transcriber,
                 log: Callable[[str, dict], None]) -> type[http.server.BaseHTTPRequestHandler]:
    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = "bhp-report-inbox/1"
        sys_version = ""

        def _reply(self, status: int, body: dict) -> None:
            payload = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def _admitted(self) -> bool:
            if authenticate(self.client_address[0]):
                return True
            log("peer_refused", {})
            self._reply(403, {"error_code": "REPORT_INBOX.PEER_REFUSED"})
            return False

        def _body(self) -> bytes | None:
            length = self.headers.get("Content-Length")
            if length is None or not length.isdigit() or int(length) > MAXIMUM_BODY:
                self._reply(413, {"error_code": "REPORT_INBOX.BODY_INVALID"})
                return None
            return self.rfile.read(int(length))

        def _fail(self, error: StoreError) -> None:
            log("request_refused", {"error_code": error.code})
            self._reply(error.status, {"error_code": error.code})

        def do_GET(self) -> None:  # noqa: N802 (http.server's name)
            if not self._admitted():
                return
            parsed = urllib.parse.urlsplit(self.path)
            try:
                match = FILE_PATH.fullmatch(parsed.path)
                if match:
                    held = store.received_bytes(match[1], match[2])
                    return self._reply(404, {}) if held is None else self._reply(200, {"received_bytes": held})
                if parsed.path == "/v1/reports/status":
                    ids = urllib.parse.parse_qs(parsed.query).get("ids", [""])[0].split(",")
                    if len(ids) > MAXIMUM_STATUS_IDS:
                        return self._reply(400, {"error_code": "REPORT_INBOX.TOO_MANY_IDS"})
                    reports = []
                    for report_id in ids:
                        if validate.REPORT_ID.fullmatch(report_id):
                            status = store.status(report_id)
                            if status:
                                reports.append({key: status[key] for key in ("report_id", "status", "pr")})
                    return self._reply(200, {"reports": reports})
            except StoreError as error:
                return self._fail(error)
            self._reply(404, {})

        def do_PUT(self) -> None:  # noqa: N802
            if not self._admitted():
                return
            match = FILE_PATH.fullmatch(self.path)
            if not match:
                return self._reply(404, {})
            body = self._body()
            if body is None:
                return
            try:
                held = store.put_range(match[1], match[2], self.headers.get("Content-Range"), body)
            except StoreError as error:
                return self._fail(error)
            self._reply(200, {"received_bytes": held})

        def do_POST(self) -> None:  # noqa: N802
            if not self._admitted():
                return
            body = self._body()
            if body is None:
                return
            try:
                match = COMPLETE_PATH.fullmatch(self.path)
                if match:
                    new = store.complete(match[1], body)
                    if new:
                        log("report_received", {"report_id": match[1]})
                        transcriber.submit(match[1])
                    return self._reply(201 if new else 200, {"status": "received"})
                match = COUNT_PATH.fullmatch(self.path)
                if match:
                    try:
                        count = json.loads(body).get("count")
                    except (ValueError, AttributeError):
                        count = None
                    record = store.add_count(match[1], count)
                    return self._reply(200, {"count": record["repeats"]})
            except StoreError as error:
                return self._fail(error)
            self._reply(404, {})

        def log_message(self, *args: object) -> None:
            pass  # http.server's own lines would carry paths; the structured log carries codes

    return Handler


def json_logger(stream=sys.stderr) -> Callable[[str, dict], None]:
    lock = threading.Lock()

    def log(message: str, fields: dict) -> None:
        record = {"timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "service": "bhp_report_inbox",
                  "message": message, **fields}
        with lock:
            print(json.dumps(record), file=stream, flush=True)

    return log


def serve(root: Path, address: str, port: int, authenticate: Callable[[str], bool], cert: Path | None,
          key: Path | None, transcribe_command: str | None,
          ready: Callable[[http.server.ThreadingHTTPServer], None] | None = None) -> None:
    store = Store(root)
    log = json_logger()
    transcriber = Transcriber(store, transcribe_command, log)
    server = http.server.ThreadingHTTPServer((address, port), make_handler(store, authenticate, transcriber, log))
    if cert and key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    log("inbox_started", {"port": server.server_address[1], "tls": bool(cert and key)})
    if ready:
        ready(server)
    server.serve_forever()
