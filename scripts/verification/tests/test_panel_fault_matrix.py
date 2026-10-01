"""Fail-closed cases for the roadmap Step 8 fault matrix checker."""

import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/verification"))
import check_fault_matrix as checker  # noqa: E402


class FaultMatrixTest(unittest.TestCase):
    def setUp(self) -> None:
        self.matrix = json.loads((ROOT / "docs/testing/fault-matrix.json").read_text())
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-fault-matrix-")
        self.addCleanup(self.temporary.cleanup)
        self.original = checker.MATRIX
        self.addCleanup(setattr, checker, "MATRIX", self.original)

    def run_checker(self, matrix: dict) -> int:
        path = Path(self.temporary.name) / "fault-matrix.json"
        path.write_text(json.dumps(matrix))
        checker.MATRIX = path
        with contextlib.redirect_stdout(io.StringIO()):
            return checker.main()

    def assert_fails(self, matrix: dict, code: str) -> None:
        with self.assertRaises(checker.MatrixError) as raised:
            self.run_checker(matrix)
        self.assertIn(f"FAULT_MATRIX.{code}", str(raised.exception))

    def row(self, matrix: dict, row_id: str) -> dict:
        return next(row for row in matrix["rows"] if row["id"] == row_id)

    def test_repository_matrix_passes(self) -> None:
        self.assertEqual(self.run_checker(self.matrix), 0)

    def test_checkout_beneath_build_directory_keeps_source_runners(self) -> None:
        root = Path(self.temporary.name) / ".build" / "clone"
        source = root / "panel_supervisor" / "CMakeLists.txt"
        generated = root / ".build" / "generated" / "CMakeLists.txt"
        source.parent.mkdir(parents=True)
        generated.parent.mkdir(parents=True)
        source.write_text("add_executable(test tests/panel_supervisor_test.cpp)")
        generated.write_text("generated runner must not count")
        with patch.object(checker, "ROOT", root):
            runners = checker.runner_sources()
        self.assertTrue(checker.registered("panel_supervisor/tests/panel_supervisor_test.cpp", runners))
        self.assertNotIn("generated runner", runners)

    def test_rows_must_match_the_roadmap_list(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        matrix["rows"].pop()
        self.assert_fails(matrix, "ROWS_DIVERGED")

    def test_cited_function_must_exist(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        self.row(matrix, "disk_full")["host_evidence"][0]["function"] = "no_such_test"
        self.assert_fails(matrix, "FUNCTION_MISSING")

    def test_needle_must_appear_inside_the_cited_function(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        # Present in the file, but not inside this function's body.
        self.row(matrix, "disk_full")["host_evidence"][0]["needle"] = "test_tls"
        self.assert_fails(matrix, "NEEDLE_MISSING")

    def test_partial_rows_name_their_residual(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        del self.row(matrix, "suspend_preparation")["residual"]
        self.assert_fails(matrix, "RESIDUAL_MISSING")

    def test_live_scenarios_must_have_passed_in_the_recorded_run(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        self.row(matrix, "offline_queues")["real_lexinet_evidence"].append("never_ran")
        self.assert_fails(matrix, "LIVE_SCENARIO_MISSING")

    def test_cited_files_must_be_wired_into_a_runner(self) -> None:
        matrix = copy.deepcopy(self.matrix)
        self.row(matrix, "cold_boot")["host_evidence"].append(
            {"file": "panel_supervisor/tests/fixtures/supervisor_test_fixture.hpp", "function": None,
             "needle": "#pragma once"})
        self.assert_fails(matrix, "TEST_NOT_REGISTERED")


if __name__ == "__main__":
    unittest.main()
