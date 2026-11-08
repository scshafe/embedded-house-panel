#!/usr/bin/env python3
"""bhp-music-spotify-sign-in end to end, with no network beyond 127.0.0.1.

A fake token endpoint on loopback checks the PKCE exchange, and this script
plays the browser: it reads the authorize URL the tool prints and calls the
tool's callback. Each run checks that the token file is 0600 in a 0700
directory and that neither the refresh token, the code nor the verifier
appears in anything the tool printed.
"""
from __future__ import annotations

import base64
import hashlib
import http.server
import json
import os
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request

TOOL = sys.argv[1]
CLIENT_ID = "0123456789abcdef0123456789abcdef"
SECRET = "fixture-refresh-secret-value"
CODE = "fixture-code-AQB"
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


class TokenEndpoint(http.server.BaseHTTPRequestHandler):
    challenge = ""
    redirect_uri = ""
    verifiers: list[str] = []

    def do_POST(self) -> None:  # noqa: N802 (http.server's name)
        length = int(self.headers.get("Content-Length", "0"))
        fields = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode()))
        verifier = fields.get("code_verifier", "")
        TokenEndpoint.verifiers.append(verifier)
        digest = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        good = (
            set(fields) == {"grant_type", "code", "redirect_uri", "client_id", "code_verifier"}
            and fields["grant_type"] == "authorization_code"
            and fields["code"] == CODE
            and fields["client_id"] == CLIENT_ID
            and fields["redirect_uri"] == TokenEndpoint.redirect_uri
            and digest == TokenEndpoint.challenge
            and "Authorization" not in self.headers
        )
        body = (
            {"access_token": "fixture-access", "token_type": "Bearer", "expires_in": 3600,
             "scope": "user-read-playback-state user-modify-playback-state", "refresh_token": SECRET}
            if good else {"error": "invalid_grant", "error_description": "fake endpoint refused the exchange"}
        )
        payload = json.dumps(body).encode()
        self.send_response(200 if good else 400)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args: object) -> None:
        pass


def fetch(url: str) -> tuple[int, str]:
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as refused:
        return refused.code, refused.read().decode()


def run(token_dir: str, token_url: str, callback: str | None) -> tuple[int, str, str]:
    """Runs the tool; `callback` is the query to send back ({state} is filled in), None to send nothing."""
    port = free_port()
    TokenEndpoint.redirect_uri = f"http://127.0.0.1:{port}/callback"
    tool = subprocess.Popen(
        [TOOL, "--client-id", CLIENT_ID, "--token-dir", token_dir, "--port", str(port), "--token-url", token_url],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    printed = []
    authorize = ""
    assert tool.stdout is not None
    while True:
        line = tool.stdout.readline()
        if not line:
            break
        printed.append(line)
        if line.startswith("https://accounts.spotify.com/authorize?"):
            authorize = line.strip()
            break
    if authorize and callback is not None:
        query = dict(urllib.parse.parse_qsl(urllib.parse.urlsplit(authorize).query))
        TokenEndpoint.challenge = query.get("code_challenge", "")
        expect(query.get("client_id") == CLIENT_ID and query.get("response_type") == "code"
               and query.get("code_challenge_method") == "S256"
               and query.get("scope") == "user-read-playback-state user-modify-playback-state"
               and query.get("redirect_uri") == TokenEndpoint.redirect_uri, "the authorize URL's parameters")
        status, _ = fetch(f"http://127.0.0.1:{port}/favicon.ico")
        expect(status == 404, "anything but the callback is 404 and the tool keeps waiting")
        status, page = fetch(f"http://127.0.0.1:{port}/callback?" + callback.format(state=query.get("state", "")))
        printed.append(page)
    try:
        out, err = tool.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        tool.kill()
        out, err = tool.communicate()
    return tool.returncode, "".join(printed) + out, err


def main() -> int:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), TokenEndpoint)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    token_url = f"http://127.0.0.1:{server.server_address[1]}/api/token"
    with tempfile.TemporaryDirectory() as root:
        # Like /run/user/UID/bhp-music/token: the parent does not exist yet.
        token_dir = os.path.join(root, "bhp-music", "token")
        code, out, err = run(token_dir, token_url, f"code={CODE}&state={{state}}")
        expect(code == 0, f"the sign-in succeeds (exit {code}, stderr {err!r})")
        last = out.strip().splitlines()[-1] if out.strip() else "{}"
        expect(json.loads(last) == {"result": "saved", "path": f"{token_dir}/spotify-token.json"},
               "the tool prints only where it saved the token")
        expect("Signed in" in out, "the browser page says signed in")
        path = os.path.join(token_dir, "spotify-token.json")
        if os.path.exists(path):
            expect(stat.S_IMODE(os.stat(path).st_mode) == 0o600, "the token file is 0600")
            expect(stat.S_IMODE(os.stat(token_dir).st_mode) == 0o700, "the token directory is 0700")
            expect(stat.S_IMODE(os.stat(os.path.dirname(token_dir)).st_mode) == 0o700, "its new parent is 0700")
            with open(path, encoding="utf-8") as saved:
                record = json.load(saved)
            expect(record == {"schema_version": 1, "client_id": CLIENT_ID, "refresh_token": SECRET,
                              "signed_out": False}, "the record holds the client ID and the token")
        else:
            expect(False, "the token file exists")
        secrets = [SECRET, CODE] + TokenEndpoint.verifiers
        expect(not any(s and (s in out or s in err) for s in secrets),
               "no token, code or verifier in anything the tool printed")

        code, out, err = run(token_dir, token_url, None)
        expect(code == 1 and "MUSIC.TOKEN_EXISTS" in err, "a directory that holds a token is refused")

        other = os.path.join(root, "other")
        code, out, err = run(other, token_url, f"code={CODE}&state=forged")
        expect(code == 1 and "MUSIC.SIGN_IN_STATE_MISMATCH" in err, "a forged state is refused")
        expect(not os.path.exists(os.path.join(other, "spotify-token.json")), "a refused sign-in saves nothing")

        denied = os.path.join(root, "denied")
        code, out, err = run(denied, token_url, "error=access_denied&state={state}")
        expect(code == 1 and "MUSIC.SIGN_IN_DENIED" in err, "a denial is reported")

        code = subprocess.run([TOOL, "--client-id", CLIENT_ID, "--token-dir", os.path.join(root, "x"),
                               "--token-url", "https://example.com/token"], capture_output=True).returncode
        expect(code == 2, "a token URL other than Spotify's or loopback is a usage error")
    server.shutdown()
    if failures == 0:
        print("music_control_sign_in_tool_test passed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
