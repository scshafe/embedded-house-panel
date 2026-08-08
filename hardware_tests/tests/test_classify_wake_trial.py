#!/usr/bin/env python3
"""Tests for the canonical wake-trial classifier using canned Linux evidence."""

from __future__ import annotations

import copy
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT))

from hardware_tests.scripts.classify_wake_trial import (
    CLASSIFICATIONS,
    WakeTrialEvidenceError,
    _parse_interrupt_counter,
    classify_wake_trial,
    summarize_wake_trial_manifests,
    validate_wake_trial_manifest,
)


CLASSIFIER = PROJECT_ROOT / "hardware_tests/scripts/classify_wake_trial.py"
FIXTURES = Path(__file__).resolve().parent / "fixtures/wake_trials"
NEGATIVE_CONTROL_FIXTURES = Path(__file__).resolve().parent / "fixtures/negative_controls"
TIMESTAMP_FIXTURES = Path(__file__).resolve().parent / "fixtures/wake_trial_timestamps"
INTERRUPT_PREFIX_FIXTURE = Path(__file__).resolve().parent / "fixtures/interrupts_label_prefix.txt"
SCHEMA = PROJECT_ROOT / "hardware_tests/schemas/wake_trial.schema.json"


class WakeTrialClassifierTest(unittest.TestCase):
    """Verify deterministic five-way wake attribution and fail-closed CLI behavior."""

    def test_each_canned_sysfs_fixture_has_expected_classification(self) -> None:
        expected_by_fixture = {
            "entered_and_attributed": "entered_and_attributed",
            "aborted": "aborted",
            "wrong_source": "wrong_source",
            "timeout": "timeout",
            "unobserved": "unobserved",
        }

        actual_by_fixture = {
            fixture_name: self._classify_fixture(fixture_name)["classification"]
            for fixture_name in expected_by_fixture
        }

        self.assertEqual(actual_by_fixture, expected_by_fixture)

    def test_schema_and_classifier_expose_exact_same_five_classes(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))

        self.assertEqual(tuple(schema["properties"]["classification"]["enum"]), CLASSIFICATIONS)

    def test_success_fixture_recomputes_suspend_and_interrupt_deltas(self) -> None:
        manifest = self._classify_fixture("entered_and_attributed")

        self.assertEqual(
            {
                "suspend_success": manifest["suspend_stat_delta"]["success"]["delta"],
                "suspend_fail": manifest["suspend_stat_delta"]["fail"]["delta"],
                "interrupt": manifest["interrupt_delta"]["delta"],
            },
            {"suspend_success": 1, "suspend_fail": 0, "interrupt": 1},
        )

    def test_aborted_precedes_timeout_and_wrong_source_evidence(self) -> None:
        manifest = self._classify_fixture("aborted")

        self.assertEqual(manifest["classification"], "aborted")

    def test_timeout_precedes_wrong_source_evidence(self) -> None:
        manifest = self._classify_fixture("timeout")

        self.assertEqual(manifest["classification"], "timeout")

    def test_missing_or_ambiguous_raw_evidence_is_unobserved(self) -> None:
        manifest = self._classify_fixture("unobserved")

        self.assertEqual(manifest["classification"], "unobserved")

    def test_contradictory_correlated_evidence_is_unobserved(self) -> None:
        manifest = self._classify_fixture("contradictory_evidence")

        self.assertEqual(
            set(manifest["classification_reasons"]),
            {
                "LINUX_EVENT_WAKE_EDGE_MISMATCH",
                "NEGATIVE_CONTROL_CORRELATION_UNBOUND",
                "NEGATIVE_CONTROL_NOT_INDEPENDENT",
                "NEGATIVE_CONTROL_UNOBSERVED",
                "RESUME_SENSOR_LEVEL_MISMATCH",
                "STATE_ENTRY_OBSERVED_STATE_MISMATCH",
            },
        )

    def test_definitive_wrong_source_precedes_missing_raw_irq_evidence(self) -> None:
        manifest = self._classify_fixture("wrong_source_incomplete_raw")

        self.assertEqual(manifest["classification"], "wrong_source")

    def test_missing_raw_mem_sleep_selection_is_unobserved(self) -> None:
        manifest = self._classify_fixture("mem_sleep_missing")

        self.assertEqual(manifest["classification"], "unobserved")
        self.assertIn("MEM_SLEEP_BEFORE_EVIDENCE_MISSING", manifest["classification_reasons"])

    def test_changed_raw_mem_sleep_selection_is_unobserved(self) -> None:
        manifest = self._classify_fixture("mem_sleep_changed")

        self.assertEqual(manifest["classification"], "unobserved")
        self.assertIn("MEM_SLEEP_SELECTION_CHANGED", manifest["classification_reasons"])

    def test_freeze_success_marks_mem_sleep_not_applicable(self) -> None:
        manifest = self._classify_fixture("freeze_entered_and_attributed")

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "mem_sleep_status": manifest["mem_sleep_selection"]["status"],
                "selected_mem_sleep": manifest["selected_mem_sleep"],
            },
            {
                "classification": "entered_and_attributed",
                "mem_sleep_status": "not_applicable",
                "selected_mem_sleep": None,
            },
        )

    def test_runner_expectation_rejects_replaced_correlation_id(self) -> None:
        expected_trial = self._fixture_expectations("entered_and_attributed")
        expected_trial["correlation_id"] = "different-prepared-trial"
        with self.assertRaises(WakeTrialEvidenceError) as raised:
            classify_wake_trial(
                FIXTURES / "entered_and_attributed",
                expected_trial,
                self._fixture_runner_timestamps("entered_and_attributed"),
            )

        self.assertEqual(
            raised.exception.code, "WAKE_TRIAL.EXPECTATIONS_ARTIFACT_MISMATCH"
        )

    def test_external_observation_cannot_replace_runner_matrix_fields(self) -> None:
        fixture_name = "observation_expectation_mismatch"
        expected_trial = self._fixture_runner_expectations(fixture_name)

        with self.assertRaises(WakeTrialEvidenceError) as raised:
            classify_wake_trial(
                FIXTURES / fixture_name,
                expected_trial,
                self._fixture_runner_timestamps(fixture_name),
            )

        self.assertEqual(raised.exception.code, "WAKE_TRIAL.EXPECTATION_MISMATCH")

    def test_external_recorder_cannot_replace_expectations_artifact(self) -> None:
        fixture_name = "expectations_artifact_mismatch"
        expected_trial = self._fixture_runner_expectations(fixture_name)

        with self.assertRaises(WakeTrialEvidenceError) as raised:
            classify_wake_trial(
                FIXTURES / fixture_name,
                expected_trial,
                self._fixture_runner_timestamps(fixture_name),
            )

        self.assertEqual(
            raised.exception.code, "WAKE_TRIAL.EXPECTATIONS_ARTIFACT_MISMATCH"
        )

    def test_pre_trial_state_entry_evidence_is_unobserved(self) -> None:
        manifest = self._classify_fixture("temporal_pre_trial")

        self.assertIn("TEMPORAL_CORRELATION_INVALID", manifest["classification_reasons"])

    def test_future_resume_evidence_is_unobserved(self) -> None:
        manifest = self._classify_fixture("temporal_future_resume")

        self.assertIn("TEMPORAL_CORRELATION_INVALID", manifest["classification_reasons"])

    def test_future_after_capture_evidence_is_unobserved(self) -> None:
        with tempfile.TemporaryDirectory(prefix="wake-trial-future-after-") as directory:
            evidence_directory = Path(directory) / "trial"
            shutil.copytree(FIXTURES / "entered_and_attributed", evidence_directory)
            future_after_capture = (
                TIMESTAMP_FIXTURES / "future_after_capture_at_utc.txt"
            ).read_text(encoding="utf-8")
            (evidence_directory / "raw/after/captured_at_utc").write_text(
                future_after_capture, encoding="utf-8"
            )
            expected_runner_timestamps = self._runner_timestamps(evidence_directory)
            with mock.patch(
                "hardware_tests.scripts.classify_wake_trial._utc_now",
                return_value="2026-08-07T12:00:10Z",
            ):
                manifest = classify_wake_trial(
                    evidence_directory,
                    self._expectations(evidence_directory),
                    expected_runner_timestamps,
                )

        self.assertIn("TEMPORAL_CORRELATION_INVALID", manifest["classification_reasons"])

    def test_observation_negative_control_status_without_artifact_is_unobserved(self) -> None:
        manifest = self._classify_with_negative_control_fixture(None)

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "status": manifest["negative_control"]["status"],
                "reason_present": "NEGATIVE_CONTROL_ARTIFACT_MISSING"
                in manifest["classification_reasons"],
            },
            {"classification": "unobserved", "status": "unobserved", "reason_present": True},
        )

    def test_tampered_negative_control_artifact_is_unobserved(self) -> None:
        manifest = self._classify_with_negative_control_fixture("tampered.json")

        self.assertIn("NEGATIVE_CONTROL_ARTIFACT_AMBIGUOUS", manifest["classification_reasons"])

    def test_wrong_subject_id_does_not_bind_negative_control(self) -> None:
        manifest = self._classify_with_negative_control_fixture("wrong_subject_id.json")

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "correlation_bound": manifest["negative_control"]["correlation_bound"],
                "reason_present": "NEGATIVE_CONTROL_CORRELATION_UNBOUND"
                in manifest["classification_reasons"],
            },
            {
                "classification": "unobserved",
                "correlation_bound": False,
                "reason_present": True,
            },
        )

    def test_wrong_evidence_source_does_not_bind_negative_control(self) -> None:
        manifest = self._classify_with_negative_control_fixture(
            "wrong_evidence_source.json"
        )

        self.assertIn("NEGATIVE_CONTROL_CORRELATION_UNBOUND", manifest["classification_reasons"])

    def test_overlapping_negative_control_is_unobserved(self) -> None:
        manifest = self._classify_with_negative_control_fixture("overlaps_subject.json")

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "temporally_isolated": manifest["negative_control"]["temporally_isolated"],
                "reason_present": "NEGATIVE_CONTROL_TEMPORAL_BOUNDS_INVALID"
                in manifest["classification_reasons"],
            },
            {
                "classification": "unobserved",
                "temporally_isolated": False,
                "reason_present": True,
            },
        )

    def test_failed_negative_control_is_wrong_source(self) -> None:
        manifest = self._classify_with_negative_control_fixture(
            "failed_unexpected_wake.json"
        )

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "status": manifest["negative_control"]["status"],
            },
            {"classification": "wrong_source", "status": "failed"},
        )

    def test_post_subject_negative_control_is_independently_derived(self) -> None:
        manifest = self._classify_with_negative_control_fixture(
            "valid_after_subject.json"
        )

        self.assertEqual(
            {
                "classification": manifest["classification"],
                "status": manifest["negative_control"]["status"],
                "temporal_relationship": manifest["negative_control"][
                    "temporal_relationship"
                ],
                "independently_verified": manifest["negative_control"][
                    "independently_verified"
                ],
            },
            {
                "classification": "entered_and_attributed",
                "status": "passed",
                "temporal_relationship": "after_subject",
                "independently_verified": True,
            },
        )

    def test_mixed_summary_reports_exact_five_counts_and_exits_nonzero(self) -> None:
        completed = self._run_summary_for_fixture_names(
            (
                "entered_and_attributed",
                "aborted",
                "wrong_source",
                "timeout",
                "unobserved",
            )
        )
        summary = json.loads(completed.stdout)

        self.assertEqual(
            {
                "returncode": completed.returncode,
                **{
                    classification: summary[f"{classification}_count"]
                    for classification in CLASSIFICATIONS
                },
            },
            {"returncode": 1, **{classification: 1 for classification in CLASSIFICATIONS}},
        )

    def test_all_success_summary_exits_zero(self) -> None:
        completed = self._run_summary_for_fixture_names(
            ("entered_and_attributed", "freeze_entered_and_attributed")
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_classification_only_forged_success_fails_summary(self) -> None:
        forged_manifest = {
            "schema_version": 1,
            "correlation_id": "forged-classification-only",
            "classification": "entered_and_attributed",
        }
        summary, all_succeeded = self._summarize_manifests((forged_manifest,))

        self.assertEqual(
            {
                "all_succeeded": all_succeeded,
                "valid_manifest_count": summary["valid_manifest_count"],
                "manifest_error_count": summary["manifest_error_count"],
                "success_count": summary["entered_and_attributed_count"],
            },
            {
                "all_succeeded": False,
                "valid_manifest_count": 0,
                "manifest_error_count": 1,
                "success_count": 0,
            },
        )

    def test_structural_schema_rejects_null_required_success_evidence(self) -> None:
        manifest = self._classify_fixture("entered_and_attributed")
        mutations = {
            "schema version boolean": (None, "schema_version", True),
            "external timestamp null": ("external_stimulus", "observed_at_utc", None),
            "state evidence null": ("state_entry", "evidence_source", None),
            "wake observed source null": ("wake_reason", "observed_source", None),
            "wake evidence null": ("wake_reason", "evidence_source", None),
            "negative subject null": (
                "negative_control",
                "subject_correlation_id",
                None,
            ),
            "negative evidence null": ("negative_control", "evidence_source", None),
            "negative timestamp null": (
                "negative_control",
                "control_completed_at_utc",
                None,
            ),
            "linux event value null": ("linux_input_event", "event_value", None),
            "linux event code boolean": ("linux_input_event", "event_code", True),
            "linux event timestamp null": (
                "linux_input_event",
                "observed_at_utc",
                None,
            ),
            "runner timestamp null": (
                "runner_timestamps",
                "after_capture_at_utc",
                None,
            ),
        }

        self.assertEqual(
            validate_wake_trial_manifest(manifest, validation_scope="structural"), []
        )
        for mutation_name, (object_name, field_name, replacement) in mutations.items():
            with self.subTest(mutation=mutation_name):
                mutated = copy.deepcopy(manifest)
                target = mutated if object_name is None else mutated[object_name]
                target[field_name] = replacement
                self.assertNotEqual(
                    validate_wake_trial_manifest(
                        mutated, validation_scope="structural"
                    ),
                    [],
                )

    def test_acceptance_recomputes_cross_field_success_invariants(self) -> None:
        manifest = self._classify_fixture("entered_and_attributed")
        mutations = {
            "state binding": ("state_entry", "observed_state", "freeze"),
            "wake binding": ("wake_reason", "observed_source", "unrelated_source"),
            "negative subject": (
                "negative_control",
                "subject_correlation_id",
                "different-subject",
            ),
            "negative companion": (
                "negative_control",
                "companion_correlation_id",
                manifest["correlation_id"],
            ),
            "edge binding": (None, "starting_level", "asserted"),
            "resume binding": (
                "resume_observation",
                "sensor_level",
                "deasserted",
            ),
            "temporal order": (
                "state_entry",
                "observed_at_utc",
                "2026-08-07T11:59:59Z",
            ),
            "negative temporal order": (
                "negative_control",
                "control_completed_at_utc",
                "2026-08-07T12:00:02Z",
            ),
            "suspend counter": (
                "suspend_stat_delta.success",
                "after",
                43,
            ),
            "interrupt label": (
                "interrupt_delta",
                "expected_interrupt_label",
                "other-irq",
            ),
            "mem sleep binding": ("mem_sleep_selection", "after", "s2idle"),
        }

        for mutation_name, (object_path, field_name, replacement) in mutations.items():
            with self.subTest(mutation=mutation_name):
                mutated = copy.deepcopy(manifest)
                target = mutated
                if object_path is not None:
                    for path_component in object_path.split("."):
                        target = target[path_component]
                target[field_name] = replacement
                self.assertEqual(
                    validate_wake_trial_manifest(
                        mutated, validation_scope="structural"
                    ),
                    [],
                )
                self.assertNotEqual(validate_wake_trial_manifest(mutated), [])
                summary, all_succeeded = self._summarize_manifests((mutated,))
                self.assertFalse(all_succeeded)
                self.assertEqual(summary["manifest_error_count"], 1)

    def test_unknown_schema_keyword_fails_closed(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        schema["unevaluatedProperties"] = False
        manifest = self._classify_fixture("entered_and_attributed")
        with tempfile.TemporaryDirectory(prefix="wake-schema-unsupported-") as directory:
            schema_path = Path(directory) / "wake_trial.schema.json"
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            with mock.patch(
                "hardware_tests.scripts.classify_wake_trial.WAKE_TRIAL_SCHEMA_PATH",
                schema_path,
            ):
                with self.assertRaises(WakeTrialEvidenceError) as raised:
                    validate_wake_trial_manifest(manifest)

        self.assertEqual(raised.exception.code, "WAKE_TRIAL.SCHEMA_UNSUPPORTED")

    def test_fresh_manifest_schema_drift_is_structured_internal_failure(self) -> None:
        with mock.patch(
            "hardware_tests.scripts.classify_wake_trial.validate_wake_trial_manifest",
            return_value=["$.classification:FORCED_DRIFT"],
        ):
            with self.assertRaises(WakeTrialEvidenceError) as raised:
                self._classify_fixture("entered_and_attributed")

        self.assertEqual(raised.exception.code, "WAKE_TRIAL.MANIFEST_INTERNAL_INVALID")

    def test_interrupt_label_prefix_does_not_match_descriptor_token(self) -> None:
        interrupt_count, error = _parse_interrupt_counter(
            INTERRUPT_PREFIX_FIXTURE, "pir"
        )

        self.assertEqual(
            {"interrupt_count": interrupt_count, "error": error},
            {"interrupt_count": None, "error": "EVIDENCE_MISSING"},
        )

    def test_require_success_rejects_aborted_fixture(self) -> None:
        completed = self._run_require_success("aborted")

        self.assertNotEqual(completed.returncode, 0)

    def test_require_success_rejects_wrong_source_fixture(self) -> None:
        completed = self._run_require_success("wrong_source")

        self.assertNotEqual(completed.returncode, 0)

    def test_require_success_accepts_entered_and_attributed_fixture(self) -> None:
        completed = self._run_require_success("entered_and_attributed")

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def _run_summary_for_fixture_names(
        self, fixture_names: tuple[str, ...]
    ) -> subprocess.CompletedProcess[str]:
        manifests = tuple(self._classify_fixture(name) for name in fixture_names)
        with tempfile.TemporaryDirectory(prefix="wake-summary-cli-") as directory:
            trials_directory = Path(directory)
            self._write_summary_manifests(trials_directory, manifests)
            return subprocess.run(
                [
                    "python3",
                    str(CLASSIFIER),
                    "summarize",
                    "--trials-directory",
                    str(trials_directory),
                    "--expected-count",
                    str(len(manifests)),
                    "--classification-error-count",
                    "0",
                ],
                check=False,
                capture_output=True,
                text=True,
            )

    def _summarize_manifests(
        self, manifests: tuple[dict[str, object], ...]
    ) -> tuple[dict[str, object], bool]:
        with tempfile.TemporaryDirectory(prefix="wake-summary-direct-") as directory:
            trials_directory = Path(directory)
            self._write_summary_manifests(trials_directory, manifests)
            return summarize_wake_trial_manifests(
                trials_directory,
                expected_count=len(manifests),
                classification_error_count=0,
            )

    def _write_summary_manifests(
        self,
        trials_directory: Path,
        manifests: tuple[dict[str, object], ...],
    ) -> None:
        for index, manifest in enumerate(manifests, start=1):
            trial_directory = trials_directory / f"trial-{index:04d}"
            trial_directory.mkdir(parents=True)
            (trial_directory / "wake_trial_manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )

    def _run_require_success(self, fixture_name: str) -> subprocess.CompletedProcess[str]:
        expectations = self._fixture_expectations(fixture_name)
        runner_timestamps = self._fixture_runner_timestamps(fixture_name)
        with tempfile.TemporaryDirectory(prefix="wake-trial-test-") as temporary_directory:
            command = [
                "python3",
                str(CLASSIFIER),
                "classify",
                "--evidence-directory",
                str(FIXTURES / fixture_name),
                "--output",
                str(Path(temporary_directory) / "manifest.json"),
                "--expected-correlation-id",
                expectations["correlation_id"],
                "--expected-implementation",
                expectations["implementation"],
                "--expected-requested-state",
                expectations["requested_state"],
                "--expected-starting-level",
                expectations["starting_level"],
                "--expected-wake-edge",
                expectations["wake_edge"],
                "--expected-wake-source",
                expectations["expected_wake_source"],
                "--expected-interrupt-label",
                expectations["expected_interrupt_label"],
                "--expected-trial-started-at-utc",
                expectations["trial_started_at_utc"],
                "--expected-before-capture-at-utc",
                runner_timestamps["before_capture_at_utc"],
                "--require-success",
            ]
            if expectations["selected_mem_sleep"] is not None:
                command.extend(
                    ["--expected-selected-mem-sleep", expectations["selected_mem_sleep"]]
                )
            if runner_timestamps["process_returned_at_utc"] is not None:
                command.extend(
                    [
                        "--expected-process-returned-at-utc",
                        runner_timestamps["process_returned_at_utc"],
                        "--expected-after-capture-at-utc",
                        runner_timestamps["after_capture_at_utc"],
                    ]
                )
            return subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
            )

    def _fixture_expectations(self, fixture_name: str) -> dict[str, object]:
        return self._expectations(FIXTURES / fixture_name)

    def _fixture_runner_expectations(self, fixture_name: str) -> dict[str, object]:
        return json.loads(
            (FIXTURES / fixture_name / "runner_expectations.json").read_text(
                encoding="utf-8"
            )
        )

    def _classify_fixture(self, fixture_name: str) -> dict[str, object]:
        return classify_wake_trial(
            FIXTURES / fixture_name,
            self._fixture_expectations(fixture_name),
            self._fixture_runner_timestamps(fixture_name),
        )

    def _fixture_runner_timestamps(self, fixture_name: str) -> dict[str, str | None]:
        return self._runner_timestamps(FIXTURES / fixture_name)

    def _classify_with_negative_control_fixture(
        self, negative_control_fixture_name: str | None
    ) -> dict[str, object]:
        with tempfile.TemporaryDirectory(prefix="wake-trial-negative-control-") as directory:
            evidence_directory = Path(directory) / "trial"
            shutil.copytree(FIXTURES / "entered_and_attributed", evidence_directory)
            negative_control_path = evidence_directory / "negative_control.json"
            if negative_control_fixture_name is None:
                negative_control_path.unlink()
            else:
                shutil.copyfile(
                    NEGATIVE_CONTROL_FIXTURES / negative_control_fixture_name,
                    negative_control_path,
                )
            with mock.patch(
                "hardware_tests.scripts.classify_wake_trial._utc_now",
                return_value="2026-08-07T12:00:10Z",
            ):
                return classify_wake_trial(
                    evidence_directory,
                    self._expectations(evidence_directory),
                    self._runner_timestamps(evidence_directory),
                )

    def _expectations(self, evidence_directory: Path) -> dict[str, object]:
        return json.loads(
            (evidence_directory / "expectations.json").read_text(encoding="utf-8")
        )

    def _runner_timestamps(
        self, evidence_directory: Path
    ) -> dict[str, str | None]:
        raw_directory = evidence_directory / "raw"

        def read_optional(relative_path: str) -> str | None:
            timestamp_path = raw_directory / relative_path
            if not timestamp_path.exists():
                return None
            return timestamp_path.read_text(encoding="utf-8").strip()

        return {
            "before_capture_at_utc": read_optional("before/captured_at_utc"),
            "process_returned_at_utc": read_optional("process_returned_at_utc"),
            "after_capture_at_utc": read_optional("after/captured_at_utc"),
        }


if __name__ == "__main__":
    unittest.main()
