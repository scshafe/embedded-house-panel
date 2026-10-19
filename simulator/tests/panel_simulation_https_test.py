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
import urllib.request

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
                                      "fixture_ca_file": str(certificate), "fixture_data": True}))
        command = [str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                   "--lexinet-config", str(config), "--state-directory", str(directory / "client"),
                   "--exit-after-ms", "1400", "--output", str(directory / "frame.png")]
        first = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
        frames = [json.loads(line)["data"] for line in first.stdout.splitlines()]
        assert frames[0]["study_state"] == "loading"
        assert any(frame.get("study_state") == "front" for frame in frames)
        assert all(not frame.get("delivery_status") for frame in frames), \
            "initial presentation-only synchronization must stay quiet"
        assert all(frame["fixture_data"] for frame in frames if "fixture_data" in frame)
        assert '"status":"200"' in first.stderr
        # Restart restores cached pixels without creating empty polling syncs.
        second = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True)
        assert json.loads(second.stdout.splitlines()[0])["data"]["study_state"] == "front"
        assert '"status":"200"' not in second.stderr
        assert all(not json.loads(line)["data"].get("delivery_status") for line in second.stdout.splitlines())
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
        third_frames = [json.loads(line)["data"] for line in third.stdout.splitlines()
                        if "study_state" in json.loads(line)["data"]]
        assert any(frame["delivery_status"] == "Waiting to sync" and frame["pending_review_events"] == 1
                   for frame in third_frames), "pending rating did not show unsaved work"
        assert any(frame["delivery_status"] == "Syncing with Lexinet" for frame in third_frames), \
            "actual in-flight rating synchronization was not projected"
        assert third_frames[-1]["delivery_status"] == "" and third_frames[-1]["pending_review_events"] == 0, \
            "a real acknowledgement must clear the rating notice"
        saved_events = [entry["event"] for entry in
                        json.loads((directory / "server/fixture-state.json").read_text())["events"].values()]
        assert [event["rating"] for event in saved_events if event["kind"] == "rated"] == ["good"], \
            "the real shell action must reach the fixture ledger exactly once"
        assert any(event["kind"] == "revealed" for event in saved_events)
        assert (directory / "frame.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")

        # Terminal rejection stays visible after the outbox is adjudicated and
        # after a real process restart; no timer is permitted to call it saved.
        rejection_command = command.copy()
        rejection_command[rejection_command.index("--state-directory") + 1] = str(directory / "rejected-client")
        subprocess.run(rejection_command, capture_output=True, text=True, timeout=10, check=True)
        control = urllib.request.Request(ready["url"] + "/fixture/control",
                                         data=json.dumps({"terminal_rejection": True}).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(control, context=ssl.create_default_context(cafile=str(certificate)), timeout=3):
            pass
        rejected_rating = subprocess.run(rejection_command, input=interactions, capture_output=True, text=True,
                                         timeout=10, check=True)
        rating_frames = [json.loads(line)["data"] for line in rejected_rating.stdout.splitlines()
                         if "study_state" in json.loads(line)["data"]]
        assert rating_frames[-1]["delivery_status"] == "Not saved" and rating_frames[-1]["pending_review_events"] == 0, (rating_frames, rejected_rating.stderr)
        rejected_restart = subprocess.run(rejection_command, capture_output=True, text=True, timeout=10, check=True)
        assert json.loads(rejected_restart.stdout.splitlines()[0])["data"]["delivery_status"] == "Not saved"

        # The source marker is explicit config, never guessed from HTTPS, a
        # loopback URL or the presence of a fixture CA. This exercises wiring;
        # this server's content remains synthetic test evidence.
        live_marker_config = json.loads(config.read_text())
        live_marker_config["fixture_data"] = False
        config.write_text(json.dumps(live_marker_config))
        marker = subprocess.run(command[:command.index("--exit-after-ms")], capture_output=True, text=True, timeout=10, check=True)
        assert json.loads(marker.stdout.splitlines()[0])["data"]["fixture_data"] is False

        def prove_study_config_outbox_survives_a_drop_and_a_restart():
            """The production BHP_VOCABULARY_PANEL_* path: rate, drop, queue, restart, deliver."""
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            state = directory / "study-config-server"

            def start_server():
                process = subprocess.Popen([sys.executable, str(root / "vocabulary_panel/tools/fixture_study_server.py"),
                                            "--certificate", str(certificate), "--private-key", str(key),
                                            "--state-directory", str(state), "--port", str(port)],
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                if not select.select([process.stdout], [], [], 5)[0]:
                    raise AssertionError("study-config fixture server readiness timed out")
                assert json.loads(process.stdout.readline())["message"] == "fixture_ready"
                return process

            def stop_server(process):
                process.terminate()
                process.wait(timeout=5)
                process.stdout.close()
                process.stderr.close()

            environment = directory / "vocabulary-panel.env"
            environment.write_text("\n".join([
                "# Fixture values for the renderer; the production file names the tailnet listener.",
                "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL=https://127.0.0.1:" + str(port),
                "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF=fixture-default",
                "BHP_VOCABULARY_PANEL_STUDY_TOPIC_REFS=fixture-words,fixture-systems",
                "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE=tailnet_node",
                "BHP_VOCABULARY_PANEL_CREDENTIAL_FILE=",
                "BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS=1000",
                "BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS=3000",
                "BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES=1048576",
                "BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS=604800",
            ]) + "\n")
            study_command = [str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                             "--study-config", str(environment), "--study-test-ca-file", str(certificate),
                             "--state-directory", str(directory / "study-config-client"), "--exit-after-ms", "1400"]

            def run(stdin=None):
                completed = subprocess.run(study_command, input=stdin, capture_output=True, text=True,
                                           timeout=10, check=True)
                return [json.loads(line)["data"] for line in completed.stdout.splitlines()
                        if "study_state" in json.loads(line)["data"]]

            def ratings():
                events = json.loads((state / "fixture-state.json").read_text())["events"].values()
                return [entry["event"]["rating"] for entry in events if entry["event"]["kind"] == "rated"]

            server_process = start_server()
            try:
                created = run()
                assert any(frame["study_state"] == "front" for frame in created), created
                assert all(frame["fixture_data"] is False for frame in created), \
                    "the production configuration is live data, not a fixture"
            finally:
                stop_server(server_process)
            # The network is gone: the rating stays visibly queued in the durable outbox.
            queued = run(interactions)
            assert queued[-1]["pending_review_events"] == 1 and queued[-1]["delivery_status"] == "Waiting to sync", queued
            assert ratings() == []
            server_process = start_server()
            try:
                # A fresh process restores the same session and delivers the queued rating once.
                delivered = run()
                assert delivered[0]["pending_review_events"] == 1, delivered
                assert delivered[-1]["pending_review_events"] == 0 and delivered[-1]["delivery_status"] == "", delivered
                assert ratings() == ["good"]
            finally:
                stop_server(server_process)
            # Exactly one client source, and the production reader's codes reach the caller.
            both = subprocess.run(study_command + ["--lexinet-config", str(config)], capture_output=True,
                                  text=True, timeout=10, check=False)
            assert both.returncode == 1 and "SIMULATOR.STUDY_CLIENT_SOURCES_EXCLUSIVE" in both.stderr
            placeholder = subprocess.run(
                [part if part != str(environment) else str(root / "config/vocabulary-panel.env.example")
                 for part in study_command], capture_output=True, text=True, timeout=10, check=False)
            assert placeholder.returncode == 1 and "STUDY_CONFIG.PLACEHOLDER" in placeholder.stderr

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
                durable_path = directory / name / "study-state.json"
                before_navigation = json.loads(durable_path.read_text())
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
                after_navigation = json.loads(durable_path.read_text())
                for field in ("stream", "session", "initial_request", "successor_request", "in_flight", "pending"):
                    assert after_navigation[field] == before_navigation[field], \
                        name + " changed durable study request or event identity during cancellation"
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=5)
                held.server_close()
                serving.join(timeout=2)

        prove_shell_cancellation("night-cancellation", {"type": "lifecycle", "state": "night"},
                                 {"display_mode": "night_red"})
        prove_shell_cancellation("home-cancellation", {
            "type": "intent", "intent_id": "shell.home", "source": "touch",
        }, {"surface": "home", "fixture_override": False})
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
            rejected = subprocess.Popen([
                str(renderer), "--scenario", str(root / "simulator/scenarios/day-front.json"),
                "--lexinet-config", str(mismatch_config), "--state-directory", str(directory / "mismatch-client"),
                "--exit-after-ms", "2200",
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            rejected_frames = []
            terminal_observed = threading.Event()

            def observe_terminal_frames():
                for line in rejected.stdout:
                    frame = json.loads(line)["data"]
                    rejected_frames.append(frame)
                    if frame.get("acknowledgement") == "STUDY.CREATE_RESPONSE_MISMATCH":
                        terminal_observed.set()

            frame_reader = threading.Thread(target=observe_terminal_frames, daemon=True)
            frame_reader.start()
            try:
                assert terminal_observed.wait(3), "reducer did not expose the mismatched scope"
                terminal_state = json.loads((directory / "mismatch-client/study-state.json").read_text())
                for intent in ("shell.home", "shell.open_agenda", "shell.home", "shell.open_study"):
                    rejected.stdin.write(json.dumps({"type": "intent", "intent_id": intent, "source": "touch"}) + "\n")
                rejected.stdin.flush()
                rejected.wait(timeout=6)
                frame_reader.join(timeout=2)
                assert rejected.returncode == 0, rejected.stderr.read()
                assert any(frame.get("surface") == "home" for frame in rejected_frames)
                assert any(frame.get("surface") == "agenda" for frame in rejected_frames)
                assert rejected_frames[-1]["surface"] == "study" and rejected_frames[-1]["study_state"] == "error", \
                    "returning through Home erased the terminal reducer failure"
                assert len(received_requests) == 1, "app switching retried a terminal rejection without explicit reload"
                returned_state = json.loads((directory / "mismatch-client/study-state.json").read_text())
                assert returned_state["initial_request"] == terminal_state["initial_request"], \
                    "terminal navigation replaced the retained creation request"
            finally:
                if rejected.poll() is None:
                    rejected.kill()
                    rejected.wait(timeout=3)
                frame_reader.join(timeout=2)
                rejected.stdin.close()
                rejected.stdout.close()
                rejected.stderr.close()
        finally:
            mismatch.shutdown()
            mismatch.server_close()
            mismatch_thread.join(timeout=2)
        prove_study_config_outbox_survives_a_drop_and_a_restart()
        print(json.dumps({"passed": True, "https_shell_resume_and_teardown": True,
                          "study_config_outbox_survives_drop_and_restart": True,
                          "shell_lifecycle_and_surface_cancel_inflight_https": True,
                          "terminal_reducer_rejection_does_not_retry": True,
                          "simulation_not_hardware_evidence": True}))
    finally:
        server.terminate()
        server.wait(timeout=5)
