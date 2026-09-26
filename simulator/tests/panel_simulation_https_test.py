#!/usr/bin/env python3
"""Exercise the real shell/worker/reducer TLS composition, resume and clean teardown."""
import json
import http.server
import os
from pathlib import Path
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
renderer = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="bhp-display-https-") as temporary:
    directory = Path(temporary)
    certificate, key = directory / "ca.pem", directory / "key.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-days", "1", "-keyout", str(key), "-out", str(certificate),
                    "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.chmod(key, 0o600)
    server = subprocess.Popen([sys.executable, str(root / "vocabulary_panel/tools/fixture_study_server.py"),
                               "--certificate", str(certificate), "--private-key", str(key),
                               "--state-directory", str(directory / "server"), "--port", "0"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if not select.select([server.stdout], [], [], 5)[0]:
            raise AssertionError("fixture server readiness timed out")
        ready = json.loads(server.stdout.readline())
        config = directory / "client.json"
        config.write_text(json.dumps({"base_url": ready["url"], "profile_ref": "fixture-default",
                                      "topic_refs": ["fixture-systems", "fixture-words"],
                                      "fixture_ca_file": str(certificate)}))
        command = [str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                   "--lexinet-config", str(config), "--state-directory", str(directory / "client"),
                   "--exit-after-ms", "1400", "--output", str(directory / "frame.png")]
        first = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
        frames = [json.loads(line)["data"] for line in first.stdout.splitlines()]
        assert frames[0]["study_state"] == "loading"
        assert any(frame.get("study_state") == "front" for frame in frames)
        assert '"status":"200"' in first.stderr
        # Restart restores cached pixels without creating empty polling syncs.
        second = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
        assert json.loads(second.stdout.splitlines()[0])["data"]["study_state"] == "front"
        assert '"status":"200"' not in second.stderr
        prior_events = json.loads((directory / "server/fixture-state.json").read_text())["events"].values()
        assert not any(entry["event"]["kind"] == "rated" for entry in prior_events), \
            "loading or resuming a card must not create an implicit rating"
        # A real accepted interaction creates durable events and then a TLS sync.
        interactions = "\n".join(json.dumps(event) for event in [
            {"type": "intent", "intent_id": "study.flip", "source": "touch"},
            {"type": "intent", "intent_id": "study.rate.good", "source": "touch"},
        ]) + "\n"
        third = subprocess.run(command, input=interactions, capture_output=True, text=True,
                               timeout=10, check=True)
        assert '"status":"200"' in third.stderr
        saved_events = [entry["event"] for entry in
                        json.loads((directory / "server/fixture-state.json").read_text())["events"].values()]
        assert [event["rating"] for event in saved_events if event["kind"] == "rated"] == ["good"], \
            "the real shell action must reach the fixture ledger exactly once"
        assert any(event["kind"] == "revealed" for event in saved_events)
        assert (directory / "frame.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")

        def prove_shell_cancellation(name, event, expected):
            """Hold a real TLS response and observe the shell closing its transfer."""
            request_started = threading.Event()
            peer_closed = threading.Event()

            class HeldResponse(http.server.BaseHTTPRequestHandler):
                def log_message(self, *_args):
                    pass

                def do_POST(self):
                    self.rfile.read(int(self.headers["Content-Length"]))
                    request_started.set()
                    # Shorter than the application's 3-second HTTP deadline:
                    # EOF proves cancellation, not a natural transfer timeout.
                    self.connection.settimeout(1.5)
                    try:
                        if self.connection.recv(1) == b"":
                            peer_closed.set()
                    except (ssl.SSLEOFError, ConnectionResetError):
                        peer_closed.set()
                    except socket.timeout:
                        pass

            held = http.server.HTTPServer(("127.0.0.1", 0), HeldResponse)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certificate, key)
            held.socket = context.wrap_socket(held.socket, server_side=True)
            serving = threading.Thread(target=held.handle_request, daemon=True)
            serving.start()
            cancel_config = directory / (name + ".json")
            cancel_config.write_text(json.dumps({
                "base_url": "https://127.0.0.1:" + str(held.server_port),
                "profile_ref": "fixture-default", "topic_refs": ["fixture-systems", "fixture-words"],
                "fixture_ca_file": str(certificate),
            }))
            child = subprocess.Popen([
                str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                "--lexinet-config", str(cancel_config), "--state-directory", str(directory / name),
                "--exit-after-ms", "8000",
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                assert request_started.wait(5), "shell HTTP request did not start"
                child.stdin.write(json.dumps(event) + "\n")
                child.stdin.flush()
                assert peer_closed.wait(1.5), name + " did not cancel in-flight TLS transfer"
                output, _errors = child.communicate(json.dumps({"type": "quit"}) + "\n", timeout=5)
                assert child.returncode == 0, _errors
                observed = [json.loads(line)["data"] for line in output.splitlines()]
                assert any(all(frame.get(key) == value for key, value in expected.items())
                           for frame in observed), name + " did not render its new shell state"
                assert not any(frame.get("study_state") == "error" for frame in observed), \
                    "cancelled worker completion escaped into the new shell state"
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=5)
                held.server_close()
                serving.join(timeout=2)

        prove_shell_cancellation("night-cancellation", {"type": "lifecycle", "state": "night"},
                                 {"display_mode": "night_red"})
        prove_shell_cancellation("surface-cancellation", {
            "type": "scenario", "path": str(root / "simulator/scenarios/agenda-today.json"),
        }, {"surface": "agenda", "fixture_override": True})

        # This body passes HTTP/wire validation but contradicts the reducer's
        # durably requested scope. It must remain a visible terminal error.
        mismatched_session = json.loads((root / "vocabulary_panel/tests/fixtures/study_v1/examples/session-created.json").read_text())
        mismatched_session["scope"]["topic_refs"].insert(1, "fixture-unrequested")
        response_bytes = json.dumps(mismatched_session).encode()
        received_requests = []

        class MismatchedScope(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_POST(self):
                received_requests.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(response_bytes)))
                self.end_headers()
                self.wfile.write(response_bytes)

        mismatch = http.server.HTTPServer(("127.0.0.1", 0), MismatchedScope)
        mismatch_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        mismatch_context.load_cert_chain(certificate, key)
        mismatch.socket = mismatch_context.wrap_socket(mismatch.socket, server_side=True)
        mismatch_thread = threading.Thread(target=mismatch.serve_forever, daemon=True)
        mismatch_thread.start()
        mismatch_config = directory / "mismatch.json"
        mismatch_config.write_text(json.dumps({
            "base_url": "https://127.0.0.1:" + str(mismatch.server_port),
            "profile_ref": "fixture-default", "topic_refs": ["fixture-systems", "fixture-words"],
            "fixture_ca_file": str(certificate),
        }))
        try:
            rejected = subprocess.run([
                str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                "--lexinet-config", str(mismatch_config), "--state-directory", str(directory / "mismatch-client"),
                "--exit-after-ms", "2200",
            ], capture_output=True, text=True, timeout=10, check=True)
            rejected_frames = [json.loads(line)["data"] for line in rejected.stdout.splitlines()]
            assert any(frame.get("acknowledgement") == "STUDY.CREATE_RESPONSE_MISMATCH"
                       for frame in rejected_frames), "reducer did not expose the mismatched scope"
            assert len(received_requests) == 1, "terminal reducer rejection retried without explicit reload"
        finally:
            mismatch.shutdown()
            mismatch.server_close()
            mismatch_thread.join(timeout=2)
        print(json.dumps({"passed": True, "https_shell_resume_and_teardown": True,
                          "shell_lifecycle_and_surface_cancel_inflight_https": True,
                          "terminal_reducer_rejection_does_not_retry": True,
                          "simulation_not_hardware_evidence": True}))
    finally:
        server.terminate()
        server.wait(timeout=5)
