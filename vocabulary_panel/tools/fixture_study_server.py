#!/usr/bin/env python3
"""Loopback-only TLS study.v1 fixture, with persisted synthetic replay/receipt state.

Inputs: explicit certificate, owner-only private key and state directory. Side
 effects: bounded synthetic state and JSON logs. Never forwards external calls;
never a production Lexinet endpoint or scheduler. Restart reuses persisted state.
"""
from __future__ import annotations
import argparse
import copy
import datetime as dt
import fcntl
import hashlib
import http.server
import json
import os
from pathlib import Path
import ssl
import stat
import threading
import time
import uuid
from typing import Any

PROTOCOL = {"protocol_version": "study.v1", "schema_version": 1}
LIMIT = 262144
MAX_EVENTS = 4096
FIXTURE = Path(__file__).resolve().parents[1] / "tests/fixtures/study_v1/examples/session-created.json"


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def digest(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def log(message: str, **fields: Any) -> None:
    print(json.dumps({"timestamp": timestamp(), "level": "info", "service": "fixture_study_server", "message": message,
                      "trace_id": str(uuid.uuid4()), "evidence_class": "simulation_not_hardware_evidence", **fields}), flush=True)


class Rejected(Exception):
    def __init__(self, code: str, status: int = 409):
        self.code, self.status = code, status


class FixtureStudyState:
    """Single authoritative fixture transaction owner; never copies real Lexinet scheduling."""
    def __init__(self, directory: Path):
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        mode = directory.lstat()
        if not stat.S_ISDIR(mode.st_mode) or mode.st_uid != os.getuid() or mode.st_mode & 0o077:
            raise RuntimeError("FIXTURE.STATE_DIRECTORY_NOT_OWNER_ONLY")
        self.directory, self.lock = directory, threading.Lock()
        self.lock_file = os.open(directory / "fixture.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        fcntl.flock(self.lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.path = directory / "fixture-state.json"
        if self.path.exists():
            if self.path.is_symlink() or self.path.stat().st_size > 8 * 1024 * 1024:
                raise RuntimeError("FIXTURE.STATE_INVALID")
            self.state = json.loads(self.path.read_text())
            if self.state["schema_version"] != 2:
                raise RuntimeError("FIXTURE.STATE_VERSION_INVALID")
        else:
            seed = json.loads(FIXTURE.read_text())
            self.state = {"schema_version": 2, "session": seed, "sessions": {seed["session_id"]: seed}, "active_session_id": seed["session_id"], "session_versions": {seed["session_id"]: 1}, "events": {}, "streams": {},
                          "syncs": {}, "creates": {}, "ordinal": 40,
                          "controls": {"offline": False, "terminal_rejection": False, "delay_ms": 0}}
            self.save(self.state)

    def save(self, value: dict[str, Any]) -> None:
        payload = json.dumps(value, separators=(",", ":")).encode()
        if len(payload) > 8 * 1024 * 1024:
            raise Rejected("STUDY_SERVICE_UNAVAILABLE", 503)
        temporary = self.directory / "fixture-state.tmp"
        fd = os.open(temporary, os.O_CREAT | os.O_TRUNC | os.O_WRONLY | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, self.path)
        directory_fd = os.open(self.directory, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
        self.state = value

    @staticmethod
    def validate_identity(request: dict[str, Any]) -> None:
        if request.get("protocol_version") != "study.v1" or type(request.get("schema_version")) is not int or request["schema_version"] != 1:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)

    @staticmethod
    def validate_uuid(value: Any) -> str:
        try:
            if str(uuid.UUID(value)) != value:
                raise ValueError()
        except (ValueError, AttributeError, TypeError):
            raise Rejected("STUDY_SCHEMA_INVALID", 400) from None
        return value

    def session(self, state: dict[str, Any], session_id: str, after: int = -1, limit: int = 64) -> dict[str, Any]:
        if type(after) is not int or type(limit) is not int or after < -1 or not 1 <= limit <= 64:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)
        resource = copy.deepcopy(state["sessions"][session_id])
        cards = [card for card in resource["page"]["items"] if card["position"] > after][:limit]
        resource["page"] = {"after_position": after, "next_after_position": cards[-1]["position"] if cards else after,
                            "complete": not cards or cards[-1]["position"] == state["sessions"][session_id]["page"]["items"][-1]["position"], "items": cards}
        resource["state_version"], resource["profile_ingestion_high_water"], resource["generated_at"] = state["session_versions"][session_id], state["ordinal"], timestamp()
        return resource

    def control(self, request: dict[str, Any]) -> dict[str, Any]:
        if set(request) - {"offline", "terminal_rejection", "delay_ms"}:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)
        state = copy.deepcopy(self.state)
        for key, value in request.items():
            if key == "delay_ms":
                if type(value) is not int or not 0 <= value <= 10000:
                    raise Rejected("STUDY_SCHEMA_INVALID", 400)
            elif type(value) is not bool:
                raise Rejected("STUDY_SCHEMA_INVALID", 400)
            state["controls"][key] = value
        self.save(state)
        return {"data": state["controls"], "error": None, "meta": {"fixture_only": True}}

    def create(self, request: dict[str, Any]) -> dict[str, Any]:
        self.validate_identity(request)
        if set(request) - {"protocol_version", "schema_version", "creation_request_id", "profile_ref", "scope", "predecessor", "page"}:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)
        key = self.validate_uuid(request["creation_request_id"])
        if request["profile_ref"] != self.state["session"]["profile_ref"] or request["scope"] != self.state["session"]["scope"]:
            raise Rejected("STUDY_AUTHORIZATION_DENIED", 403)
        state = copy.deepcopy(self.state)
        normalized = digest(request)
        if key in state["creates"]:
            prior = state["creates"][key]
            if prior["request_hash"] != normalized:
                raise Rejected("IDEMPOTENCY_CONFLICT")
            return prior["response"]
        if len(state["creates"]) >= 256:
            raise Rejected("STUDY_SERVICE_UNAVAILABLE", 503)
        disposition = "converged_to_existing"
        session_id = state["active_session_id"]
        if request.get("predecessor"):
            predecessor_id = self.validate_uuid(request["predecessor"]["session_id"])
            receipt_id = self.validate_uuid(request["predecessor"]["flush_receipt_id"])
            predecessor = state["sessions"].get(predecessor_id)
            if predecessor is None:
                raise Rejected("STUDY_SESSION_NOT_FOUND", 404)
            if predecessor["successor_session_id"]:
                session_id = predecessor["successor_session_id"]
            else:
                receipt = next((entry["response"]["flush_receipt"] for entry in state["syncs"].values()
                                if entry["response"]["flush_receipt"]["receipt_id"] == receipt_id), None)
                if not receipt or receipt["session_id"] != predecessor_id:
                    raise Rejected("STUDY_FLUSH_RECEIPT_REQUIRED")
                if receipt["state_version"] != state["session_versions"][predecessor_id] or receipt["acknowledged_through_sequence"] != state["streams"][receipt["client_stream_id"]]["through"]:
                    raise Rejected("STUDY_FLUSH_RECEIPT_STALE")
                presented = {entry["event"]["session_item_id"] for entry in state["events"].values()
                             if entry["session_id"] == predecessor_id and entry["event"]["kind"] == "presented"}
                if predecessor["lifecycle"] != "active" or not predecessor["page"]["items"] or any(card["session_item_id"] not in presented for card in predecessor["page"]["items"]):
                    raise Rejected("STUDY_CYCLE_NOT_EXHAUSTED")
                if len(state["sessions"]) >= 16:
                    raise Rejected("STUDY_SERVICE_UNAVAILABLE", 503)
                session_id = str(uuid.uuid4())
                successor = copy.deepcopy(predecessor)
                successor["session_id"], successor["predecessor_session_id"], successor["successor_session_id"] = session_id, predecessor_id, None
                successor["creation_receipt"] = None
                successor["scheduler"]["seed"] = "fixture-cycle-" + str(len(state["sessions"]))
                successor["scheduler"]["input_profile_ingestion_high_water"] = state["ordinal"]
                successor["scheduler"]["planned_at"] = timestamp()
                for card in successor["page"]["items"]:
                    card["session_item_id"] = str(uuid.uuid4())
                predecessor["lifecycle"], predecessor["successor_session_id"] = "completed", session_id
                state["session_versions"][predecessor_id] += 1
                state["sessions"][session_id] = successor
                state["session_versions"][session_id] = 1
                state["active_session_id"] = session_id
            disposition = "successor"
        result = self.session(state, session_id, request["page"]["after_position"], request["page"]["limit"])
        result["creation_receipt"] = {"creation_request_id": key, "normalized_request_sha256": normalized,
                                      "disposition": disposition, "canonical_session_id": result["session_id"]}
        state["creates"][key] = {"request_hash": normalized, "response": result}
        self.save(state)
        return result

    def sync(self, session_id: str, request: dict[str, Any]) -> dict[str, Any]:
        self.validate_identity(request)
        if set(request) != {"protocol_version", "schema_version", "sync_id", "client_stream_id", "expected_state_version", "events", "page"}:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)
        if session_id not in self.state["sessions"]:
            raise Rejected("STUDY_SESSION_NOT_FOUND", 404)
        key, stream_id = self.validate_uuid(request["sync_id"]), self.validate_uuid(request["client_stream_id"])
        normalized = digest(request)
        state = copy.deepcopy(self.state)
        if key in state["syncs"]:
            prior = state["syncs"][key]
            if prior["request_hash"] != normalized:
                raise Rejected("IDEMPOTENCY_CONFLICT")
            replay = copy.deepcopy(prior["response"])
            original_page = replay["session"]["page"]
            replay["session"] = self.session(state, session_id)
            replay["session"]["page"] = original_page
            replay["profile_ingestion_high_water"] = state["ordinal"]
            return replay
        if request["expected_state_version"] != state["session_versions"][session_id]:
            raise Rejected("STUDY_STATE_STALE")
        if not isinstance(request["events"], list) or len(request["events"]) > 64:
            raise Rejected("STUDY_SCHEMA_INVALID", 400)
        if len(state["syncs"]) >= 256 or len(state["events"]) + len(request["events"]) > MAX_EVENTS:
            raise Rejected("STUDY_SERVICE_UNAVAILABLE", 503)
        stream = state["streams"].setdefault(stream_id, {"through": 0, "receipts": {}})
        replay, suffix = [], []
        previous = None
        for event in request["events"]:
            self.validate_uuid(event["event_id"])
            sequence = event["client_sequence"]
            if type(sequence) is not int or sequence < 1 or (previous is not None and sequence != previous + 1):
                raise Rejected("STUDY_STREAM_SEQUENCE_GAP")
            previous = sequence
            if sequence <= stream["through"]:
                receipt = stream["receipts"].get(str(sequence))
                if not receipt or receipt["event_id"] != event["event_id"] or receipt["payload_sha256"] != digest(event):
                    raise Rejected("IDEMPOTENCY_CONFLICT")
                replay.append(receipt)
            else:
                if sequence != stream["through"] + len(suffix) + 1:
                    raise Rejected("STUDY_STREAM_SEQUENCE_GAP")
                suffix.append(event)
        tentative = copy.deepcopy(state)
        added, failure = [], None
        for event in suffix:
            try:
                added.append(self.adjudicate(tentative, event, session_id))
            except Rejected as rejected:
                failure = rejected.code
                break
        if state["controls"]["terminal_rejection"]:
            failure = "STUDY_EVENT_TRANSITION_INVALID"
        if failure:
            added = [{"client_sequence": e["client_sequence"], "event_id": e["event_id"], "payload_sha256": digest(e),
                      "disposition": "terminal_rejected", "code": failure, "canonical_event_id": None,
                      "profile_ingestion_ordinal": None} for e in suffix]
        else:
            state = tentative
        stream = state["streams"][stream_id]
        for receipt in added:
            stream["receipts"][str(receipt["client_sequence"])] = receipt
            stream["through"] = receipt["client_sequence"]
        if suffix:
            state["session_versions"][session_id] += 1
        resource = self.session(state, session_id, request["page"]["after_position"], request["page"]["limit"])
        response = {**PROTOCOL, "required_semantics": resource["required_semantics"], "sync_id": key,
                    "normalized_request_sha256": normalized, "client_stream_id": stream_id,
                    "adjudicated_through_sequence": stream["through"], "profile_ingestion_high_water": state["ordinal"],
                    "receipts": replay + added, "session": resource, "generated_at": timestamp(),
                    "flush_receipt": {"receipt_id": str(uuid.uuid4()), "session_id": session_id, "sync_id": key,
                                      "client_stream_id": stream_id, "acknowledged_through_sequence": stream["through"],
                                      "profile_ingestion_high_water": state["ordinal"], "state_version": state["session_versions"][session_id], "issued_at": timestamp()}}
        state["syncs"][key] = {"request_hash": normalized, "response": response}
        self.save(state)
        return response

    def adjudicate(self, state: dict[str, Any], event: dict[str, Any], session_id: str) -> dict[str, Any]:
        common = {"event_id", "client_sequence", "profile_ref", "session_item_id", "card_identity", "content_sha256", "client_clock_quality", "client_observed_at", "kind"}
        kind = event["kind"]
        expected = common | ({"rating"} if kind == "rated" else {"rating_event_id"} if kind == "rating_voided" else set())
        if set(event) != expected or kind not in {"presented", "revealed", "rated", "rating_voided"}:
            raise Rejected("STUDY_EVENT_TRANSITION_INVALID")
        card = next((card for card in state["sessions"][session_id]["page"]["items"] if card["session_item_id"] == event["session_item_id"]), None)
        if not card or event["profile_ref"] != state["session"]["profile_ref"] or card["card_identity"] != event["card_identity"] or card["source_revision"]["content_sha256"] != event["content_sha256"]:
            raise Rejected("STUDY_CONTENT_REVISION_MISMATCH")
        if event["event_id"] in state["events"]:
            prior = state["events"][event["event_id"]]
            if prior["payload_hash"] != digest(event):
                raise Rejected("STUDY_EVENT_ID_CONFLICT")
            return prior["receipt"]
        item_events = [value for value in state["events"].values() if value["event"]["session_item_id"] == event["session_item_id"]]
        duplicate = next((value for value in item_events if value["event"]["kind"] == kind), None) if kind in {"presented", "revealed"} else None
        if duplicate:
            return {"client_sequence": event["client_sequence"], "event_id": event["event_id"], "payload_sha256": digest(event),
                    "disposition": "semantic_duplicate", "code": "STUDY_EVENT_SEMANTIC_DUPLICATE",
                    "canonical_event_id": duplicate["receipt"]["canonical_event_id"], "profile_ingestion_ordinal": duplicate["receipt"]["profile_ingestion_ordinal"]}
        kinds = {value["event"]["kind"] for value in item_events}
        voided = {value["event"]["rating_event_id"] for value in item_events if value["event"]["kind"] == "rating_voided"}
        ratings = [value for value in item_events if value["event"]["kind"] == "rated" and value["event"]["event_id"] not in voided]
        if (kind == "revealed" and "presented" not in kinds) or (kind == "rated" and "revealed" not in kinds):
            raise Rejected("STUDY_EVENT_TRANSITION_INVALID")
        if kind == "rated" and (ratings or event["rating"] not in {"again", "hard", "good", "easy"}):
            raise Rejected("STUDY_RATING_ALREADY_EFFECTIVE")
        if kind == "rating_voided" and (len(ratings) != 1 or ratings[0]["event"]["event_id"] != event["rating_event_id"]):
            raise Rejected("STUDY_RATING_VOID_TARGET_INVALID")
        state["ordinal"] += 1
        receipt = {"client_sequence": event["client_sequence"], "event_id": event["event_id"], "payload_sha256": digest(event),
                   "disposition": "accepted", "code": "STUDY_EVENT_ACCEPTED", "canonical_event_id": event["event_id"],
                   "profile_ingestion_ordinal": state["ordinal"]}
        state["events"][event["event_id"]] = {"event": event, "session_id": session_id, "payload_hash": digest(event), "receipt": receipt}
        return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--certificate", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--state-directory", type=Path, required=True)
    parser.add_argument("--port", type=int, default=9443)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("port must be in 0..65535")
    key_stat = args.private_key.lstat()
    if not stat.S_ISREG(key_stat.st_mode) or key_stat.st_mode & 0o077:
        parser.error("private key must be an owner-only regular file")
    state = FixtureStudyState(args.state_directory)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format: str, *values: Any) -> None:
            pass  # Content-free structured request result is emitted below.

        def do_POST(self) -> None:
            status, body = 200, None
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= LIMIT:
                    raise Rejected("STUDY_REQUEST_TOO_LARGE", 413)
                request = json.loads(self.rfile.read(size))
                if not isinstance(request, dict):
                    raise Rejected("STUDY_SCHEMA_INVALID", 400)
                with state.lock:
                    if self.path == "/fixture/control":
                        body = state.control(request)
                    else:
                        controls = state.state["controls"]
                        time.sleep(controls["delay_ms"] / 1000)
                        if controls["offline"]:
                            raise Rejected("STUDY_SERVICE_UNAVAILABLE", 503)
                        if self.path == "/api/v1/study/sessions":
                            body = state.create(request)
                        elif self.path.startswith("/api/v1/study/sessions/") and self.path.endswith("/sync"):
                            body = state.sync(self.path.split("/")[5], request)
                        else:
                            raise Rejected("STUDY_SESSION_NOT_FOUND", 404)
            except Rejected as rejected:
                status, body = rejected.status, {"error": {**PROTOCOL, "code": rejected.code, "message": "Synthetic fixture rejection"}}
            except (ValueError, KeyError, TypeError):
                status, body = 400, {"error": {**PROTOCOL, "code": "STUDY_SCHEMA_INVALID", "message": "Fixture request malformed"}}
            payload = json.dumps(body, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("X-Request-ID", str(uuid.uuid4()))
            self.end_headers()
            try:
                self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                log("fixture_client_disconnected", status=status)
            log("fixture_request_completed", status=status)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.certificate, args.private_key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    log("fixture_ready", url=f"https://127.0.0.1:{server.server_port}", profile_ref="fixture-default", topic_refs=["fixture-systems", "fixture-words"])
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
