"""The receiver over loopback: the panel's v1 calls, peer refusal, TLS under idle peers, and transcription."""

from __future__ import annotations

import io
import json
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stderr
from pathlib import Path

from support import FINGERPRINT, REPORT_ID, bundle, wav

from bhp_report_inbox import server
from bhp_report_inbox.store import Store


def request(base: str, method: str, path: str, body: bytes | None = None,
            headers: dict[str, str] | None = None) -> tuple[int, dict]:
    call = urllib.request.Request(base + path, data=body, method=method, headers=headers or {})
    context = ssl._create_unverified_context() if base.startswith("https") else None  # the test's own cert
    try:
        with urllib.request.urlopen(call, timeout=10, context=context) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        with error:
            return error.code, json.loads(error.read() or b"{}")


def wait_for(condition, seconds: float = 10.0) -> bool:  # noqa: ANN001
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return condition()


class InboxCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def start(self, admit: object = True, transcribe: str | None = None, context: ssl.SSLContext | None = None,
              **limits: float) -> str:
        self.log = io.StringIO()
        store = Store(self.root)
        log = server.json_logger(self.log)
        transcriber = server.Transcriber(store, transcribe, log)
        authenticate = admit if callable(admit) else (lambda address: admit)
        httpd = server.make_server(store, "127.0.0.1", 0, authenticate, transcriber, log, context, **limits)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)
        self.port = httpd.server_address[1]
        return f"{'https' if context else 'http'}://127.0.0.1:{self.port}"

    def send_bundle(self, base: str, manifest: dict, files: dict[str, bytes], report_id: str = REPORT_ID) -> int:
        for name, data in files.items():
            status, held = request(base, "GET", f"/v1/reports/{report_id}/files/{name}")
            self.assertEqual(status, 404)
            status, held = request(base, "PUT", f"/v1/reports/{report_id}/files/{name}", data,
                                   {"Content-Range": f"bytes 0-{len(data) - 1}/{len(data)}",
                                    "Content-Type": "application/octet-stream"})
            self.assertEqual((status, held), (200, {"received_bytes": len(data)}))
        status, _ = request(base, "POST", f"/v1/reports/{report_id}/complete", json.dumps(manifest).encode(),
                            {"Content-Type": "application/json"})
        return status

    def fake_whisper(self) -> tuple[str, Path]:
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory)
        fake = directory / "fake_whisper.py"
        ran = directory / "ran"
        fake.write_text(f"import sys\nopen({str(ran)!r}, 'a').write(sys.argv[1] + '\\n')\n"
                        "open(sys.argv[2] + '.txt', 'w').write('skip did nothing')\n")
        return f"{sys.executable} {fake} {{input}} {{output_base}}", ran


class ServerTest(InboxCase):
    def test_the_panel_flow(self) -> None:
        base = self.start()
        manifest, files = bundle()
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        status, _ = request(base, "POST", f"/v1/reports/{REPORT_ID}/complete", json.dumps(manifest).encode())
        self.assertEqual(status, 200, "complete again is 200, not a second report")
        update = json.dumps({"count": 2, "batch": "e" * 32}).encode()
        self.assertEqual(request(base, "POST", f"/v1/fingerprints/{FINGERPRINT}/count", update), (200, {"count": 2}))
        self.assertEqual(request(base, "POST", f"/v1/fingerprints/{FINGERPRINT}/count", update), (200, {"count": 2}),
                         "a retried update after a lost answer adds once")
        Store(self.root).set_status(REPORT_ID, "fix_in_pr", 81, "private reason")
        status, body = request(base, "GET", f"/v1/reports/status?ids={REPORT_ID},rpt-{'f' * 32}")
        self.assertEqual((status, body), (200, {"reports": [{"report_id": REPORT_ID, "status": "fix_in_pr", "pr": 81}]}),
                         "the panel gets status and PR, never the private reason")
        self.assertEqual(request(base, "GET", "/v1/reports/status?ids=" + ",".join([REPORT_ID] * 21))[0], 400)
        self.assertNotIn("logs.jsonl.gz", self.log.getvalue().replace(f"{REPORT_ID}", ""))

    def test_an_unknown_peer_is_refused(self) -> None:
        base = self.start(admit=False)
        with self.assertRaises(OSError, msg="refused before a byte is read, so before any TLS handshake"):
            request(base, "PUT", f"/v1/reports/{REPORT_ID}/files/logs.jsonl.gz", b"x",
                    {"Content-Range": "bytes 0-0/1"})
        self.assertTrue(wait_for(lambda: "peer_refused" in self.log.getvalue()))
        self.assertFalse(any((self.root / "incoming").iterdir()))

    def test_transcription_of_a_spoken_description(self) -> None:
        command, _ = self.fake_whisper()
        base = self.start(transcribe=command)
        manifest, files = bundle("voice", audio=wav())
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        transcript = self.root / "transcripts" / f"{REPORT_ID}.txt"
        self.assertTrue(wait_for(transcript.exists))
        self.assertEqual(transcript.read_text(), "skip did nothing")
        self.assertIn("transcription_finished", self.log.getvalue())

    def test_audio_in_another_format_is_not_transcribed(self) -> None:
        command, ran = self.fake_whisper()
        base = self.start(transcribe=command)
        manifest, files = bundle("voice", audio=wav(rate=44100, channels=2))
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        self.assertTrue(wait_for(lambda: "REPORT_INBOX.AUDIO_FORMAT_INVALID" in self.log.getvalue()))
        self.assertFalse(ran.exists(), "whisper never sees audio that is not 16 kHz mono PCM")
        self.assertFalse(server.description_audio_ok(self.root / "nonexistent.wav"))

    def test_untranscribed_audio_is_queued_again_on_start(self) -> None:
        store = Store(self.root)
        closed = "rpt-" + "c" * 32
        for report_id in (REPORT_ID, closed):
            manifest, files = bundle("voice", report_id=report_id, audio=wav())
            for name, data in files.items():
                store.put_range(report_id, name, f"bytes 0-{len(data) - 1}/{len(data)}", data)
            store.complete(report_id, json.dumps(manifest).encode())
        store.set_status(closed, "wont_fix", None, None)
        command, ran = self.fake_whisper()
        self.start(transcribe=command)
        self.assertTrue(wait_for((self.root / "transcripts" / f"{REPORT_ID}.txt").exists))
        self.assertIn('"transcription_requeued", "reports": 1', self.log.getvalue())
        self.assertNotIn(closed, ran.read_text(), "a closed report is not transcribed again")


@unittest.skipUnless(shutil.which("openssl"), "needs openssl to make a test certificate")
class DeadlineAndAudioTest(InboxCase):
    def test_a_slow_peer_is_cut_off_at_the_connection_deadline(self) -> None:
        self.start(connection_deadline=1.0, connection_seconds=30.0)
        peer = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        self.addCleanup(peer.close)
        started = time.monotonic()
        closed = False
        for byte in b"GET /v1/reports/status?ids= HTTP/1.1\r\n":
            try:
                peer.sendall(bytes([byte]))
            except OSError:
                closed = True
                break
            time.sleep(0.2)
            if time.monotonic() - started > 6:
                break
        if not closed:
            peer.settimeout(5)
            try:
                closed = peer.recv(1) == b""
            except OSError:
                closed = True
        self.assertTrue(closed)
        self.assertLess(time.monotonic() - started, 6, "cut off after its 1 s deadline, not after 30 s reads")

    def malformed_wav(self) -> bytes:
        fmt = struct.pack("<HHIIHH", 1, 1, 16000, 32000, 2, 16)
        body = b"WAVE" + b"fmt " + struct.pack("<I", 10_000_000) + fmt  # a chunk larger than its container
        return b"RIFF" + struct.pack("<I", len(body)) + body

    def test_audio_that_breaks_the_wave_module_is_refused_and_the_thread_lives_on(self) -> None:
        self.assertFalse(server.description_audio_ok(self.write_file("bad.wav", self.malformed_wav())))
        command, ran = self.fake_whisper()
        base = self.start(transcribe=command)
        bad = "rpt-" + "b" * 32
        manifest, files = bundle("voice", report_id=bad, audio=self.malformed_wav())
        self.assertEqual(self.send_bundle(base, manifest, files, bad), 201)
        self.assertTrue(wait_for((self.root / "transcripts" / f"{bad}.refused").exists))
        manifest, files = bundle("voice", audio=wav())
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        self.assertTrue(wait_for((self.root / "transcripts" / f"{REPORT_ID}.txt").exists),
                        "the next report is still transcribed")
        self.assertNotIn(bad, ran.read_text())

    def test_refused_and_repeatedly_failing_audio_is_not_retried_on_restart(self) -> None:
        store = Store(self.root)
        failing, refused, fresh = "rpt-" + "a" * 32, "rpt-" + "b" * 32, "rpt-" + "c" * 32
        for report_id in (failing, refused, fresh):
            manifest, files = bundle("voice", report_id=report_id, audio=wav())
            for name, data in files.items():
                store.put_range(report_id, name, f"bytes 0-{len(data) - 1}/{len(data)}", data)
            store.complete(report_id, json.dumps(manifest).encode())
        (self.root / "transcripts" / f"{failing}.refused").write_text('{"reason": "transcription_failed", "attempts": 3}')
        (self.root / "transcripts" / f"{refused}.refused").write_text('{"reason": "audio_format_invalid", "attempts": 0}')
        command, ran = self.fake_whisper()
        self.start(transcribe=command)
        self.assertTrue(wait_for((self.root / "transcripts" / f"{fresh}.txt").exists))
        self.assertEqual(ran.read_text().count("description.wav"), 1, "only the fresh report is tried")

    def test_a_failing_whisper_counts_its_attempts(self) -> None:
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory)
        failing = directory / "fail.py"
        failing.write_text("raise SystemExit(1)\n")
        base = self.start(transcribe=f"{sys.executable} {failing} {{input}} {{output_base}}")
        manifest, files = bundle("voice", audio=wav())
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        marker = self.root / "transcripts" / f"{REPORT_ID}.refused"
        self.assertTrue(wait_for(marker.exists))
        self.assertEqual(json.loads(marker.read_text()), {"reason": "transcription_failed", "attempts": 1})

    def write_file(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path


class KnownPeers:
    """An authenticator whose cache says `known` (None: not cached) and that admits every peer it is asked."""

    def __init__(self, known: bool | None = None) -> None:
        self.known, self.calls = known, 0

    def cached(self, address: str) -> bool | None:
        return self.known

    def __call__(self, address: str) -> bool:
        self.calls += 1
        return self.known is not False


class TlsTest(InboxCase):
    """The receiver as installed: TLS, with peers that open a connection and send nothing."""

    def tls(self) -> ssl.SSLContext:
        key, cert = self.root.parent / f"{self.root.name}.key", self.root.parent / f"{self.root.name}.crt"
        self.addCleanup(key.unlink, missing_ok=True)
        self.addCleanup(cert.unlink, missing_ok=True)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                        "-nodes", "-subj", "/CN=127.0.0.1", "-days", "1", "-keyout", str(key), "-out", str(cert)],
                       check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        return context

    def idle(self) -> socket.socket:
        peer = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        self.addCleanup(peer.close)
        return peer

    def test_an_idle_connection_neither_stalls_the_inbox_nor_stays(self) -> None:
        base = self.start(context=self.tls(), connection_seconds=2.0)
        idle = self.idle()
        started = time.monotonic()
        self.assertEqual(request(base, "GET", f"/v1/reports/{REPORT_ID}/files/logs.jsonl.gz")[0], 404)
        self.assertLess(time.monotonic() - started, 2.0, "served while the idle peer still holds its connection")
        self.assertEqual(idle.recv(1), b"", "the idle peer is closed when its handshake times out")
        self.assertTrue(wait_for(lambda: '"error": "TimeoutError"' in self.log.getvalue()
                                 or '"error": "SSLError"' in self.log.getvalue()))

    def test_connections_beyond_the_cap_are_closed_but_one_slot_waits_for_the_panel(self) -> None:
        peers = KnownPeers()
        base = self.start(admit=peers, context=self.tls(), maximum_connections=3, connection_seconds=10.0)
        held = [self.idle(), self.idle()]
        extra = self.idle()
        try:
            self.assertEqual(extra.recv(1), b"", "unknown peers get every slot but the last")
        except ConnectionResetError:
            pass
        self.assertIn("REPORT_INBOX.TOO_MANY_CONNECTIONS", self.log.getvalue())
        peers.known = True
        self.assertEqual(request(base, "GET", f"/v1/reports/{REPORT_ID}/files/logs.jsonl.gz")[0], 404,
                         "a peer the cache knows is the panel takes the kept slot")
        for peer in held:
            peer.close()
        self.assertTrue(wait_for(lambda: self.log.getvalue().count("connection_failed") == 2))

    def test_a_peer_known_not_to_be_the_panel_is_refused_before_it_takes_a_slot(self) -> None:
        peers = KnownPeers(known=False)
        self.start(admit=peers, context=self.tls(), maximum_connections=2)
        for _ in range(5):
            peer = self.idle()
            try:
                self.assertEqual(peer.recv(1), b"")
            except ConnectionResetError:
                pass
        self.assertEqual(peers.calls, 0, "refused in the accept loop, without a whois or a thread")
        self.assertEqual(self.log.getvalue().count('"peer_refused"'), 1, "the refusal lines are rate-limited")

    def test_the_panel_flow_over_tls(self) -> None:
        base = self.start(context=self.tls())
        manifest, files = bundle()
        self.assertEqual(self.send_bundle(base, manifest, files), 201)


class AuthenticatorTest(unittest.TestCase):
    def test_tailscale_whois_needs_the_tag(self) -> None:
        fake = Path(tempfile.mkdtemp()) / "tailscale"
        fake.write_text("#!/bin/sh\n[ \"$3\" = 100.64.0.7 ] && echo '{\"Node\":{\"Tags\":[\"tag:house-panel\"]}}' "
                        "|| echo '{\"Node\":{\"Tags\":[\"tag:other\"]}}'\n")
        fake.chmod(0o755)
        check = server.tailscale_whois_authenticator(str(fake), "tag:house-panel")
        self.assertTrue(check("100.64.0.7"))
        self.assertFalse(check("100.64.0.8"))
        self.assertFalse(server.tailscale_whois_authenticator("/nonexistent", "tag:house-panel")("100.64.0.7"))

    def test_a_malformed_whois_answer_is_refused(self) -> None:
        answers = {"100.64.0.1": "[]", "100.64.0.2": '{\"Node\":\"tag:house-panel\"}',
                   "100.64.0.3": '{\"Node\":{\"Tags\":\"tag:house-panel-x\"}}',
                   "100.64.0.4": '{\"Node\":{\"Tags\":[[\"tag:house-panel\"]]}}', "100.64.0.5": "not json"}
        fake = Path(tempfile.mkdtemp()) / "tailscale"
        self.addCleanup(shutil.rmtree, fake.parent)
        fake.write_text("#!/bin/sh\ncase \"$3\" in\n" + "".join(
            f"{address}) echo \"{answer}\" ;;\n" for address, answer in answers.items()) + "esac\n")
        fake.chmod(0o755)
        check = server.tailscale_whois_authenticator(str(fake), "tag:house-panel")
        for address in answers:
            self.assertFalse(check(address), address)

    def test_concurrent_lookups_of_one_address_run_whois_once(self) -> None:
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory)
        calls = directory / "calls"
        fake = directory / "tailscale"
        fake.write_text(f"#!/bin/sh\necho x >> {calls}\nsleep 0.5\necho '{{\"Node\":{{\"Tags\":[\"tag:house-panel\"]}}}}'\n")
        fake.chmod(0o755)
        check = server.tailscale_whois_authenticator(str(fake), "tag:house-panel")
        self.assertIsNone(check.cached("100.64.0.7"))
        answers: list[bool] = []
        threads = [threading.Thread(target=lambda: answers.append(check("100.64.0.7"))) for _ in range(5)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(10)
        self.assertEqual(answers, [True] * 5)
        self.assertEqual(len(calls.read_text().splitlines()), 1)
        self.assertTrue(check.cached("100.64.0.7"))

    def test_refusal_lines_are_rate_limited_and_count_what_they_held(self) -> None:
        lines: list[tuple[str, dict]] = []
        limited = server.RateLimitedLog(lambda message, fields: lines.append((message, fields)), interval=0.2)
        for _ in range(4):
            limited("peer_refused", {})
        limited("connection_refused", {})
        time.sleep(0.25)
        limited("peer_refused", {})
        self.assertEqual(lines, [("peer_refused", {}), ("connection_refused", {}), ("peer_refused", {"suppressed": 3})])

    def test_the_listen_address_is_one_literal_ip(self) -> None:
        self.assertTrue(server.listen_address_ok("100.64.0.1"))
        for address in ("", "0.0.0.0", "::", "*", "cole-arch-pc"):
            self.assertFalse(server.listen_address_ok(address), address)


if __name__ == "__main__":
    with redirect_stderr(io.StringIO()):
        unittest.main()
