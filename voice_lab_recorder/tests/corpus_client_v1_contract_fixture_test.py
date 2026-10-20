#!/usr/bin/env python3
"""Verify the panel's provenance-stamped voice-lab corpus-client.v1 fixtures (offline).

The export (scripts/voice_lab/export_corpus_client_v1_fixtures.py) copies the
bundle's examples byte for byte; this test checks the copy against its
provenance and checks what the bundle's PROTOCOL.md promises about them: the
golden take's WAV rebuilt from its recipe, the take id, the READY digests, the
continuity rule in integer nanoseconds, and the canonical form.
"""

from __future__ import annotations

import hashlib
import json
import re
import struct
import unittest
from pathlib import Path, PurePosixPath
from typing import Any

FIXTURE_ROOT = Path(__file__).resolve().parent / "fixtures" / "corpus_client_v1"
PROVENANCE_PATH = FIXTURE_ROOT / "contract-provenance.json"
PINNED_REVISION = "8f516567a38a1ef10cae23060623633791b24b2f"
PINNED_MANIFEST_SHA256 = "845038524025c37d09e30ec6efdc8dcf6334e55dbecf5e16f8d534e7bdf6475b"
PINNED_BUNDLE_SHA256 = "496302a49ba4123816a87dd487d0fb175c02e3bf9448cf21ddf1a60f3db945d1"
EXPECTED_REQUIRED_SEMANTICS = [
    "device_code_sign_in_without_resource",
    "single_flight_refresh_rotation",
    "standing_approval_and_ledger_entry_before_the_first_byte",
    "corpus_take_v2_in_five_files",
    "canonical_json_byte_for_byte",
    "alsa_continuity_rule_in_integer_nanoseconds",
    "profile_scoped_library_and_history",
    "resumable_chunked_upload",
    "finalize_takes_only_the_clients_profile",
    "decisions_with_stable_request_ids_and_expected_revisions",
]
SHA256 = re.compile(r"[0-9a-f]{64}")
#: Nanoseconds per frame at 16 kHz (PROTOCOL.md section 3).
NS_PER_FRAME_16K = 62_500
SAMPLE_WIDTHS = {"S16_LE": 2, "S24_3LE": 3, "S32_LE": 4}


def canonical(value: Any) -> bytes:
    """PROTOCOL.md section 8: sorted keys, no spaces, UTF-8 unescaped, repr floats, one newline."""
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
                       allow_nan=False) + "\n").encode("utf-8")


def fixture(path: str) -> bytes:
    return (FIXTURE_ROOT / path).read_bytes()


def record(path: str) -> dict[str, Any]:
    value = json.loads(fixture(path))
    assert isinstance(value, dict)
    return value


def wav_from_recipe(recipe: dict[str, Any]) -> bytes:
    """PROTOCOL.md section 9: the 44-byte header, then the interleaved sawtooth."""
    width = SAMPLE_WIDTHS[recipe["format"]]
    channels, frames, rate = recipe["channels"], recipe["frames"], recipe["rate"]
    data_bytes = frames * channels * width
    header = (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVE"
              + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * channels * width,
                                      channels * width, width * 8)
              + b"data" + struct.pack("<I", data_bytes))
    samples = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            value = (((frame * (channel + 3) + recipe["offset"]) % recipe["modulus"]
                      - recipe["center"]) * (channel + 1) * recipe["scale"])
            samples += value.to_bytes(width, "little", signed=True)
    return header + bytes(samples)


def continuity_deviations(periods: list[list[int]], rate: int) -> list[int]:
    assert rate == 16_000
    return [abs((t1 - t0) - (p1 - p0) * NS_PER_FRAME_16K)
            for (p0, t0), (p1, t1) in zip(periods, periods[1:])]


class CorpusClientV1ContractFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))

    def test_the_pin_is_the_relayed_revision_and_digests(self) -> None:
        source = self.provenance["source"]
        self.assertEqual(source["repository"], "voice-lab")
        self.assertEqual(source["repository_base_revision"], PINNED_REVISION)
        self.assertEqual(source["manifest_sha256"], PINNED_MANIFEST_SHA256)
        self.assertEqual(source["contract_bundle_sha256"], PINNED_BUNDLE_SHA256)
        self.assertRegex(source["openapi_sha256"], SHA256)
        contract = self.provenance["contract"]
        self.assertEqual((contract["id"], contract["schema_version"]), ("corpus-client.v1", 1))
        self.assertEqual(contract["required_semantics"], EXPECTED_REQUIRED_SEMANTICS)

    def test_every_exported_file_matches_its_provenance_and_nothing_else_is_there(self) -> None:
        listed = set()
        for entry in self.provenance["fixtures"]:
            path = PurePosixPath(entry["export_path"])
            self.assertFalse(path.is_absolute() or ".." in path.parts, entry["export_path"])
            self.assertEqual(entry["export_path"], entry["source_path"])
            contents = fixture(entry["export_path"])
            self.assertEqual(hashlib.sha256(contents).hexdigest(), entry["sha256"], entry["export_path"])
            self.assertEqual(len(contents), entry["size_bytes"], entry["export_path"])
            self.assertIn(entry["expected"], {"valid", "invalid"})
            self.assertEqual("refusal" in entry, entry["expected"] == "invalid", entry["export_path"])
            listed.add(entry["export_path"])
        present = {path.relative_to(FIXTURE_ROOT).as_posix() for path in FIXTURE_ROOT.rglob("*")
                   if path.is_file()} - {PROVENANCE_PATH.name}
        self.assertEqual(present, listed)

    def test_the_golden_take_wav_is_rebuilt_from_its_recipe(self) -> None:
        recipe = record("examples/take/original.wav.recipe.json")
        wav = wav_from_recipe(recipe)
        self.assertEqual(len(wav), recipe["size_bytes"])
        self.assertEqual(hashlib.sha256(wav).hexdigest(), recipe["sha256"])
        width = SAMPLE_WIDTHS[recipe["format"]]
        self.assertEqual(len(wav), 44 + recipe["frames"] * recipe["channels"] * width)

    def test_the_golden_take_records_bind_each_other(self) -> None:
        recipe = record("examples/take/original.wav.recipe.json")
        manifest = record("examples/take/manifest.json")
        continuity = record("examples/take/continuity.json")
        ready = record("examples/take/READY")
        take_id = f"take-{recipe['sha256']}"
        self.assertEqual(manifest["take_id"], take_id)
        self.assertEqual(ready["session_id"], take_id)
        self.assertEqual(manifest["schema_version"], "cole-voice-lab.corpus-take/v2")
        self.assertEqual(manifest["subject_id"], "cole-self")
        self.assertEqual(manifest["audio"]["sha256"], recipe["sha256"])
        self.assertEqual(manifest["audio"]["size_bytes"], recipe["size_bytes"])
        self.assertEqual(continuity["wav"]["sha256"], recipe["sha256"])
        self.assertEqual(ready["manifest_sha256"],
                         hashlib.sha256(fixture("examples/take/manifest.json")).hexdigest())
        self.assertEqual(ready["qc_report_sha256"],
                         hashlib.sha256(fixture("examples/take/qc.json")).hexdigest())
        continuity_bytes = fixture("examples/take/continuity.json")
        self.assertEqual(manifest["continuity"]["sha256"], hashlib.sha256(continuity_bytes).hexdigest())
        self.assertEqual(manifest["continuity"]["size_bytes"], len(continuity_bytes))
        profile = manifest["capture_profile"]
        self.assertEqual(profile["profile"], "xvf3800-linear-4")
        self.assertEqual((profile["device"]["usb_vendor_id"], profile["device"]["usb_product_id"]),
                         (0x2886, 0x0022))
        self.assertEqual(len(profile["channels"]), recipe["channels"])

    def test_the_continuity_rule_in_integer_nanoseconds(self) -> None:
        good = record("examples/take/continuity.json")
        deviations = continuity_deviations(good["periods"], good["rate"])
        self.assertEqual(max(deviations), good["max_abs_deviation_ns"])
        self.assertLessEqual(max(deviations), good["tolerance_ns"])
        self.assertEqual(good["frames"], len(good["periods"]) * good["period_frames"])
        lost = record("examples/invalid/continuity-lost-period.json")
        self.assertGreater(max(continuity_deviations(lost["periods"], lost["rate"])), lost["tolerance_ns"])

    def test_package_records_are_canonical_with_one_newline(self) -> None:
        for path in ("examples/take/manifest.json", "examples/take/qc.json", "examples/take/READY",
                     "examples/take/continuity.json", "examples/home-host-approval.json",
                     "examples/home-host-binding.json", "examples/home-host-ledger-upload-started.json"):
            contents = fixture(path)
            self.assertEqual(canonical(json.loads(contents)), contents, path)

    def test_the_canonical_json_vectors(self) -> None:
        vectors = record("examples/canonical-json-vectors.json")["vectors"]
        self.assertGreaterEqual(len(vectors), 8)
        for vector in vectors:
            self.assertEqual(canonical(vector["value"]), (vector["canonical"] + "\n").encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
