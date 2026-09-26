#!/usr/bin/env python3
"""Bounded JSONL deterministic command-recognition fixture process.

Usage: python3 media_broker/fake_media_broker.py --root REPO
No ALSA, microphone, network, transcript output, or filesystem mutation. This is
an explicitly fake local process boundary, never production peer authentication.
"""
from __future__ import annotations
import argparse
import json
import os
import secrets
import select
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Protocol

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from shared.spoken_command_catalog.catalog import CatalogError, build_bundle, canonical, normalize_text, verify_policy

MAXIMUM_LINE_BYTES = 8192
MAXIMUM_BUFFER_BYTES = 65536
MAXIMUM_LEASES = 32
LEASE_START_NS = 1_000_000_000
LEASE_LIFETIME_NS = 30_000_000_000
CONTEXT_KEYS = {"display_mode", "presence", "surface", "idle", "muted", "consumer_connected",
                "active_context_nonce", "render_generation", "back_revealed", "history_available"}

class BrokerError(ValueError):
    def __init__(self, reason: str, operation: str):
        self.code, self.operation = "MEDIA_BROKER." + reason, operation
        super().__init__(self.code)

class CommandRecognizer(Protocol):
    """@extension-point CommandRecognizer: bounded complete-phrase semantic result only."""
    def recognize(self, phrase: str, forms: list[dict]) -> dict | None: ...

class DeterministicFakeRecognizer:
    def recognize(self, phrase: str, forms: list[dict]) -> dict | None:
        matched = [form for form in forms if normalize_text(form["phrase"]) == normalize_text(phrase)]
        return matched[0] if len(matched) == 1 else None

@dataclass(frozen=True)
class LeaseBinding:
    lease_id: str
    peer: str
    purpose: str
    context_nonce: str
    render_generation: int
    start_deadline_ns: int
    expires_ns: int

class FakeMediaBroker:
    """One fake consumer, one-shot leases, and fail-closed volatile lifetime.

    Lifecycle is scripted fixture input. No production trust or physical closure
    evidence is inferred from this class or its telemetry.
    """
    def __init__(self, root: Path, clock: Callable[[], int] = time.monotonic_ns):
        self.root, self.clock = root, clock
        self.recognizer: CommandRecognizer = DeterministicFakeRecognizer()
        self.source_instance = "fake-broker-" + secrets.token_hex(16)
        self.peer = "inherited-private-stdio"
        self.context: dict | None = None
        self.bundle: dict | None = None
        self.leases: dict[str, tuple[LeaseBinding, str, str]] = {}
        self.current_lease: str | None = None
        self.buffer = bytearray()
        self.latest_sequence = 0
        self.latest_accepted_ns: int | None = None
        self.last_clock_ns: int | None = None
        self.last_activity_ns: int | None = None

    def now(self) -> int:
        current = self.clock()
        if type(current) is not int or current < 0 or (self.last_clock_ns is not None and current < self.last_clock_ns):
            self.close("clock_failure")
            raise BrokerError("CLOCK_INVALID", "clock")
        self.last_clock_ns = current
        return current

    def wipe(self) -> None:
        """Overwrite mutable fixture bytes before releasing their storage."""
        for i in range(len(self.buffer)):
            self.buffer[i] = 0
        self.buffer.clear()

    def close(self, reason: str) -> None:
        for lease_id, (binding, state, previous_reason) in list(self.leases.items()):
            if state != "closed": self.leases[lease_id] = (binding, "closed", reason)
        self.current_lease = None
        self.wipe()

    def tick(self) -> None:
        """Deadline closure without waiting for an utterance or a consumer response."""
        now = self.now()
        for binding, state, _ in self.leases.values():
            if (state == "armed" and now >= binding.start_deadline_ns) or (state == "active" and now >= binding.expires_ns):
                self.close("expired"); return
        if self.current_lease and self.bundle and self.last_activity_ns is not None and now - self.last_activity_ns >= self.bundle["policy"]["silence"]["timeout_ms"] * 1_000_000:
            self.close("silence_timeout")

    def append_test_audio(self, data: bytes) -> None:
        """In-memory test hook only; deliberately absent from process JSONL operations."""
        if not self.current_lease or len(data) + len(self.buffer) > MAXIMUM_BUFFER_BYTES:
            self.close("buffer_bound")
            raise BrokerError("BUFFER_BOUND", "append_test_audio")
        self.buffer.extend(data)

    def eligible(self) -> bool:
        c = self.context
        return bool(c and c["display_mode"] == "day_active" and c["presence"] and c["surface"] == "study"
                    and c["idle"] and not c["muted"] and c["consumer_connected"])

    def arm(self, purpose: str, peer: str) -> str:
        now = self.now()
        if purpose != "command_recognition": raise BrokerError("PURPOSE_DISABLED", "arm")
        if peer != self.peer or not self.eligible(): raise BrokerError("WINDOW_CLOSED", "arm")
        self.close("superseded")
        while len(self.leases) >= MAXIMUM_LEASES:
            self.leases.pop(next(iter(self.leases)))
        lease_id = "lease-" + secrets.token_hex(16)
        binding = LeaseBinding(lease_id, peer, purpose, self.context["active_context_nonce"],
                               self.context["render_generation"], now + LEASE_START_NS, now + LEASE_LIFETIME_NS)
        self.leases[lease_id] = (binding, "armed", "")
        return lease_id

    def start(self, lease_id: str, peer: str) -> None:
        now = self.now()
        if lease_id not in self.leases: raise BrokerError("LEASE_UNKNOWN", "start")
        binding, state, reason = self.leases[lease_id]
        if state == "closed": raise BrokerError("LEASE_CLOSED", "start")
        if state == "active": raise BrokerError("START_ALREADY_CONSUMED", "start")
        if now >= binding.start_deadline_ns or now >= binding.expires_ns:
            self.close("expired"); raise BrokerError("LEASE_EXPIRED", "start")
        if peer != binding.peer or not self.eligible() or binding.context_nonce != self.context["active_context_nonce"] or binding.render_generation != self.context["render_generation"]:
            self.close("binding_mismatch"); raise BrokerError("LEASE_BINDING_MISMATCH", "start")
        self.leases[lease_id] = (binding, "active", "")
        self.current_lease = lease_id
        self.last_activity_ns = now

    def set_context(self, context: dict) -> dict:
        if not isinstance(context, dict) or set(context) != CONTEXT_KEYS:
            self.close("malformed_context"); raise BrokerError("CONTEXT_INVALID", "context")
        for name in ("presence", "idle", "muted", "consumer_connected", "back_revealed", "history_available"):
            if type(context[name]) is not bool:
                self.close("malformed_context"); raise BrokerError("CONTEXT_INVALID", "context")
        if context["display_mode"] not in {"day_active", "night_red", "diagnostic", "blank", "suspended"} or context["surface"] not in {"home", "study", "agenda", "night", "blank"} or type(context["render_generation"]) is not int or context["render_generation"] < 1 or not isinstance(context["active_context_nonce"], str) or not 1 <= len(context["active_context_nonce"]) <= 128:
            self.close("malformed_context"); raise BrokerError("CONTEXT_INVALID", "context")
        if self.context != context:
            self.close("context_changed")
            self.context = dict(context)
        if not self.eligible():
            self.close("window_closed"); self.bundle = None
            return {"ok": True, "state": "closed", "binding": None, "evidence": "fixture_only"}
        contexts = ["study_back" if context["back_revealed"] else "study_front"]
        if context["history_available"]: contexts.append("study_history")
        self.bundle = build_bundle(self.root, contexts)
        verify_policy(self.bundle["policy"], self.bundle["backend"])
        if self.current_lease and self.now() >= self.leases[self.current_lease][0].expires_ns:
            self.close("expired")
        if not self.current_lease:
            self.start(self.arm("command_recognition", self.peer), self.peer)
        return {"ok": True, "state": "active", "binding": self.bundle["binding"], "evidence": "fixture_only"}

    def utterance(self, phrase: str, sequence: int) -> dict:
        now = self.now()
        if type(sequence) is not int or sequence < 1 or sequence > 9007199254740991 or sequence <= self.latest_sequence:
            raise BrokerError("UTTERANCE_REPLAY", "utterance")
        self.latest_sequence = sequence
        self.last_activity_ns = now
        if not self.eligible() or not self.current_lease or not self.bundle:
            raise BrokerError("WINDOW_CLOSED", "utterance")
        if now >= self.leases[self.current_lease][0].expires_ns:
            self.close("expired"); raise BrokerError("LEASE_EXPIRED", "utterance")
        if not isinstance(phrase, str) or len(phrase.encode()) > 512:
            self.close("malformed_utterance"); raise BrokerError("UTTERANCE_INVALID", "utterance")
        verify_policy(self.bundle["policy"], self.bundle["backend"])
        if self.latest_accepted_ns is not None and now - self.latest_accepted_ns < max(self.bundle["policy"]["cooldown_ms"], self.bundle["policy"]["debounce_ms"]) * 1_000_000:
            raise BrokerError("COOLDOWN", "utterance")
        self.wipe()
        if not normalize_text(phrase): raise BrokerError("SILENCE", "utterance")
        form = self.recognizer.recognize(phrase, self.bundle["backend"]["forms"])
        if form is None: raise BrokerError("ABSTAIN", "utterance")
        self.latest_accepted_ns = now
        evidence = {key: value for key, value in self.bundle["binding"].items() if key != "spoken_form_bindings"}
        evidence["spoken_form_id"] = form["spoken_form_id"]
        return {"ok": True, "intent": {"protocol_version": 1, "active_context_nonce": self.context["active_context_nonce"],
                "source_instance_id": self.source_instance, "source_kind": "voice", "source_sequence": sequence,
                "observed_monotonic_time_ns": now, "render_generation": self.context["render_generation"],
                "target_surface_id": "study", "intent_id": form["intent_id"], "voice_evidence": evidence}}

    def request(self, request: dict) -> dict:
        if not isinstance(request, dict): raise BrokerError("REQUEST_INVALID", "request")
        op = request.get("op")
        if op == "context" and set(request) == {"op", "context"}: return self.set_context(request["context"])
        if op == "utterance" and set(request) == {"op", "phrase", "utterance_sequence"}: return self.utterance(request["phrase"], request["utterance_sequence"])
        if op == "stop" and set(request) == {"op"}:
            self.close("stop"); return {"ok": True, "state": "closed", "closure": "telemetry_only_no_hardware_evidence"}
        if op == "arm" and set(request) == {"op", "purpose"}:
            return {"ok": True, "lease_id": self.arm(request["purpose"], self.peer)}
        if op == "start" and set(request) == {"op", "lease_id"}:
            self.start(request["lease_id"], self.peer); return {"ok": True, "state": "active"}
        self.close("malformed_request"); raise BrokerError("REQUEST_INVALID", "request")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    broker = FakeMediaBroker(args.root.resolve())
    buffered = bytearray()
    try:
        while True:
            broker.tick()
            if b"\n" not in buffered:
                readable, _, _ = select.select([sys.stdin.fileno()], [], [], .1)
                if not readable: continue
                data = os.read(sys.stdin.fileno(), MAXIMUM_LINE_BYTES + 1 - len(buffered))
                if not data: break
                buffered.extend(data)
            if b"\n" not in buffered and len(buffered) <= MAXIMUM_LINE_BYTES: continue
            split = buffered.find(b"\n")
            line = bytes(buffered if split < 0 else buffered[:split + 1])
            del buffered[:len(line)]
            try:
                if len(line) > MAXIMUM_LINE_BYTES or not line.endswith(b"\n"):
                    raise BrokerError("FRAME_BOUND", "request")
                def unique_pairs(pairs):
                    value = {}
                    for key, item in pairs:
                        if key in value: raise BrokerError("DUPLICATE_FIELD", "request")
                        value[key] = item
                    return value
                request = json.loads(line, object_pairs_hook=unique_pairs)
                canonical(request)
                response = broker.request(request)
            except (BrokerError, CatalogError) as error:
                if error.code not in {"MEDIA_BROKER.ABSTAIN", "MEDIA_BROKER.COOLDOWN", "MEDIA_BROKER.SILENCE", "MEDIA_BROKER.UTTERANCE_REPLAY"}:
                    broker.close("rejected")
                response = {"ok": False, "error": {"code": error.code, "message": "Fake broker request rejected", "context": {"operation": error.operation}}}
            except (ValueError, TypeError, KeyError, OSError, UnicodeError):
                broker.close("malformed_input")
                response = {"ok": False, "error": {"code": "MEDIA_BROKER.REQUEST_INVALID", "message": "Fake broker request rejected", "context": {"operation": "request"}}}
            # Envelope timestamps are uint64 nanoseconds, unlike bounded policy
            # integers; long host uptimes may exceed the I-JSON safe-integer bound.
            sys.stdout.buffer.write(json.dumps(response, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode() + b"\n")
            sys.stdout.buffer.flush()
            if len(line) > MAXIMUM_LINE_BYTES: break
    finally:
        broker.close("consumer_disconnect")
        for i in range(len(buffered)): buffered[i] = 0
        buffered.clear()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
