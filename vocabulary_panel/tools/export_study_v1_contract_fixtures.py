#!/usr/bin/env python3
"""Export provenance-stamped study.v1 examples from the Lexinet authority."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts" / "lexinet"))
from pinned_source import PinnedSourceError, resolve_source  # noqa: E402


EXPECTED_CONTRACT_ID = "study.v1"
EXPECTED_SCHEMA_VERSION = 1
EXPORT_SCHEMA_VERSION = 1
MAX_SOURCE_JSON_BYTES = 4 * 1024 * 1024
MAX_EXAMPLES = 128
DESTINATION_ROOT = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "study_v1"


class ExportError(RuntimeError):
    """A stable, content-free export failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class ExportedFile:
    source_path: str
    export_path: str
    schema: str
    expected: str
    contents: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.contents).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export Lexinet study.v1 examples with deterministic provenance."
    )
    parser.add_argument(
        "--lexinet-source-dir",
        type=Path,
        help=(
            "Explicit Lexinet checkout containing contracts/study.v1. Without it, the pinned "
            "revision is fetched from GitHub into .build/lexinet/source/."
        ),
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if the checked-in export differs; do not write files.",
    )
    return parser.parse_args()


def load_json_object(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    try:
        with path.open("rb") as input_file:
            contents = input_file.read(MAX_SOURCE_JSON_BYTES + 1)
    except OSError as error:
        raise ExportError("SOURCE_READ_FAILED", f"could not read {label}") from error
    if len(contents) > MAX_SOURCE_JSON_BYTES:
        raise ExportError("SOURCE_TOO_LARGE", f"{label} exceeds the exporter size bound")

    try:
        value = json.loads(contents)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ExportError("SOURCE_JSON_INVALID", f"{label} is not valid JSON") from error
    if not isinstance(value, dict):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a JSON object")
    return value, contents


def required_string(value: dict[str, Any], key: str, label: str) -> str:
    member = value.get(key)
    if not isinstance(member, str) or not member:
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.{key} must be a nonempty string")
    return member


def safe_relative_path(value: str, label: str) -> PurePosixPath:
    relative = PurePosixPath(value)
    if relative.is_absolute() or not relative.parts or ".." in relative.parts:
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a safe relative path")
    return relative


def repository_revision(source_dir: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(source_dir), "rev-parse", "--verify", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise ExportError(
            "SOURCE_REVISION_UNAVAILABLE", "could not resolve the Lexinet repository revision"
        ) from error
    revision = result.stdout.strip()
    if len(revision) != 40 or any(character not in "0123456789abcdef" for character in revision):
        raise ExportError(
            "SOURCE_REVISION_INVALID", "Lexinet repository revision is not a full SHA"
        )
    return revision


def string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be a nonempty array")
    if any(not isinstance(item, str) or not item for item in value):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} entries must be nonempty strings")
    if len(set(value)) != len(value):
        raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} entries must be unique")
    return value


def build_export(source_dir: Path) -> tuple[dict[str, Any], list[ExportedFile]]:
    source_dir = source_dir.resolve()
    contract_root = source_dir / "contracts" / EXPECTED_CONTRACT_ID
    manifest, manifest_contents = load_json_object(contract_root / "manifest.json", "manifest")

    contract_id = required_string(manifest, "contract_id", "manifest")
    schema_version = manifest.get("schema_version")
    if contract_id != EXPECTED_CONTRACT_ID or schema_version != EXPECTED_SCHEMA_VERSION:
        raise ExportError(
            "CONTRACT_IDENTITY_MISMATCH",
            f"expected {EXPECTED_CONTRACT_ID} schema {EXPECTED_SCHEMA_VERSION}",
        )
    required_semantics = string_list(
        manifest.get("required_semantics"), "manifest.required_semantics"
    )

    openapi_relative = safe_relative_path(
        required_string(manifest, "openapi_path", "manifest"), "manifest.openapi_path"
    )
    openapi_path = contract_root.joinpath(*openapi_relative.parts)
    openapi, openapi_contents = load_json_object(openapi_path, "OpenAPI document")
    extension = openapi.get("x-lexinet-contract")
    if not isinstance(extension, dict):
        raise ExportError(
            "SOURCE_SCHEMA_INVALID", "OpenAPI x-lexinet-contract must be an object"
        )
    if extension.get("canonical") != contract_id:
        raise ExportError("CONTRACT_IDENTITY_MISMATCH", "OpenAPI canonical identity diverges")
    if extension.get("required_semantics") != required_semantics:
        raise ExportError("CONTRACT_SEMANTICS_MISMATCH", "OpenAPI required semantics diverge")

    examples = manifest.get("examples")
    if not isinstance(examples, list) or not examples:
        raise ExportError("SOURCE_SCHEMA_INVALID", "manifest.examples must be a nonempty array")
    if len(examples) > MAX_EXAMPLES:
        raise ExportError("SOURCE_SCHEMA_INVALID", "manifest.examples exceeds the exporter bound")

    exported: list[ExportedFile] = []
    seen_paths: set[str] = set()
    for index, entry in enumerate(examples):
        label = f"manifest.examples[{index}]"
        if not isinstance(entry, dict):
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label} must be an object")
        source_path_text = required_string(entry, "path", label)
        source_relative = safe_relative_path(source_path_text, f"{label}.path")
        if source_relative.parts[0] != "examples":
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.path must be under examples")
        if source_path_text in seen_paths:
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.path is duplicated")
        seen_paths.add(source_path_text)
        schema = required_string(entry, "schema", label)
        expected = required_string(entry, "expected", label)
        if expected not in {"valid", "invalid"}:
            raise ExportError("SOURCE_SCHEMA_INVALID", f"{label}.expected is unsupported")

        source_path = contract_root.joinpath(*source_relative.parts)
        _, contents = load_json_object(source_path, source_path_text)
        exported.append(
            ExportedFile(
                source_path=source_path_text,
                export_path=source_path_text,
                schema=schema,
                expected=expected,
                contents=contents,
            )
        )

    bundle_hash = hashlib.sha256()
    source_files = [
        ("manifest.json", manifest_contents),
        (openapi_relative.as_posix(), openapi_contents),
        *((item.source_path, item.contents) for item in exported),
    ]
    for path, contents in sorted(source_files):
        bundle_hash.update(path.encode("utf-8"))
        bundle_hash.update(b"\0")
        bundle_hash.update(contents)
        bundle_hash.update(b"\0")

    provenance: dict[str, Any] = {
        "export_schema_version": EXPORT_SCHEMA_VERSION,
        "contract": {
            "id": contract_id,
            "schema_version": schema_version,
            "required_semantics": required_semantics,
        },
        "source": {
            "repository": "lexinet",
            "repository_base_revision": repository_revision(source_dir),
            "contract_root": f"contracts/{contract_id}",
            "manifest_path": f"contracts/{contract_id}/manifest.json",
            "manifest_sha256": hashlib.sha256(manifest_contents).hexdigest(),
            "openapi_path": f"contracts/{contract_id}/{openapi_relative.as_posix()}",
            "openapi_sha256": hashlib.sha256(openapi_contents).hexdigest(),
            "contract_bundle_sha256": bundle_hash.hexdigest(),
        },
        "fixtures": [
            {
                "source_path": item.source_path,
                "export_path": item.export_path,
                "schema": item.schema,
                "expected": item.expected,
                "sha256": item.sha256,
            }
            for item in exported
        ],
    }
    return provenance, exported


def encoded_provenance(provenance: dict[str, Any]) -> bytes:
    return (json.dumps(provenance, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def expected_outputs(
    provenance: dict[str, Any], exported: list[ExportedFile]
) -> dict[Path, bytes]:
    outputs = {DESTINATION_ROOT / item.export_path: item.contents for item in exported}
    outputs[DESTINATION_ROOT / "contract-provenance.json"] = encoded_provenance(provenance)
    return outputs


def check_outputs(outputs: dict[Path, bytes]) -> None:
    mismatches: list[str] = []
    for path, expected in outputs.items():
        try:
            actual = path.read_bytes()
        except OSError:
            mismatches.append(str(path.relative_to(DESTINATION_ROOT)))
            continue
        if actual != expected:
            mismatches.append(str(path.relative_to(DESTINATION_ROOT)))

    expected_json_paths = {path.resolve() for path in outputs}
    if DESTINATION_ROOT.exists():
        for path in DESTINATION_ROOT.rglob("*.json"):
            if path.resolve() not in expected_json_paths:
                mismatches.append(str(path.relative_to(DESTINATION_ROOT)))

    if mismatches:
        paths = ", ".join(sorted(set(mismatches)))
        raise ExportError("EXPORTED_FIXTURES_STALE", f"fixture export differs: {paths}")


def write_outputs(outputs: dict[Path, bytes]) -> None:
    for path, contents in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)


def main() -> int:
    args = parse_args()
    try:
        source_dir, _ = resolve_source(args.lexinet_source_dir)
        provenance, exported = build_export(source_dir)
        outputs = expected_outputs(provenance, exported)
        if args.check:
            check_outputs(outputs)
        else:
            write_outputs(outputs)
            check_outputs(outputs)
    except (ExportError, PinnedSourceError) as error:
        print(
            json.dumps({"ok": False, "error": {"code": error.code, "message": str(error)}}),
            file=sys.stderr,
        )
        return 1

    print(
        json.dumps(
            {
                "ok": True,
                "mode": "check" if args.check else "export",
                "contract_id": provenance["contract"]["id"],
                "schema_version": provenance["contract"]["schema_version"],
                "fixtures": len(exported),
                "contract_bundle_sha256": provenance["source"]["contract_bundle_sha256"],
            }
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
