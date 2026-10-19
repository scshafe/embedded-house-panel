"""The generic contract pin against a local repository (no network), named VOICE_LAB.

A second name proves the mechanism is not lexinet's: error codes take the
contract's prefix, and the cache and provenance are the pin's own.
"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/contracts"))

from contract_pin import (ContractPin, PinnedSourceError, fetch_pinned_source,  # noqa: E402
                          pinned_revision, resolve_source)


def git(cwd: Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=cwd, check=True, text=True,
                          capture_output=True).stdout.strip()


class ContractPinTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-contract-pin-test-")
        base = Path(self.temporary.name)
        remote = base / "remote"
        remote.mkdir()
        git(remote, "init", "-q")
        (remote / "contracts").mkdir()
        (remote / "contracts/manifest.json").write_text("{}\n", encoding="utf-8")
        git(remote, "add", ".")
        git(remote, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "-m", "bundle")
        self.revision = git(remote, "rev-parse", "HEAD")
        self.provenance = base / "contract-provenance.json"
        self.provenance.write_text(json.dumps({"source": {"repository_base_revision": self.revision}}),
                                   encoding="utf-8")
        self.pin = ContractPin("VOICE_LAB", remote.as_uri(), self.provenance, base / "cache")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_fetches_once_and_reuses_the_clean_cache(self) -> None:
        source, how = resolve_source(self.pin, None)
        self.assertEqual(how, "pinned_fetch")
        self.assertEqual(source, self.pin.cache_root / self.revision)
        self.assertTrue((source / "contracts/manifest.json").exists())
        self.assertEqual(fetch_pinned_source(self.pin, self.revision), source)

    def test_a_changed_cache_is_refused_with_the_contract_prefix(self) -> None:
        source = fetch_pinned_source(self.pin, self.revision)
        (source / "contracts/manifest.json").write_text("{\"tampered\":true}\n", encoding="utf-8")
        with self.assertRaises(PinnedSourceError) as raised:
            fetch_pinned_source(self.pin, self.revision)
        self.assertEqual(raised.exception.code, "VOICE_LAB_CACHE_INVALID")

    def test_pin_errors(self) -> None:
        self.provenance.write_text(json.dumps({"source": {"repository_base_revision": "abc"}}), encoding="utf-8")
        with self.assertRaises(PinnedSourceError) as invalid:
            pinned_revision(self.pin)
        self.assertEqual(invalid.exception.code, "VOICE_LAB_PIN_INVALID")
        self.provenance.write_text("not json", encoding="utf-8")
        with self.assertRaises(PinnedSourceError) as unreadable:
            pinned_revision(self.pin)
        self.assertEqual(unreadable.exception.code, "VOICE_LAB_PIN_UNREADABLE")
        missing = ContractPin("VOICE_LAB", "file:///nonexistent-repository", self.provenance, self.pin.cache_root)
        with self.assertRaises(PinnedSourceError) as fetch:
            fetch_pinned_source(missing, "0" * 40)
        self.assertEqual(fetch.exception.code, "VOICE_LAB_PIN_FETCH_FAILED")
        self.assertFalse(any(self.pin.cache_root.glob(".*")), "a failed fetch leaves no staging directory")

    def test_explicit_source_is_used_as_given(self) -> None:
        explicit = Path(self.temporary.name) / "checkout"
        self.assertEqual(resolve_source(self.pin, explicit), (explicit, "explicit"))

    def test_names_are_upper_case(self) -> None:
        with self.assertRaises(ValueError):
            ContractPin("voice-lab", "file:///x", self.provenance, self.pin.cache_root)


if __name__ == "__main__":
    unittest.main()
