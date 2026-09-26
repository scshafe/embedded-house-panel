#!/usr/bin/env python3
"""Run C++ study.v1 create/sync/replay/restart against the real fixture HTTPS server."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import urllib.error
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="bhp-study-https-") as directory:
        root = Path(directory)
        cert, key = root / "fixture.pem", root / "fixture-key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=fixture.local", "-addext", "subjectAltName=IP:127.0.0.1",
                        "-keyout", str(key), "-out", str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.chmod(key, 0o600)
        server = Path(__file__).resolve().parents[1] / "tools/fixture_study_server.py"
        for scenario, initial_mode, expected_events in [("rollover", "create", 5), ("response-loss", "crash_after_send", 3), ("initial-create-loss", "crash_after_create", 5)]:
            server_state, client_state = root / (scenario + "-server"), root / (scenario + "-client")
            for iteration in range(2):
                process = subprocess.Popen(["python3", str(server), "--certificate", str(cert), "--private-key", str(key),
                                            "--state-directory", str(server_state), "--port", "0"], stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, text=True)
                try:
                    assert process.stdout is not None
                    ready = json.loads(process.stdout.readline())
                    assert ready["message"] == "fixture_ready"
                    url = ready["url"]
                    completed = subprocess.run([str(args.client.resolve()), url, str(cert), str(client_state),
                                                initial_mode if iteration == 0 else ("resume_create" if initial_mode == "crash_after_create" else "restore")], check=False, capture_output=True, text=True)
                    if initial_mode in {"crash_after_send", "crash_after_create"} and iteration == 0:
                        assert completed.returncode == (89 if initial_mode == "crash_after_create" else 90), completed.stderr
                        if initial_mode == "crash_after_create":
                            server_data = json.loads((server_state / "fixture-state.json").read_text())
                            local_data = json.loads((client_state / "study-state.json").read_text())
                            assert len(server_data["creates"]) == 1
                            assert local_data["session"] == "" and local_data["pending"] == []
                            assert local_data["initial_request"]["creation_request_id"] == next(iter(server_data["creates"]))
                    else:
                        assert completed.returncode == 0, completed.stderr
                        result = json.loads(completed.stdout)
                        assert result["saved"] and result["event_count"] == (5 if iteration == 0 or initial_mode == "crash_after_create" else 0)
                    context = ssl.create_default_context(cafile=str(cert))
                    control = urllib.request.Request(url + "/fixture/control", data=b'{"offline":true}', headers={"Content-Type": "application/json"})
                    with urllib.request.urlopen(control, context=context) as response:
                        assert json.load(response)["data"]["offline"]
                    control.data = b'{"offline":false}'
                    with urllib.request.urlopen(control, context=context) as response:
                        assert not json.load(response)["data"]["offline"]
                finally:
                    process.terminate()
                    process.communicate(timeout=5)
            persisted = json.loads((server_state / "fixture-state.json").read_text())
            assert len(persisted["events"]) == expected_events
            if initial_mode == "crash_after_create":
                assert len(persisted["creates"]) == 2  # One initial UUID plus its successor.
                assert len(persisted["sessions"]) == 2
            assert {event["event"]["kind"] for event in persisted["events"].values()} == ({"presented", "revealed", "rated", "rating_voided"} if expected_events == 5 else {"presented", "revealed", "rated"})
            assert len(persisted["streams"]) == 1
            assert next(iter(persisted["streams"].values()))["through"] == expected_events
        print(json.dumps({"test": "study_fixture_https_roundtrip", "passed": True, "verified": ["TLS", "create", "same_batch_present_reveal_rate_void", "idempotent_replay", "client_restart", "server_restart", "stream_sequence", "flush_receipt", "offline_control", "server_authoritative_successor", "late_predecessor_correction", "stable_successor_replay", "server_commit_client_crash_replay", "initial_create_commit_client_crash_exact_replay"]}))


if __name__ == "__main__":
    main()
