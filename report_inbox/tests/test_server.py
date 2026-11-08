"""The receiver over loopback HTTP: the panel's v1 calls, peer refusal and transcription."""

from __future__ import annotations

import io
import json
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stderr
from pathlib import Path

from support import FINGERPRINT, REPORT_ID, bundle

from bhp_report_inbox import server
from bhp_report_inbox.store import Store


def request(base: str, method: str, path: str, body: bytes | None = None,
            headers: dict[str, str] | None = None) -> tuple[int, dict]:
    call = urllib.request.Request(base + path, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(call, timeout=10) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        with error:
            return error.code, json.loads(error.read() or b"{}")


class ServerTest(unittest.TestCase):
    def start(self, admit: bool = True, transcribe: str | None = None) -> str:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.log = io.StringIO()
        store = Store(self.root)
        log = server.json_logger(self.log)
        transcriber = server.Transcriber(store, transcribe, log)
        handler = server.make_handler(store, lambda address: admit, transcriber, log)
        httpd = server.http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)
        return f"http://127.0.0.1:{httpd.server_address[1]}"

    def send_bundle(self, base: str, manifest: dict, files: dict[str, bytes]) -> int:
        for name, data in files.items():
            status, held = request(base, "GET", f"/v1/reports/{REPORT_ID}/files/{name}")
            self.assertEqual(status, 404)
            status, held = request(base, "PUT", f"/v1/reports/{REPORT_ID}/files/{name}", data,
                                   {"Content-Range": f"bytes 0-{len(data) - 1}/{len(data)}",
                                    "Content-Type": "application/octet-stream"})
            self.assertEqual((status, held), (200, {"received_bytes": len(data)}))
        status, _ = request(base, "POST", f"/v1/reports/{REPORT_ID}/complete", json.dumps(manifest).encode(),
                            {"Content-Type": "application/json"})
        return status

    def test_the_panel_flow(self) -> None:
        base = self.start()
        manifest, files = bundle()
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        status, _ = request(base, "POST", f"/v1/reports/{REPORT_ID}/complete", json.dumps(manifest).encode())
        self.assertEqual(status, 200, "complete again is 200, not a second report")
        self.assertEqual(request(base, "POST", f"/v1/fingerprints/{FINGERPRINT}/count", b'{"count": 2}'),
                         (200, {"count": 2}))
        Store(self.root).set_status(REPORT_ID, "fix_in_pr", 81, "private reason")
        status, body = request(base, "GET", f"/v1/reports/status?ids={REPORT_ID},rpt-{'f' * 32}")
        self.assertEqual((status, body), (200, {"reports": [{"report_id": REPORT_ID, "status": "fix_in_pr", "pr": 81}]}),
                         "the panel gets status and PR, never the private reason")
        self.assertEqual(request(base, "GET", "/v1/reports/status?ids=" + ",".join([REPORT_ID] * 21))[0], 400)
        self.assertNotIn("logs.jsonl.gz", self.log.getvalue().replace(f"{REPORT_ID}", ""))

    def test_an_unknown_peer_is_refused(self) -> None:
        base = self.start(admit=False)
        self.assertEqual(request(base, "GET", f"/v1/reports/{REPORT_ID}/files/logs.jsonl.gz"),
                         (403, {"error_code": "REPORT_INBOX.PEER_REFUSED"}))
        self.assertFalse(any((self.root / "incoming").iterdir()))

    def test_transcription_of_a_spoken_description(self) -> None:
        fake = Path(tempfile.mkdtemp()) / "fake_whisper.py"
        fake.write_text("import sys\nopen(sys.argv[2] + '.txt', 'w').write('skip did nothing')\n")
        base = self.start(transcribe=f"{sys.executable} {fake} {{input}} {{output_base}}")
        manifest, files = bundle("voice", audio=b"RIFF0000WAVEfmt ")
        self.assertEqual(self.send_bundle(base, manifest, files), 201)
        transcript = self.root / "transcripts" / f"{REPORT_ID}.txt"
        for _ in range(100):
            if transcript.exists():
                break
            time.sleep(0.05)
        self.assertEqual(transcript.read_text(), "skip did nothing")
        self.assertIn("transcription_finished", self.log.getvalue())


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


if __name__ == "__main__":
    with redirect_stderr(io.StringIO()):
        unittest.main()
