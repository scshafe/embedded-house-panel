from __future__ import annotations
import copy
import json
import subprocess
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from media_broker.fake_media_broker import BrokerError, FakeMediaBroker
from fixtures import Clock, DAY, PRIVATE_CANARY, ROOT, SYNTHETIC_AUDIO

class BrokerTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock(); self.broker = FakeMediaBroker(ROOT, self.clock)
        self.broker.set_context(DAY)

    def assert_code(self, code, function):
        with self.assertRaises(BrokerError) as error: function()
        self.assertEqual(error.exception.code, "MEDIA_BROKER." + code)

    def test_complete_phrase_one_intent_replay_cooldown(self):
        first = self.broker.utterance("Lexi flip", 1)["intent"]
        self.assertEqual(first["intent_id"], "study.flip")
        self.assertEqual(first["source_kind"], "voice")
        self.assert_code("UTTERANCE_REPLAY", lambda: self.broker.utterance("Lexi next", 1))
        self.assert_code("COOLDOWN", lambda: self.broker.utterance("Lexi next", 2))
        self.clock.advance_ms(301)
        self.assert_code("ABSTAIN", lambda: self.broker.utterance("please Lexi flip now", 3))
        self.assert_code("ABSTAIN", lambda: self.broker.utterance("Lexi flip Lexi next", 4))
        self.assertEqual(self.broker.utterance("LEXI, NEXT", 5)["intent"]["intent_id"], "study.next")

    def test_rating_projection_requires_revealed_back_and_excludes_undo(self):
        self.assert_code("ABSTAIN", lambda: self.broker.utterance("Lexi good", 1))
        self.broker.set_context({**DAY, "back_revealed": True, "render_generation": 2})
        for sequence, rating in enumerate(("again", "hard", "good", "easy"), 2):
            self.clock.advance_ms(301)
            self.assertEqual(self.broker.utterance("Lexi " + rating, sequence)["intent"]["intent_id"], "study.rate." + rating)
        self.clock.advance_ms(301)
        self.assert_code("ABSTAIN", lambda: self.broker.utterance("Lexi undo", 6))

    def test_each_lifecycle_closure_erases_buffer_and_cancels_lease(self):
        for key, value in (("display_mode", "night_red"), ("presence", False), ("surface", "agenda"), ("surface", "home"),
                           ("idle", False), ("muted", True), ("consumer_connected", False)):
            with self.subTest(key=key):
                self.broker.set_context(DAY)
                self.broker.append_test_audio(SYNTHETIC_AUDIO)
                result = self.broker.set_context({**DAY, key: value})
                self.assertEqual(result["state"], "closed")
                self.assertEqual(self.broker.buffer, bytearray())
                self.assertIsNone(self.broker.current_lease)

    def test_home_context_is_valid_but_rejects_recognition_and_old_lease_restart(self):
        lease = self.broker.current_lease
        result = self.broker.set_context({**DAY, "surface": "home", "render_generation": 2})
        self.assertIsNone(result["binding"])
        self.assert_code("WINDOW_CLOSED", lambda: self.broker.utterance("Lexi flip", 1))
        self.assert_code("LEASE_CLOSED", lambda: self.broker.start(lease, self.broker.peer))

    def test_shell_navigation_has_no_spoken_form(self):
        for sequence, phrase in enumerate(("Lexi home", "Lexi open study", "Lexi open agenda"), 1):
            with self.subTest(phrase=phrase):
                self.assert_code("ABSTAIN", lambda: self.broker.utterance(phrase, sequence))

    def test_one_shot_lease_start_closed_expiry_and_peer_binding(self):
        lease = self.broker.arm("command_recognition", self.broker.peer)
        self.broker.start(lease, self.broker.peer)
        self.assert_code("START_ALREADY_CONSUMED", lambda: self.broker.start(lease, self.broker.peer))
        self.broker.close("stop")
        self.assert_code("LEASE_CLOSED", lambda: self.broker.start(lease, self.broker.peer))
        lease = self.broker.arm("command_recognition", self.broker.peer)
        self.assert_code("LEASE_BINDING_MISMATCH", lambda: self.broker.start(lease, "different-peer"))
        lease = self.broker.arm("command_recognition", self.broker.peer)
        self.clock.advance_ms(1000)
        self.assert_code("LEASE_EXPIRED", lambda: self.broker.start(lease, self.broker.peer))
        self.assert_code("PURPOSE_DISABLED", lambda: self.broker.arm("wish_recording", self.broker.peer))

    def test_silence_deadline_and_restart_never_retain_capture(self):
        self.broker.append_test_audio(SYNTHETIC_AUDIO)
        self.clock.advance_ms(1501); self.broker.tick()
        self.assertIsNone(self.broker.current_lease)
        self.assertEqual(self.broker.buffer, bytearray())
        restarted = FakeMediaBroker(ROOT, self.clock)
        self.assertNotEqual(self.broker.source_instance, restarted.source_instance)
        self.assertEqual(restarted.leases, {})

    def test_malformed_start_extra_payload_cannot_supply_provenance(self):
        self.assert_code("REQUEST_INVALID", lambda: self.broker.request({"op": "start", "lease_id": self.broker.current_lease, "touch": True}))
        self.assertIsNone(self.broker.current_lease)

    def test_subprocess_only_semantic_output_and_redacted_errors(self):
        requests = [{"op": "context", "context": DAY}, {"op": "utterance", "phrase": "Lexi flip", "utterance_sequence": 1},
                    {"op": "utterance", "phrase": PRIVATE_CANARY, "utterance_sequence": 2}, {"op": PRIVATE_CANARY}]
        process = subprocess.run([sys.executable, str(ROOT / "media_broker/fake_media_broker.py"), "--root", str(ROOT)],
                                 input="".join(json.dumps(request) + "\n" for request in requests), capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 0)
        self.assertNotIn(PRIVATE_CANARY, process.stdout + process.stderr)
        self.assertNotIn("Lexi flip", process.stdout + process.stderr)
        outputs = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual(outputs[1]["intent"]["intent_id"], "study.flip")
        self.assertFalse(outputs[-1]["ok"])

    def test_subprocess_duplicate_and_oversized_frames_fail_bounded(self):
        for data in ('{"op":"stop","op":"start"}\n', '{"op":"' + PRIVATE_CANARY * 500 + '"}\n'):
            process = subprocess.run([sys.executable, str(ROOT / "media_broker/fake_media_broker.py"), "--root", str(ROOT)], input=data, capture_output=True, text=True, timeout=10)
            self.assertEqual(process.returncode, 0)
            self.assertFalse(json.loads(process.stdout)["ok"])
            self.assertNotIn(PRIVATE_CANARY, process.stdout + process.stderr)

if __name__ == "__main__": unittest.main()
