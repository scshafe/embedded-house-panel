#!/usr/bin/env python3
"""Export provenance-stamped corpus-client.v1 examples from the voice-lab bundle.

@canonical voice_lab_contract_export -- the panel's copy of voice-lab's examples.

The bundle (``contracts/corpus-client.v1/`` in voice-lab) binds every file but
its ``manifest.json`` by SHA-256 and size, and voice-lab pins it by one
digest: each path in sorted order, hashed as ``path NUL contents NUL``. The
exporter checks both against the pinned source, then copies the manifest's
examples byte for byte into ``voice_lab_recorder/tests/fixtures/corpus_client_v1/``
with ``contract-provenance.json``. ``--check`` writes nothing and fails if the
checked-in export differs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from voice_lab_pin import CONTRACT_ROOT, PinnedSourceError, resolve_source  # noqa: E402

EXPECTED_CONTRACT_ID = "corpus-client.v1"
EXPECTED_SCHEMA_VERSION = 1
EXPECTED_MANIFEST_VERSION = 1
EXPORT_SCHEMA_VERSION = 1
MAX_SOURCE_BYTES = 4 * 1024 * 1024
MAX_BUNDLE_FILES = 256
DESTINATION_ROOT = (Path(__file__).resolve().parents[2]
                    / "voice_lab_recorder/tests/fixtures/corpus_client_v1")
PROVENANCE_NAME = "contract-provenance.json"


class ExportError(RuntimeError):
    """A stable, content-free export failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class ExportedFile:
    source_path: str
    schema: str
    expected: str
    refusal: dict[str, Any] | None
    contents: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.contents).hexdigest()


def read_bounded(path: Path, label: str) -> bytes:
    try:
        with path.open("rb") as source:
            contents = source.read(MAX_SOURCE_BYTES + 1)
    except OSError as error:
        raise ExportError("SOURCE_READ_FAILED", f"could not read {label}") from error
    if len(contents) > MAX_SOURCE_BYTES:
        raise ExportError("SOURCE_TOO_LARGE", f"{label} exceeds the exporter size bound")
    return contents


def json_object(contents: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(contents)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ExportError("SOURCE_JSON_INVALID", f"{label} is not valid JSON") from error
    if not isinstance(value, dict):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a JSON object")
    return value


def required_string(value: dict[str, Any], key: str, label: str) -> str:
    member = value.get(key)
    if not isinstance(member, str) or not member:
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.{key} must be a nonempty string")
    return member


def safe_relative_path(value: str, label: str) -> PurePosixPath:
    relative = PurePosixPath(value)
    if (relative.is_absolute() or not relative.parts or ".." in relative.parts
            or relative.as_posix() != value):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a safe relative path")
    return relative


def string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a nonempty array")
    if any(not isinstance(item, str) or not item for item in value):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} entries must be nonempty strings")
    if len(set(value)) != len(value):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} entries must be unique")
    return value


def repository_revision(source_dir: Path) -> str:
    try:
        result = subprocess.run(["git", "-C", str(source_dir), "rev-parse", "--verify", "HEAD"],
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, timeout=10)
    except (OSError, subprocess.SubprocessError) as error:
        raise ExportError("SOURCE_REVISION_UNAVAILABLE",
                          "could not resolve the voice-lab repository revision") from error
    revision = result.stdout.strip()
    if len(revision) != 40 or any(character not in "0123456789abcdef" for character in revision):
        raise ExportError("SOURCE_REVISION_INVALID", "voice-lab revision is not a full SHA")
    return revision


def bundle_digest(files: dict[str, bytes]) -> str:
    """voice-lab's bundle digest: each path in sorted order, hashed as path NUL contents NUL."""
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(path.encode("utf-8") + b"\0" + files[path] + b"\0")
    return digest.hexdigest()


def bundle_files(contract_root: Path) -> dict[str, bytes]:
    if not contract_root.is_dir():
        raise ExportError("SOURCE_READ_FAILED", f"{CONTRACT_ROOT} is missing")
    paths = sorted(path for path in contract_root.rglob("*") if path.is_file() or path.is_symlink())
    if len(paths) > MAX_BUNDLE_FILES:
        raise ExportError("SOURCE_TOO_LARGE", "the bundle has too many files")
    files: dict[str, bytes] = {}
    for path in paths:
        relative = path.relative_to(contract_root).as_posix()
        if path.is_symlink():
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{relative} is a symbolic link")
        files[relative] = read_bounded(path, relative)
    return files


def check_bound_files(manifest: dict[str, Any], files: dict[str, bytes]) -> None:
    """Every file but manifest.json is bound by SHA-256 and size, and nothing else is there."""
    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise ExportError("SOURCE_SCHEMA_INVALID", "manifest.files must be a nonempty array")
    bound: set[str] = set()
    for index, entry in enumerate(entries):
        label = f"manifest.files[{index}]"
        if not isinstance(entry, dict):
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be an object")
        path = safe_relative_path(required_string(entry, "path", label), f"{label}.path").as_posix()
        if path in bound or path == "manifest.json":
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.path is duplicated")
        bound.add(path)
        contents = files.get(path)
        if contents is None:
            raise ExportError("BUNDLE_FILE_MISSING", f"{path} is bound but missing")
        if (entry.get("sha256") != hashlib.sha256(contents).hexdigest()
                or entry.get("size_bytes") != len(contents)):
            raise ExportError("BUNDLE_FILE_MISMATCH", f"{path} differs from its binding")
    unbound = sorted(set(files) - bound - {"manifest.json"})
    if unbound:
        raise ExportError("BUNDLE_FILE_UNBOUND", f"unbound files: {', '.join(unbound)}")


def build_export(source_dir: Path) -> tuple[dict[str, Any], list[ExportedFile]]:
    source_dir = source_dir.resolve()
    contract_root = source_dir / CONTRACT_ROOT
    files = bundle_files(contract_root)
    if "manifest.json" not in files:
        raise ExportError("SOURCE_READ_FAILED", "manifest.json is missing")
    manifest_contents = files["manifest.json"]
    manifest = json_object(manifest_contents, "manifest")
    contract_id = required_string(manifest, "contract_id", "manifest")
    schema_version = manifest.get("schema_version")
    if (contract_id != EXPECTED_CONTRACT_ID or schema_version != EXPECTED_SCHEMA_VERSION
            or manifest.get("manifest_version") != EXPECTED_MANIFEST_VERSION):
        raise ExportError("CONTRACT_IDENTITY_MISMATCH",
                          f"expected {EXPECTED_CONTRACT_ID} schema {EXPECTED_SCHEMA_VERSION}")
    required_semantics = string_list(manifest.get("required_semantics"),
                                     "manifest.required_semantics")
    check_bound_files(manifest, files)

    openapi_path = safe_relative_path(required_string(manifest, "openapi_path", "manifest"),
                                      "manifest.openapi_path").as_posix()
    if openapi_path not in files:
        raise ExportError("BUNDLE_FILE_MISSING", "the OpenAPI document is missing")
    json_object(files[openapi_path], "OpenAPI document")

    examples = manifest.get("examples")
    if not isinstance(examples, list) or not examples:
        raise ExportError("SOURCE_SCHEMA_INVALID", "manifest.examples must be a nonempty array")
    exported: list[ExportedFile] = []
    seen: set[str] = set()
    for index, entry in enumerate(examples):
        label = f"manifest.examples[{index}]"
        if not isinstance(entry, dict):
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be an object")
        path = safe_relative_path(required_string(entry, "path", label), f"{label}.path")
        if path.parts[0] != "examples" or path.as_posix() in seen:
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.path is invalid or duplicated")
        seen.add(path.as_posix())
        expected = required_string(entry, "expected", label)
        refusal = entry.get("refusal")
        if expected == "valid" and refusal is not None:
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} is valid but has a refusal")
        if expected == "invalid" and not isinstance(refusal, dict):
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} is invalid without a refusal")
        if expected not in {"valid", "invalid"}:
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.expected is unsupported")
        exported.append(ExportedFile(path.as_posix(), required_string(entry, "schema", label),
                                     expected, refusal, files[path.as_posix()]))

    provenance: dict[str, Any] = {
        "export_schema_version": EXPORT_SCHEMA_VERSION,
        "contract": {
            "id": contract_id,
            "schema_version": schema_version,
            "manifest_version": EXPECTED_MANIFEST_VERSION,
            "required_semantics": required_semantics,
        },
        "source": {
            "repository": "voice-lab",
            "repository_base_revision": repository_revision(source_dir),
            "contract_root": CONTRACT_ROOT,
            "manifest_path": f"{CONTRACT_ROOT}/manifest.json",
            "manifest_sha256": hashlib.sha256(manifest_contents).hexdigest(),
            "openapi_path": f"{CONTRACT_ROOT}/{openapi_path}",
            "openapi_sha256": hashlib.sha256(files[openapi_path]).hexdigest(),
            "bundle_files": len(files),
            "contract_bundle_sha256": bundle_digest(files),
        },
        "fixtures": [
            {
                "source_path": item.source_path,
                "export_path": item.source_path,
                "schema": item.schema,
                "expected": item.expected,
                **({"refusal": item.refusal} if item.refusal is not None else {}),
                "sha256": item.sha256,
                "size_bytes": len(item.contents),
            }
            for item in exported
        ],
    }
    return provenance, exported


def encoded_provenance(provenance: dict[str, Any]) -> bytes:
    return (json.dumps(provenance, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def expected_outputs(provenance: dict[str, Any], exported: list[ExportedFile],
                     destination: Path = DESTINATION_ROOT) -> dict[Path, bytes]:
    outputs = {destination / item.source_path: item.contents for item in exported}
    outputs[destination / PROVENANCE_NAME] = encoded_provenance(provenance)
    return outputs


def check_outputs(outputs: dict[Path, bytes], destination: Path = DESTINATION_ROOT) -> None:
    mismatches: list[str] = []
    for path, expected in outputs.items():
        try:
            if path.read_bytes() == expected:
                continue
        except OSError:
            pass
        mismatches.append(path.relative_to(destination).as_posix())
    wanted = {path.resolve() for path in outputs}
    if destination.exists():
        for path in destination.rglob("*"):
            if path.is_file() and path.resolve() not in wanted:
                mismatches.append(path.relative_to(destination).as_posix())
    if mismatches:
        raise ExportError("EXPORTED_FIXTURES_STALE",
                          f"fixture export differs: {', '.join(sorted(set(mismatches)))}")


def write_outputs(outputs: dict[Path, bytes]) -> None:
    for path, contents in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)


def main() -> int:
    parser = argparse.ArgumentParser(description="Export voice-lab corpus-client.v1 examples "
                                     "with deterministic provenance.")
    parser.add_argument("--voice-lab-source-dir", type=Path,
                        help="explicit voice-lab checkout, for repinning; without it the pinned "
                             "revision is fetched from GitHub into .build/voice-lab/source/")
    parser.add_argument("--check", action="store_true",
                        help="fail if the checked-in export differs; write nothing")
    args = parser.parse_args()
    try:
        source_dir, _ = resolve_source(args.voice_lab_source_dir)
        provenance, exported = build_export(source_dir)
        outputs = expected_outputs(provenance, exported)
        if not args.check:
            write_outputs(outputs)
        check_outputs(outputs)
    except (ExportError, PinnedSourceError) as error:
        print(json.dumps({"ok": False, "error": {"code": error.code, "message": str(error)}}),
              file=sys.stderr)
        return 1
    print(json.dumps({
        "ok": True,
        "mode": "check" if args.check else "export",
        "contract_id": provenance["contract"]["id"],
        "repository_base_revision": provenance["source"]["repository_base_revision"],
        "fixtures": len(exported),
        "manifest_sha256": provenance["source"]["manifest_sha256"],
        "contract_bundle_sha256": provenance["source"]["contract_bundle_sha256"],
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
