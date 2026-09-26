#!/usr/bin/env python3
"""Fail closed when the roadmap Step 8 fault matrix drifts from the tests it cites.

Every row names concrete evidence: a test file, the function that proves the row and a
needle that must appear inside that function's body, plus scenarios from the recorded
real-Lexinet contract run. Each cited file must also be wired into a test runner. The
matrix is simulation evidence only; the Mac-local VM run repeats each row.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--root"
            else Path(__file__).resolve().parents[2]).resolve()
MATRIX = ROOT / "docs/testing/fault-matrix.json"
LIVE_REPORT = ROOT / "docs/testing/lexinet-live-contract-validation.json"
ROWS = ("cold_boot", "untrusted_clock", "sunrise_sunset_rollover", "absence_during_work",
        "suspend_preparation", "crash_restart", "offline_queues", "stale_data",
        "corrupted_state", "disk_full", "tls_failure", "credential_capability_denial")
CPP_DEFINITION = re.compile(r"^(?:static\s+)?(?:bool|void|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
PY_DEFINITION = re.compile(r"^(\s*)def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")


class MatrixError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f"FAULT_MATRIX.{code}: {detail}")


def fail(code: str, detail: str) -> None:
    raise MatrixError(code, detail)


def function_body(path: Path, name: str) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    python = path.suffix == ".py"
    for index, line in enumerate(lines):
        match = PY_DEFINITION.match(line) if python else CPP_DEFINITION.match(line)
        if not match or match.group(2 if python else 1) != name:
            continue
        indent = len(match.group(1)) if python else 0
        body = [line]
        for following in lines[index + 1:]:
            nested = PY_DEFINITION.match(following) if python else CPP_DEFINITION.match(following)
            if nested and (not python or len(nested.group(1)) <= indent):
                break
            body.append(following)
        return "\n".join(body)
    fail("FUNCTION_MISSING", f"{path.relative_to(ROOT)}::{name}")
    return ""


def runner_sources() -> str:
    sources = [ROOT / "Makefile", *ROOT.glob("scripts/verification/*.sh"),
               *ROOT.glob("**/CMakeLists.txt")]
    return "\n".join(path.read_text(encoding="utf-8") for path in sources
                     if ".build" not in path.parts and path.is_file())


GOLDEN_MANIFEST = ROOT / "panel_ui/tests/golden/manifest.json"


def registered(relative: str, runners: str) -> bool:
    path = Path(relative)
    if path.parent.as_posix() == "simulator/scenarios":
        # Scenarios are exact-pixel goldens checked by make verify-panel-goldens.
        manifest = json.loads(GOLDEN_MANIFEST.read_text(encoding="utf-8"))
        return path.stem in manifest["scenarios"] and "verify-panel-goldens" in runners
    if path.suffix == ".py":
        return f"discover -s {path.parent.as_posix()} " in runners
    if path.suffix == ".cpp":
        # Either a direct target source, or a test-loop entry that expands to <entry>_test.cpp.
        loop_entry = re.escape(path.stem.removesuffix("_test"))
        return (f"tests/{path.name}" in runners or re.search(
            rf"^\s*{loop_entry}\s*(?:\\|\))?\s*$", runners, re.M) is not None)
    return path.name in runners


def main() -> int:
    matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
    if (matrix.get("schema_version") != 1 or matrix.get("canonical") != "roadmap-step-8-fault-matrix"
            or matrix.get("evidence") != "simulation_not_hardware_evidence"):
        fail("SCHEMA_INVALID", str(MATRIX.relative_to(ROOT)))
    rows = matrix["rows"]
    if tuple(row["id"] for row in rows) != ROWS:
        fail("ROWS_DIVERGED", "rows must be exactly the roadmap Step 8 fault list, in order")
    live = json.loads(LIVE_REPORT.read_text(encoding="utf-8"))
    passed_scenarios = {scenario["scenario"] for scenario in live["scenarios"]
                        if scenario.get("passed") is True}
    runners = runner_sources()
    summary = {}
    for row in rows:
        if row["status"] not in ("covered", "partial"):
            fail("ROW_UNCOVERED", row["id"])
        if row["status"] == "partial" and not row.get("residual"):
            fail("RESIDUAL_MISSING", row["id"])
        if not row["host_evidence"] or not row.get("vm_repeat"):
            fail("EVIDENCE_MISSING", row["id"])
        for evidence in row["host_evidence"]:
            path = ROOT / evidence["file"]
            if not path.is_file():
                fail("FILE_MISSING", evidence["file"])
            text = (function_body(path, evidence["function"]) if evidence["function"]
                    else path.read_text(encoding="utf-8"))
            if evidence["needle"] not in text:
                fail("NEEDLE_MISSING", f"{evidence['file']}::{evidence['function']}: "
                                       f"{evidence['needle']}")
            if not registered(evidence["file"], runners):
                fail("TEST_NOT_REGISTERED", evidence["file"])
        for scenario in row["real_lexinet_evidence"]:
            if scenario not in passed_scenarios:
                fail("LIVE_SCENARIO_MISSING", f"{row['id']}: {scenario}")
        summary[row["id"]] = {"status": row["status"], "host": len(row["host_evidence"]),
                              "real_lexinet": len(row["real_lexinet_evidence"])}
    print(json.dumps({"fault_matrix": "verified", "rows": summary,
                      "evidence": matrix["evidence"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except MatrixError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
