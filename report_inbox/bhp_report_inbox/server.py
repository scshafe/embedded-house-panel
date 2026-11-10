"""The report inbox's HTTP receiver: the v1 API of bug_reporter/inbox_client.h.

It listens on the PC's tailnet address with TLS from `tailscale cert`, and
admits a connection only from a tailnet node carrying the panel's tag, asked of
`tailscale whois` per peer address before the TLS handshake. The accept loop
never blocks on a peer: the peer check and the handshake run in the
connection's own thread under a timeout, and at most `maximum_connections` run at
once. It writes reports into the store and runs the optional transcription
command for spoken descriptions. It never logs a report's content: its log
lines carry codes, report IDs and sizes.
"""

from __future__ import annotations

import http.server
import ipaddress
import json
import queue
import re
import shlex
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
import wave
from pathlib import Path
from typing import Callable

from . import validate
from .store import Store, StoreError, write_atomic

FILE_PATH = re.compile(r"/v1/reports/(rpt-[0-9a-f]{32})/files/([a-z.]+)")
COMPLETE_PATH = re.compile(r"/v1/reports/(rpt-[0-9a-f]{32})/complete")
COUNT_PATH = re.compile(r"/v1/fingerprints/([0-9a-f]{64})/count")
MAXIMUM_BODY = 2 * 1024 * 1024
MAXIMUM_STATUS_IDS = 20
#: Seconds a connection may take for its TLS handshake, and then for each read or write.
CONNECTION_SECONDS = 30.0
#: Connections served at once; more are closed as soon as they are accepted. One of them is kept
#: for peers already known to be the panel, so unknown peers can never fill every slot.
MAXIMUM_CONNECTIONS = 8
#: The whole life of one connection, handshake to last byte; a slow peer is cut off after it.
CONNECTION_DEADLINE_SECONDS = 120.0
#: Refusal log lines of one kind are written at most this often; the next one counts the rest.
REFUSAL_LOG_SECONDS = 10.0
#: Transcription tries per report before it is marked refused and left alone.
TRANSCRIPTION_ATTEMPTS = 3


def listen_address_ok(address: str) -> bool:
    """A literal IP that is not a wildcard: the receiver binds the tailnet address only."""
    try:
        return not ipaddress.ip_address(address).is_unspecified
    except ValueError:
        return False


class TailscaleWhoisAuthenticator:
    """Admits a peer address whose tailnet node carries `allowed_tag`, cached per address.

    One `tailscale whois` runs per address at a time; concurrent connections from the same address
    wait for it. `cached` answers from the cache only, for the accept loop, which never waits.
    """

    def __init__(self, tailscale: str, allowed_tag: str, cache_seconds: float = 60.0) -> None:
        self.tailscale, self.allowed_tag, self.cache_seconds = tailscale, allowed_tag, cache_seconds
        self.cache: dict[str, tuple[float, bool]] = {}
        self.in_flight: dict[str, threading.Event] = {}
        self.lock = threading.Lock()

    def cached(self, address: str) -> bool | None:
        with self.lock:
            entry = self.cache.get(address)
            return entry[1] if entry and time.monotonic() - entry[0] < self.cache_seconds else None

    def __call__(self, address: str) -> bool:
        while True:
            with self.lock:
                entry = self.cache.get(address)
                if entry and time.monotonic() - entry[0] < self.cache_seconds:
                    return entry[1]
                waiting = self.in_flight.get(address)
                if waiting is None:
                    self.in_flight[address] = threading.Event()
                    break
            waiting.wait(10)
        try:
            allowed = self._whois(address)
            with self.lock:
                self.cache[address] = (time.monotonic(), allowed)
            return allowed
        finally:
            with self.lock:
                self.in_flight.pop(address).set()

    def _whois(self, address: str) -> bool:
        try:
            result = subprocess.run([self.tailscale, "whois", "--json", address], capture_output=True, text=True,
                                    timeout=5, check=False)
            answer = json.loads(result.stdout) if result.returncode == 0 else None
            node = answer.get("Node") if isinstance(answer, dict) else None
            tags = node.get("Tags") if isinstance(node, dict) else None
            return isinstance(tags, list) and self.allowed_tag in [tag for tag in tags if isinstance(tag, str)]
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return False


def tailscale_whois_authenticator(tailscale: str, allowed_tag: str, cache_seconds: float = 60.0
                                  ) -> TailscaleWhoisAuthenticator:
    """Admits a peer address whose tailnet node carries `allowed_tag`."""
    return TailscaleWhoisAuthenticator(tailscale, allowed_tag, cache_seconds)


class RateLimitedLog:
    """Writes one line of each kind per `interval`, and counts the lines it held back."""

    def __init__(self, log: Callable[[str, dict], None], interval: float = REFUSAL_LOG_SECONDS) -> None:
        self.log, self.interval = log, interval
        self.last: dict[str, float] = {}
        self.held: dict[str, int] = {}
        self.lock = threading.Lock()

    def __call__(self, message: str, fields: dict) -> None:
        with self.lock:
            now = time.monotonic()
            if message in self.last and now - self.last[message] < self.interval:
                self.held[message] = self.held.get(message, 0) + 1
                return
            self.last[message] = now
            held = self.held.pop(message, 0)
        self.log(message, {**fields, "suppressed": held} if held else fields)


def description_audio_ok(path: Path) -> bool:
    """The panel's description format (bug_reporter/description_audio.h): PCM, 16-bit, mono, 16 kHz, 60 s at most."""
    try:
        with wave.open(str(path), "rb") as audio:
            return (audio.getcomptype() == "NONE" and audio.getsampwidth() == 2 and audio.getnchannels() == 1
                    and audio.getframerate() == validate.DESCRIPTION_RATE
                    and 0 < audio.getnframes() <= validate.DESCRIPTION_MAXIMUM_FRAMES)
    except Exception:  # noqa: BLE001 (wave raises RuntimeError and others on a malformed chunk)
        return False


class Transcriber:
    """Runs the owner's speech-to-text command (decision 4) for each spoken description, one at a time.

    `command` is a template with `{input}` and `{output_base}`; for whisper.cpp:
    `whisper-cli -m MODEL -l en -t 4 -np -f {input} -otxt -of {output_base}`, which writes `{output_base}.txt`.
    The queue lives in memory, so on start it takes again every open report whose audio has no transcript
    and no `transcripts/<id>.refused` marker. The marker is written when the audio is not the panel's format,
    and after TRANSCRIPTION_ATTEMPTS failed runs, so such audio is not tried again on every restart.
    """

    def __init__(self, store: Store, command: str | None, log: Callable[[str, dict], None]) -> None:
        self.store, self.command, self.log = store, command, log
        self.jobs: queue.Queue[str] = queue.Queue()
        if command:
            threading.Thread(target=self._run, daemon=True).start()

    def submit(self, report_id: str) -> None:
        if self.command and (self.store.root / "reports" / report_id / "description.wav").exists():
            self.jobs.put(report_id)

    def _marker(self, report_id: str) -> Path:
        return self.store.root / "transcripts" / f"{report_id}.refused"

    def _refused(self, report_id: str) -> dict:
        try:
            marker = json.loads(self._marker(report_id).read_text())
            return marker if isinstance(marker, dict) else {}
        except (OSError, ValueError):
            return {}

    def _given_up(self, report_id: str) -> bool:
        marker = self._refused(report_id)
        return marker.get("reason") == "audio_format_invalid" or marker.get("attempts", 0) >= TRANSCRIPTION_ATTEMPTS

    def _mark(self, report_id: str, reason: str) -> None:
        before = self._refused(report_id).get("attempts", 0)
        attempts = (before if isinstance(before, int) else 0) + (1 if reason == "transcription_failed" else 0)
        write_atomic(self._marker(report_id), json.dumps({"reason": reason, "attempts": attempts}).encode() + b"\n")

    def _requeue(self) -> None:
        waiting = [report_id for report_id in self.store.report_ids()
                   if (self.store.root / "reports" / report_id / "description.wav").exists()
                   and not (self.store.root / "transcripts" / f"{report_id}.txt").exists()
                   and not self._given_up(report_id)
                   and (self.store.status(report_id) or {}).get("status") not in validate.TERMINAL_STATUSES]
        for report_id in waiting:
            self.jobs.put(report_id)
        if waiting:
            self.log("transcription_requeued", {"reports": len(waiting)})

    def _run(self) -> None:
        try:
            self._requeue()
        except Exception as error:  # noqa: BLE001 (the thread must outlive any one report)
            self.log("transcription_failed", {"stage": "requeue", "error": type(error).__name__})
        while True:
            report_id = self.jobs.get()
            try:
                self._transcribe(report_id)
            except Exception as error:  # noqa: BLE001
                self.log("transcription_failed", {"report_id": report_id, "error": type(error).__name__})

    def _transcribe(self, report_id: str) -> None:
        audio = self.store.root / "reports" / report_id / "description.wav"
        base = self.store.root / "transcripts" / report_id
        if Path(f"{base}.txt").exists() or self._given_up(report_id):
            return
        if not description_audio_ok(audio):
            self._mark(report_id, "audio_format_invalid")
            self.log("transcription_refused", {"report_id": report_id,
                                               "error_code": "REPORT_INBOX.AUDIO_FORMAT_INVALID"})
            return
        argv = [part.format(input=str(audio), output_base=str(base)) for part in shlex.split(self.command or "")]
        try:
            result = subprocess.run(argv, capture_output=True, timeout=600, check=False)
            ok = result.returncode == 0 and Path(f"{base}.txt").exists()
        except (OSError, subprocess.TimeoutExpired):
            ok = False
        if ok:
            Path(f"{base}.txt").chmod(0o640)
            self._marker(report_id).unlink(missing_ok=True)
        else:
            self._mark(report_id, "transcription_failed")
        self.log("transcription_finished" if ok else "transcription_failed", {"report_id": report_id})


def make_handler(store: Store, authenticate: Callable[[str], bool], transcriber: Transcriber,
                 log: Callable[[str, dict], None]) -> type[http.server.BaseHTTPRequestHandler]:
    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = "bhp-report-inbox/1"
        sys_version = ""
        timeout = CONNECTION_SECONDS

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
                        update = json.loads(body)
                        count, batch = update.get("count"), update.get("batch")
                    except (ValueError, AttributeError):
                        count, batch = None, None
                    record = store.add_count(match[1], count, batch)
                    return self._reply(200, {"count": record["repeats"]})
            except StoreError as error:
                return self._fail(error)
            self._reply(404, {})

        def log_message(self, *args: object) -> None:
            pass  # http.server's own lines would carry paths; the structured log carries codes

    return Handler


class InboxServer(http.server.ThreadingHTTPServer):
    """Accepts without blocking; each connection's thread checks the peer, then does the TLS handshake.

    The accept loop refuses a peer the cache already knows is not the panel, before it takes a slot, and
    keeps the last slot for peers the cache knows are. Each connection ends after `connection_deadline`.
    """

    daemon_threads = True

    def __init__(self, address: tuple[str, int], handler: type[http.server.BaseHTTPRequestHandler],
                 authenticate: Callable[[str], bool], log: Callable[[str, dict], None],
                 context: ssl.SSLContext | None = None, maximum_connections: int = MAXIMUM_CONNECTIONS,
                 connection_seconds: float = CONNECTION_SECONDS,
                 connection_deadline: float = CONNECTION_DEADLINE_SECONDS) -> None:
        super().__init__(address, handler)
        self.authenticate, self.log, self.connection_seconds = authenticate, log, connection_seconds
        self.connection_deadline = connection_deadline
        self.cached = getattr(authenticate, "cached", lambda address: None)
        self.refusals = RateLimitedLog(log)
        self.maximum_connections, self.active = maximum_connections, 0
        self.active_lock = threading.Lock()
        if context:
            self.socket = context.wrap_socket(self.socket, server_side=True, do_handshake_on_connect=False)

    def verify_request(self, request, client_address) -> bool:  # noqa: ANN001 (socketserver's signature)
        known = self.cached(client_address[0])
        if known is False:
            self.refusals("peer_refused", {"stage": "accept"})
            return False
        limit = self.maximum_connections if known else self.maximum_connections - 1
        with self.active_lock:
            if self.active < limit:
                self.active += 1
                return True
        self.refusals("connection_refused", {"error_code": "REPORT_INBOX.TOO_MANY_CONNECTIONS"})
        return False

    def _release(self) -> None:
        with self.active_lock:
            self.active -= 1

    def process_request(self, request, client_address) -> None:  # noqa: ANN001
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._release()
            raise

    @staticmethod
    def _cut(request) -> None:  # noqa: ANN001
        try:
            request.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def process_request_thread(self, request, client_address) -> None:  # noqa: ANN001
        deadline = threading.Timer(self.connection_deadline, self._cut, (request,))
        deadline.daemon = True
        deadline.start()
        try:
            if not self.authenticate(client_address[0]):
                self.refusals("peer_refused", {"stage": "connection"})
                return
            request.settimeout(self.connection_seconds)
            if isinstance(request, ssl.SSLSocket):
                request.do_handshake()
            self.finish_request(request, client_address)
        except Exception as error:  # noqa: BLE001 (a failed connection never stops the receiver)
            self.log("connection_failed", {"error": type(error).__name__})
        finally:
            deadline.cancel()
            self.shutdown_request(request)
            self._release()


def make_server(store: Store, address: str, port: int, authenticate: Callable[[str], bool],
                transcriber: Transcriber, log: Callable[[str, dict], None], context: ssl.SSLContext | None = None,
                maximum_connections: int = MAXIMUM_CONNECTIONS, connection_seconds: float = CONNECTION_SECONDS,
                connection_deadline: float = CONNECTION_DEADLINE_SECONDS) -> InboxServer:
    handler = make_handler(store, authenticate, transcriber, log)
    handler.timeout = connection_seconds
    return InboxServer((address, port), handler, authenticate, log, context, maximum_connections, connection_seconds,
                       connection_deadline)


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
    if not listen_address_ok(address):
        raise ValueError("the receiver binds one literal, non-wildcard address")
    store = Store(root)
    log = json_logger()
    transcriber = Transcriber(store, transcribe_command, log)
    context = None
    if cert and key:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, key)
    server = make_server(store, address, port, authenticate, transcriber, log, context)
    log("inbox_started", {"port": server.server_address[1], "tls": bool(cert and key)})
    if ready:
        ready(server)
    server.serve_forever()
