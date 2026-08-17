#!/usr/bin/env python3
"""Fail closed when an active plan Verify item lacks a verification mapping."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ERROR_PREFIX = "PLAN_VERIFICATION_MAP"
LEXINET_PLAN = ".claude/plans/2026-08-05-lexinet-study-integration.md"
PROOF_KINDS = {"automated", "delegated", "hardware", "human", "integration"}
PROOF_STATUSES = {"planned", "recorded"}
COMMAND_SEPARATORS = {"&&", "||", ";", "|"}
ENV_ASSIGNMENT_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=.*$")
STEP_PATTERN = re.compile(r"^\s*- \[([ xX])\] \*\*(Step [^:]+):")
VERIFY_PATTERN = re.compile(r"^(\s*)- Verify(?: \([^)]*\))?:\s*(.*)$")
VERIFY_WITHOUT_COLON_PATTERN = re.compile(r"^(\s*)- Verify[ \t]+([^:(].*)$")
VERIFY_LOOKING_PATTERN = re.compile(r"^\s*- Verify(?:\b|[(:])")
STRIKE_PATTERN = re.compile(r"~~.*?~~", re.DOTALL)


class MapError(Exception):
    """A stable, user-actionable verification-map failure."""

    def __init__(self, code: str, detail: str) -> None:
        super().__init__(detail)
        self.code = code
        self.detail = detail


@dataclass(frozen=True)
class ClauseItem:
    plan: str
    step: str
    verify_index: int
    item_index: int
    clause_item: str
    step_complete: bool

    @property
    def key(self) -> tuple[str, str, int, int]:
        return (self.plan, self.step, self.verify_index, self.item_index)


def normalize(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def remove_struck_text(text: str, source: Path) -> str:
    if text.count("~~") % 2:
        raise MapError(
            "STRIKETHROUGH_UNBALANCED",
            f"{source}: unmatched strikethrough delimiter",
        )
    return STRIKE_PATTERN.sub("", text)


def split_outside_code(value: str, separator: str) -> list[str]:
    items: list[str] = []
    start = 0
    in_code = False
    for index, character in enumerate(value):
        if character == "`":
            in_code = not in_code
        elif character == separator and not in_code:
            item = normalize(value[start:index])
            if item:
                items.append(item)
            start = index + 1
    final = normalize(value[start:])
    if final:
        items.append(final)
    return items


def split_named_list(value: str, marker: str) -> list[str]:
    prefix, found, suffix = value.partition(marker)
    if not found:
        return [value]
    items: list[str] = []
    if normalize(prefix):
        items.append(normalize(prefix))
    for item in split_outside_code(suffix, ","):
        items.append(re.sub(r"^(?:and|then)\s+", "", item))
    return items


def decompose_verify(plan: str, step: str, body: str) -> list[str]:
    """Split semantic items, with explicit granular rules for Lexinet 4 and 6."""
    default_items = split_outside_code(body, ";")
    if (
        plan == ".claude/plans/2026-08-05-end-to-end-development-roadmap.md"
        and step == "Step 12"
        and body.startswith("no stale or bright frame appears")
    ):
        return [normalize(item) for item in body.split(". ", 1)]
    if (
        plan == ".claude/plans/2026-08-05-personal-agenda-feasibility.md"
        and step == "Step 5"
        and body.startswith("that a stop-phrase tail")
    ):
        first, second = body.split(" and that maximum duration", 1)
        return [normalize(first.removeprefix("that ")), normalize("maximum duration" + second)]
    if plan != LEXINET_PLAN:
        return default_items

    if step == "Step 4":
        granular: list[str] = []
        for item in default_items:
            if item.startswith("reject local-only exhaustion,"):
                granular.extend(
                    re.sub(r"^(?:and|then)\s+", "", part)
                    for part in split_outside_code(item, ",")
                )
            elif " and that changed receipt replay conflicts" in item:
                left, right = item.split(" and that ", 1)
                granular.extend((normalize(left), normalize(right)))
            elif ". Also prove " in item:
                primary, _, remaining = item.partition(". Also prove ")
                additional, race_marker, race = remaining.partition(
                    ". Run focused "
                )
                granular.append(normalize(primary + "."))
                granular.append(normalize("Also prove " + additional + "."))
                if race_marker:
                    granular.append(normalize("Run focused " + race))
            else:
                granular.append(item)
        return granular

    if step == "Step 6":
        granular = []
        for item in default_items:
            if "Named tests prove " in item:
                before_source, source_marker, after_source = item.partition(
                    ". A tracked-source assertion"
                )
                granular.extend(split_named_list(before_source, "Named tests prove "))
                if source_marker:
                    source_item, final_marker, final_item = after_source.partition(
                        ". The current `host-debug` preset"
                    )
                    granular.append(normalize("A tracked-source assertion" + source_item))
                    if final_marker:
                        granular.append(
                            normalize("The current `host-debug` preset" + final_item)
                        )
            elif item.startswith("tests cover "):
                granular.extend(split_named_list(item, "tests cover "))
            else:
                granular.append(item)
        return granular

    return default_items


def verify_blocks(text: str, source: Path) -> Iterable[tuple[str, bool, int, str]]:
    lines = remove_struck_text(text, source).splitlines()
    current_step: str | None = None
    current_step_complete = False
    verify_counts: dict[str, int] = {}
    index = 0
    while index < len(lines):
        line = lines[index]
        step_match = STEP_PATTERN.match(line)
        if step_match:
            current_step_complete = step_match.group(1).lower() == "x"
            current_step = normalize(step_match.group(2))

        verify_match = VERIFY_PATTERN.match(line)
        if verify_match is None:
            verify_match = VERIFY_WITHOUT_COLON_PATTERN.match(line)
        if not verify_match:
            if VERIFY_LOOKING_PATTERN.match(line):
                raise MapError(
                    "VERIFY_SYNTAX_INVALID",
                    f"{source}:{index + 1}: unsupported Verify bullet syntax",
                )
            index += 1
            continue
        if current_step is None:
            raise MapError(
                "VERIFY_WITHOUT_STEP", f"{source}:{index + 1}: Verify has no Step"
            )

        indent = len(verify_match.group(1))
        body_lines = [verify_match.group(2)]
        cursor = index + 1
        while cursor < len(lines):
            candidate = lines[cursor]
            candidate_indent = len(candidate) - len(candidate.lstrip())
            if candidate.strip() and candidate_indent <= indent:
                if re.match(r"^\s*- ", candidate) or candidate.startswith("#"):
                    break
            body_lines.append(candidate.strip())
            cursor += 1

        body = normalize(" ".join(body_lines))
        if not body:
            raise MapError(
                "VERIFY_EMPTY", f"{source}:{index + 1}: active Verify has no text"
            )
        verify_counts[current_step] = verify_counts.get(current_step, 0) + 1
        yield current_step, current_step_complete, verify_counts[current_step], body
        index = cursor


def discover(plans_dir: Path, project_root: Path) -> list[ClauseItem]:
    discovered: list[ClauseItem] = []
    for plan_path in sorted(plans_dir.rglob("*.md")):
        try:
            relative_plan = plan_path.relative_to(project_root).as_posix()
        except ValueError as error:
            raise MapError(
                "PLAN_OUTSIDE_PROJECT", f"{plan_path} is outside {project_root}"
            ) from error
        for step, step_complete, verify_index, body in verify_blocks(
            plan_path.read_text(encoding="utf-8"), plan_path
        ):
            items = decompose_verify(relative_plan, step, body)
            if not items:
                raise MapError(
                    "VERIFY_DECOMPOSITION_EMPTY",
                    f"{relative_plan} {step} Verify {verify_index}",
                )
            for item_index, clause_item in enumerate(items, start=1):
                discovered.append(
                    ClauseItem(
                        plan=relative_plan,
                        step=step,
                        verify_index=verify_index,
                        item_index=item_index,
                        clause_item=clause_item,
                        step_complete=step_complete,
                    )
                )
    return discovered


def load_map(path: Path) -> list[dict[str, object]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise MapError("MAP_UNREADABLE", f"{path}: {error}") from error
    if document.get("schema_version") != 1:
        raise MapError("SCHEMA_VERSION_INVALID", "schema_version must equal 1")
    entries = document.get("entries")
    if not isinstance(entries, list):
        raise MapError("ENTRIES_INVALID", "entries must be an array")
    return entries


def entry_key(entry: dict[str, object], index: int) -> tuple[str, str, int, int]:
    required = ("plan", "step", "verify_index", "item_index")
    if any(name not in entry for name in required):
        raise MapError("ENTRY_KEY_INCOMPLETE", f"entry {index} has an incomplete key")
    plan = entry["plan"]
    step = entry["step"]
    verify_index = entry["verify_index"]
    item_index = entry["item_index"]
    if not isinstance(plan, str) or not isinstance(step, str):
        raise MapError("ENTRY_KEY_INVALID", f"entry {index} plan/step must be strings")
    if not isinstance(verify_index, int) or not isinstance(item_index, int):
        raise MapError("ENTRY_KEY_INVALID", f"entry {index} indexes must be integers")
    return (plan, step, verify_index, item_index)


def command_executables(command: str, entry_index: int) -> list[str]:
    protected = re.sub(
        r"<[^>]*>", lambda match: match.group(0).replace(" ", "\x1f"), command
    )
    try:
        lexer = shlex.shlex(protected, posix=True, punctuation_chars=";&|")
        lexer.whitespace_split = True
        lexer.commenters = ""
        tokens = [token.replace("\x1f", " ") for token in lexer]
    except ValueError as error:
        raise MapError(
            "RECORDED_COMMAND_INVALID", f"entry {entry_index}: {error}"
        ) from error

    executables: list[str] = []
    expect_executable = True
    for token in tokens:
        if token in COMMAND_SEPARATORS:
            if expect_executable:
                raise MapError(
                    "RECORDED_COMMAND_INVALID",
                    f"entry {entry_index}: empty compound-command segment",
                )
            expect_executable = True
            continue
        if not expect_executable:
            continue
        if ENV_ASSIGNMENT_PATTERN.match(token):
            continue
        executables.append(token)
        expect_executable = False

    if expect_executable or not executables:
        raise MapError(
            "RECORDED_COMMAND_INVALID",
            f"entry {entry_index}: no executable in recorded command",
        )
    return executables


def require_resolvable_executables(
    command: str, entry_index: int, project_root: Path
) -> None:
    for executable in command_executables(command, entry_index):
        if "/" not in executable:
            if shutil.which(executable) is None:
                raise MapError(
                    "RECORDED_EXECUTABLE_UNRESOLVED",
                    f"entry {entry_index}: {executable} is not on PATH",
                )
            continue

        executable_path = Path(executable)
        if not executable_path.is_absolute():
            executable_path = (project_root / executable_path).resolve()
            try:
                executable_path.relative_to(project_root)
            except ValueError as error:
                raise MapError(
                    "RECORDED_EXECUTABLE_UNRESOLVED",
                    f"entry {entry_index}: {executable} leaves the project root",
                ) from error
        if not executable_path.is_file() or not os.access(executable_path, os.X_OK):
            raise MapError(
                "RECORDED_EXECUTABLE_UNRESOLVED",
                f"entry {entry_index}: {executable} is not an executable file",
            )


def check_map(
    discovered: list[ClauseItem],
    entries: list[dict[str, object]],
    project_root: Path,
) -> None:
    expected = {item.key: item for item in discovered}
    mapped: dict[tuple[str, str, int, int], dict[str, object]] = {}
    symbols: set[str] = set()

    for index, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise MapError("ENTRY_INVALID", f"entry {index} must be an object")
        key = entry_key(entry, index)
        if key in mapped:
            raise MapError("ITEM_MAPPED_TWICE", f"duplicate mapping for {key}")
        if key not in expected:
            raise MapError("STALE_MAPPING", f"entry {index} maps no active item: {key}")

        clause_item = entry.get("clause_item")
        if clause_item != expected[key].clause_item:
            raise MapError(
                "CLAUSE_TEXT_MISMATCH",
                f"entry {index} does not match active text for {key}",
            )
        symbol = entry.get("test_symbol")
        if not isinstance(symbol, str) or not symbol.strip():
            raise MapError("TEST_SYMBOL_EMPTY", f"entry {index} has no test symbol")
        if symbol in symbols:
            raise MapError("TEST_SYMBOL_DUPLICATE", f"duplicate test symbol: {symbol}")
        symbols.add(symbol)
        command = entry.get("command")
        if not isinstance(command, str) or not command.strip():
            raise MapError("COMMAND_EMPTY", f"entry {index} has no command")
        proof_kind = entry.get("proof_kind")
        if proof_kind not in PROOF_KINDS:
            raise MapError(
                "PROOF_KIND_INVALID", f"entry {index} has invalid proof_kind"
            )
        proof_status = entry.get("proof_status")
        if proof_status not in PROOF_STATUSES:
            raise MapError(
                "PROOF_STATUS_INVALID", f"entry {index} has invalid proof_status"
            )
        expected_status = "recorded" if expected[key].step_complete else "planned"
        if proof_status != expected_status:
            raise MapError(
                "PROOF_STATUS_MISMATCH",
                f"entry {index} must be {expected_status} for its current checkbox",
            )
        if proof_status == "recorded" and command.lstrip().startswith("PLANNED:"):
            raise MapError(
                "RECORDED_COMMAND_PLANNED",
                f"entry {index}: recorded proof cannot retain a PLANNED command",
            )
        if proof_status == "recorded" and proof_kind == "automated":
            require_resolvable_executables(command, index, project_root)
        mapped[key] = entry

    missing = [item for item in discovered if item.key not in mapped]
    if missing:
        first = missing[0]
        raise MapError(
            "ACTIVE_ITEM_UNMAPPED",
            f"{len(missing)} active item(s) unmapped; first is {first.key}: "
            f"{first.clause_item}",
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path)
    parser.add_argument("--plans-dir", type=Path)
    parser.add_argument("--map", dest="map_path", type=Path)
    parser.add_argument("--dump-discovered", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    script_root = Path(__file__).resolve().parents[2]
    project_root = (args.project_root or script_root).resolve()
    plans_dir = (args.plans_dir or project_root / ".claude/plans").resolve()
    map_path = (
        args.map_path or project_root / "docs/testing/plan-step-verification-map.json"
    ).resolve()
    try:
        discovered = discover(plans_dir, project_root)
        if args.dump_discovered:
            print(
                json.dumps(
                    [item.__dict__ for item in discovered], indent=2, ensure_ascii=False
                )
            )
            return 0
        entries = load_map(map_path)
        check_map(discovered, entries, project_root)
    except MapError as error:
        print(f"{ERROR_PREFIX}.{error.code} {error.detail}", file=sys.stderr)
        return 1
    print(
        f"{ERROR_PREFIX}.COMPLETE active_items={len(discovered)} "
        f"unique_symbols={len(discovered)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
