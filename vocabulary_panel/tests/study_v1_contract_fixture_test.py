#!/usr/bin/env python3
"""Verify the panel's provenance-stamped Lexinet study.v1 fixtures."""

from __future__ import annotations

import copy
import hashlib
import json
import re
import unittest
from pathlib import Path, PurePosixPath
from typing import Any


FIXTURE_ROOT = Path(__file__).resolve().parent / "fixtures" / "study_v1"
PROVENANCE_PATH = FIXTURE_ROOT / "contract-provenance.json"
EXPECTED_PROTOCOL_VERSION = "study.v1"
EXPECTED_SCHEMA_VERSION = 1
EXPECTED_REQUIRED_SEMANTICS = [
    "active_scope_create_or_resume",
    "immutable_complete_card_projection",
    "scheduler_provenance",
    "singular_presented_revealed",
    "append_only_rated_rating_voided",
    "durable_client_stream_terminal_receipts",
    "server_authoritative_cycle_exhaustion",
    "atomic_predecessor_successor_linking",
    "predecessor_flush_receipt_barrier",
    "waiting_for_corpus_convergence",
    "normalized_hash_idempotency",
    "late_completed_predecessor_correction",
]
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class ContractValidationError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def load_json_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_bytes())
    if not isinstance(value, dict):
        raise AssertionError(f"{path.name} must contain a JSON object")
    return value


def contract_identity(value: dict[str, Any], schema: str) -> dict[str, Any]:
    if schema != "ErrorEnvelope":
        return value
    error = value.get("error")
    if not isinstance(error, dict):
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "error response has no structured error object"
        )
    return error


def validate_pinned_contract_identity(value: dict[str, Any], schema: str) -> None:
    identity = contract_identity(value, schema)
    if identity.get("protocol_version") != EXPECTED_PROTOCOL_VERSION:
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "unsupported Lexinet protocol version"
        )
    schema_version = identity.get("schema_version")
    if type(schema_version) is not int or schema_version != EXPECTED_SCHEMA_VERSION:
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "unsupported Lexinet schema version"
        )

    if schema == "ErrorEnvelope":
        return
    required_semantics = identity.get("required_semantics")
    if not isinstance(required_semantics, list) or any(
        not isinstance(semantic, str) for semantic in required_semantics
    ):
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "required semantics are not a string array"
        )
    unknown = [
        semantic for semantic in required_semantics if semantic not in EXPECTED_REQUIRED_SEMANTICS
    ]
    if unknown:
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "response requires unsupported study semantics"
        )
    if required_semantics != EXPECTED_REQUIRED_SEMANTICS:
        raise ContractValidationError(
            "LEXINET.RESPONSE_SCHEMA_INVALID", "response omits or reorders required study semantics"
        )


class StudyV1ContractFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.provenance = load_json_object(PROVENANCE_PATH)

    def test_provenance_pins_exact_contract_identity(self) -> None:
        self.assertIs(type(self.provenance.get("export_schema_version")), int)
        self.assertEqual(self.provenance.get("export_schema_version"), 1)
        contract = self.provenance.get("contract")
        self.assertIsInstance(contract, dict)
        self.assertEqual(contract.get("id"), EXPECTED_PROTOCOL_VERSION)
        self.assertIs(type(contract.get("schema_version")), int)
        self.assertEqual(contract.get("schema_version"), EXPECTED_SCHEMA_VERSION)
        self.assertEqual(contract.get("required_semantics"), EXPECTED_REQUIRED_SEMANTICS)

        source = self.provenance.get("source")
        self.assertIsInstance(source, dict)
        self.assertEqual(source.get("repository"), "lexinet")
        self.assertEqual(source.get("contract_root"), "contracts/study.v1")
        self.assertRegex(source.get("repository_base_revision", ""), r"^[0-9a-f]{40}$")
        for field in (
            "manifest_sha256",
            "openapi_sha256",
            "contract_bundle_sha256",
        ):
            self.assertRegex(source.get(field, ""), SHA256_PATTERN)

    def test_every_exported_fixture_matches_its_source_digest(self) -> None:
        fixtures = self.provenance.get("fixtures")
        self.assertIsInstance(fixtures, list)
        self.assertGreater(len(fixtures), 0)

        for fixture in fixtures:
            self.assertIsInstance(fixture, dict)
            relative = PurePosixPath(fixture.get("export_path", ""))
            self.assertFalse(relative.is_absolute())
            self.assertNotIn("..", relative.parts)
            contents = FIXTURE_ROOT.joinpath(*relative.parts).read_bytes()
            self.assertEqual(hashlib.sha256(contents).hexdigest(), fixture.get("sha256"))

    def test_valid_golden_resources_identify_study_v1(self) -> None:
        for fixture in self.provenance["fixtures"]:
            if fixture["expected"] != "valid":
                continue
            path = FIXTURE_ROOT / fixture["export_path"]
            validate_pinned_contract_identity(load_json_object(path), fixture["schema"])

    def test_unknown_required_semantic_is_rejected(self) -> None:
        invalid = next(
            fixture
            for fixture in self.provenance["fixtures"]
            if fixture["expected"] == "invalid"
        )
        value = load_json_object(FIXTURE_ROOT / invalid["export_path"])

        with self.assertRaises(ContractValidationError) as raised:
            validate_pinned_contract_identity(value, invalid["schema"])
        self.assertEqual(raised.exception.code, "LEXINET.RESPONSE_SCHEMA_INVALID")

    def test_optional_response_field_remains_compatible(self) -> None:
        valid = next(
            fixture
            for fixture in self.provenance["fixtures"]
            if fixture["schema"] == "SessionResource" and fixture["expected"] == "valid"
        )
        value = copy.deepcopy(load_json_object(FIXTURE_ROOT / valid["export_path"]))
        value["future_optional_observation"] = {"opaque": True}

        validate_pinned_contract_identity(value, valid["schema"])


if __name__ == "__main__":
    unittest.main()
