#!/usr/bin/env python3
"""Focused positive and fail-closed tests for the plan verification map."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
CHECKER = SCRIPT_DIRECTORY / "check_plan_verification_map.py"
FIXTURE_ROOT = SCRIPT_DIRECTORY / "fixtures/plan_verification_map"


def invoke(
    map_path: Path, plans_dir: Path | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(CHECKER),
            "--project-root",
            str(FIXTURE_ROOT),
            "--plans-dir",
            str(plans_dir or FIXTURE_ROOT / "plans"),
            "--map",
            str(map_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def expect_failure(document: dict[str, object], code: str, temporary: Path) -> None:
    path = temporary / f"{code}.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    result = invoke(path)
    if result.returncode == 0 or f"PLAN_VERIFICATION_MAP.{code}" not in result.stderr:
        raise AssertionError(
            f"expected {code}, got exit={result.returncode} stderr={result.stderr!r}"
        )


def main() -> int:
    valid_path = FIXTURE_ROOT / "valid-map.json"
    valid_result = invoke(valid_path)
    if valid_result.returncode != 0:
        raise AssertionError(valid_result.stderr)

    malformed_result = invoke(
        valid_path, FIXTURE_ROOT / "invalid_plans"
    )
    if (
        malformed_result.returncode == 0
        or "PLAN_VERIFICATION_MAP.VERIFY_SYNTAX_INVALID"
        not in malformed_result.stderr
    ):
        raise AssertionError(
            "malformed Verify bullet was not rejected: "
            f"exit={malformed_result.returncode} stderr={malformed_result.stderr!r}"
        )

    source = json.loads(valid_path.read_text(encoding="utf-8"))
    unchecked_step_statuses = {
        entry["proof_status"]
        for entry in source["entries"]
        if entry["step"] == "Step 2"
    }
    if unchecked_step_statuses != {"planned", "recorded"}:
        raise AssertionError(
            "valid fixture must prove mixed recorded/planned evidence "
            "on an unchecked step"
        )
    with tempfile.TemporaryDirectory(prefix="bhp-plan-map-") as temporary_name:
        temporary = Path(temporary_name)

        unmapped = json.loads(json.dumps(source))
        unmapped["entries"].pop()
        expect_failure(unmapped, "ACTIVE_ITEM_UNMAPPED", temporary)

        duplicate = json.loads(json.dumps(source))
        duplicate["entries"][2]["test_symbol"] = duplicate["entries"][1][
            "test_symbol"
        ]
        expect_failure(duplicate, "TEST_SYMBOL_DUPLICATE", temporary)

        empty_command = json.loads(json.dumps(source))
        empty_command["entries"][0]["command"] = ""
        expect_failure(empty_command, "COMMAND_EMPTY", temporary)

        stale_text = json.loads(json.dumps(source))
        stale_text["entries"][0]["clause_item"] = "changed plan text"
        expect_failure(stale_text, "CLAUSE_TEXT_MISMATCH", temporary)

        checked_step_with_planned_proof = json.loads(json.dumps(source))
        checked_step_with_planned_proof["entries"][0]["proof_status"] = "planned"
        expect_failure(
            checked_step_with_planned_proof, "PROOF_STATUS_MISMATCH", temporary
        )

        recorded_planned = json.loads(json.dumps(source))
        recorded_planned["entries"][0]["command"] = "PLANNED: test -n fixture"
        expect_failure(recorded_planned, "RECORDED_COMMAND_PLANNED", temporary)

        unresolved_executable = json.loads(json.dumps(source))
        unresolved_executable["entries"][0]["command"] = (
            "test -n fixture && command-that-does-not-exist --check"
        )
        expect_failure(
            unresolved_executable, "RECORDED_EXECUTABLE_UNRESOLVED", temporary
        )

    project_root = SCRIPT_DIRECTORY.parents[1]
    discovered_result = subprocess.run(
        [
            sys.executable,
            str(CHECKER),
            "--project-root",
            str(project_root),
            "--plans-dir",
            str(project_root / ".claude/plans"),
            "--dump-discovered",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if discovered_result.returncode != 0:
        raise AssertionError(discovered_result.stderr)
    discovered = json.loads(discovered_result.stdout)
    lexinet_plan = ".claude/plans/2026-08-05-lexinet-study-integration.md"
    granular_counts = {
        step: sum(
            1
            for item in discovered
            if item["plan"] == lexinet_plan and item["step"] == step
        )
        for step in ("Step 4", "Step 6")
    }
    if granular_counts != {"Step 4": 22, "Step 6": 41}:
        raise AssertionError(f"Lexinet granular decomposition drift: {granular_counts}")

    step_4_terminal_items = [
        item["clause_item"]
        for item in discovered
        if item["plan"] == lexinet_plan
        and item["step"] == "Step 4"
        and item["item_index"] >= 20
    ]
    if step_4_terminal_items != [
        "run CLI round trips against a disposable server and PostgreSQL instance.",
        "Also prove topic deletion cannot become empty success, exact-limit versus "
        "over-limit raw bodies, duplicate/ambiguous JSON rejection, normalized JCS "
        "equivalence and semantic mutation, all stable error mappings/redaction, "
        "auth/authorizer short-circuiting, admission/cancellation, committed-readback "
        "mismatch suppressing shadow, sync-page byte stability across merge/delete, "
        "response/page budgets without partial writes, endpoint/redirect/TLS policy, "
        "and CLI request/response overflow handling.",
        "Run focused service/server/CLI and live-PostgreSQL suites under `go test -race`.",
    ]:
        raise AssertionError(
            f"Lexinet Step 4 terminal verification drift: {step_4_terminal_items}"
        )

    print(
        "PLAN_VERIFICATION_MAP_TESTS.COMPLETE positive=3 negative=8 "
        "lexinet_step_4_items=22 lexinet_step_6_items=41"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
