"""The corpus-client.v1 export against a synthetic bundle in a local repository (no network)."""

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/voice_lab"))

import export_corpus_client_v1_fixtures as export  # noqa: E402
import voice_lab_pin  # noqa: E402


def git(cwd: Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=cwd, check=True, text=True,
                          capture_output=True).stdout.strip()


def binding(path: str, contents: bytes) -> dict:
    return {"path": path, "sha256": hashlib.sha256(contents).hexdigest(), "size_bytes": len(contents)}


class ExportCorpusClientV1Test(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-voice-lab-export-test-")
        self.source = Path(self.temporary.name) / "voice-lab"
        self.bundle = self.source / "contracts/corpus-client.v1"
        (self.bundle / "examples/take").mkdir(parents=True)
        self.files = {
            "openapi.json": b'{"openapi":"3.1.0"}\n',
            "README.md": b"readme\n",
            "examples/take/READY": b'{"schema_version":"cole-voice-lab.ready/v1"}\n',
            "examples/bad.json": b'{"unknown":1}\n',
        }
        self.manifest = {
            "manifest_version": 1, "contract_id": "corpus-client.v1", "schema_version": 1,
            "openapi_path": "openapi.json", "required_semantics": ["a", "b"],
            "examples": [
                {"path": "examples/take/READY", "schema": "ReadyMarker", "expected": "valid"},
                {"path": "examples/bad.json", "schema": "X", "expected": "invalid",
                 "refusal": {"where": "the API answers 422", "code": "invalid_request"}},
            ],
        }
        self.write()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, manifest_files: list | None = None) -> None:
        for path, contents in self.files.items():
            (self.bundle / path).write_bytes(contents)
        manifest = dict(self.manifest)
        manifest["files"] = manifest_files if manifest_files is not None else [
            binding(path, contents) for path, contents in sorted(self.files.items())]
        (self.bundle / "manifest.json").write_bytes(json.dumps(manifest).encode() + b"\n")
        if not (self.source / ".git").exists():
            git(self.source, "init", "-q")
        git(self.source, "add", "-A")
        git(self.source, "-c", "user.name=t", "-c", "user.email=t@example.invalid",
            "commit", "-q", "--allow-empty", "-m", "bundle")

    def code(self) -> str:
        with self.assertRaises(export.ExportError) as caught:
            export.build_export(self.source)
        return caught.exception.code

    def test_the_pin_is_the_checked_in_revision(self) -> None:
        self.assertEqual(voice_lab_pin.pinned_revision(), "8f516567a38a1ef10cae23060623633791b24b2f")
        self.assertEqual(voice_lab_pin.pin().name, "VOICE_LAB")

    def test_exports_the_examples_with_voice_labs_bundle_digest(self) -> None:
        provenance, exported = export.build_export(self.source)
        everything = dict(self.files)
        everything["manifest.json"] = (self.bundle / "manifest.json").read_bytes()
        digest = hashlib.sha256()
        for path in sorted(everything):
            digest.update(path.encode() + b"\0" + everything[path] + b"\0")
        source = provenance["source"]
        self.assertEqual(source["contract_bundle_sha256"], digest.hexdigest())
        self.assertEqual(source["bundle_files"], 5)
        self.assertEqual(source["repository_base_revision"], git(self.source, "rev-parse", "HEAD"))
        self.assertEqual([item.source_path for item in exported], ["examples/take/READY", "examples/bad.json"])
        self.assertEqual(provenance["fixtures"][1]["refusal"]["code"], "invalid_request")
        self.assertNotIn("refusal", provenance["fixtures"][0])

    def test_a_file_that_differs_from_its_binding_is_refused(self) -> None:
        bindings = [binding(path, contents) for path, contents in sorted(self.files.items())]
        self.files["README.md"] = b"changed\n"
        self.write(bindings)
        self.assertEqual(self.code(), "BUNDLE_FILE_MISMATCH")

    def test_an_unbound_file_is_refused(self) -> None:
        bindings = [binding(path, contents) for path, contents in sorted(self.files.items())]
        self.files["extra.json"] = b"{}\n"
        self.write(bindings)
        self.assertEqual(self.code(), "BUNDLE_FILE_UNBOUND")

    def test_a_missing_bound_file_is_refused(self) -> None:
        bindings = [binding(path, contents) for path, contents in sorted(self.files.items())]
        (self.bundle / "README.md").unlink()
        del self.files["README.md"]
        self.write(bindings)
        self.assertEqual(self.code(), "BUNDLE_FILE_MISSING")

    def test_another_contract_is_refused(self) -> None:
        self.manifest["contract_id"] = "corpus-client.v2"
        self.write()
        self.assertEqual(self.code(), "CONTRACT_IDENTITY_MISMATCH")

    def test_an_invalid_example_needs_its_refusal(self) -> None:
        del self.manifest["examples"][1]["refusal"]
        self.write()
        self.assertEqual(self.code(), "SOURCE_SCHEMA_INVALID")

    def test_check_finds_a_changed_or_extra_fixture(self) -> None:
        provenance, exported = export.build_export(self.source)
        destination = Path(self.temporary.name) / "fixtures"
        outputs = export.expected_outputs(provenance, exported, destination)
        export.write_outputs(outputs)
        export.check_outputs(outputs, destination)
        (destination / "examples/stray.json").write_bytes(b"{}\n")
        with self.assertRaises(export.ExportError) as caught:
            export.check_outputs(outputs, destination)
        self.assertEqual(caught.exception.code, "EXPORTED_FIXTURES_STALE")
        (destination / "examples/stray.json").unlink()
        (destination / "examples/take/READY").write_bytes(b"{}\n")
        with self.assertRaises(export.ExportError):
            export.check_outputs(outputs, destination)


if __name__ == "__main__":
    unittest.main()
