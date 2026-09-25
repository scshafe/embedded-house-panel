#!/usr/bin/env python3
"""Validate every config/boards/<id>.env against board-profile.schema.json.

Checks the line grammar, required keys, value patterns, placeholder rules, the
single active profile, referenced files and CMake presets, and that every
UNMEASURED:FRDM-GATE-* value in an active profile names a gate row in ADR 0008.
Also runs negative fixtures through both this validator and the POSIX shell
loader (scripts/boards/board_profile.sh) so neither can silently accept an
unsafe or incomplete profile.

Output: one JSON object on stdout. Exit 0 when valid, 1 otherwise.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = PROJECT_ROOT / "config/boards/board-profile.schema.json"
BOARD_DIRECTORY = PROJECT_ROOT / "config/boards"
SHELL_LOADER = PROJECT_ROOT / "scripts/boards/board_profile.sh"


class ProfileError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(detail)
        self.code = code
        self.detail = detail


def parse_profile(text: str, schema: dict, source: str) -> dict[str, str]:
    assignment = re.compile(schema["line_grammar"]["assignment"])
    values: dict[str, str] = {}
    for number, line in enumerate(text.split("\n"), start=1):
        if line == "" or line.startswith("#"):
            continue
        match = assignment.match(line)
        if match is None:
            raise ProfileError("GRAMMAR_INVALID", f"{source}:{number}")
        key, value = match.group(1), match.group(2)
        if key in values:
            raise ProfileError("KEY_DUPLICATE", f"{source}:{number}: {key}")
        if key in schema.get("reserved_keys", []):
            raise ProfileError("KEY_RESERVED", f"{source}:{number}: {key}")
        if "%" in value and re.fullmatch(r"([^%]|%[0-9A-F]{2})*", value) is None:
            raise ProfileError("PERCENT_ESCAPE_INVALID", f"{source}:{number}: {key}")
        values[key] = value
    return values


def placeholder_kind(value: str, schema: dict) -> str | None:
    for kind, pattern in schema["placeholders"].items():
        if re.match(pattern, value):
            return kind
    return None


def active_gate_ids(schema: dict) -> set[str]:
    source = schema["active_gate_source"]
    row = re.compile(source["row_pattern"])
    gates: set[str] = set()
    for line in (PROJECT_ROOT / source["path"]).read_text(encoding="utf-8").splitlines():
        match = row.match(line)
        if match:
            gates.add(match.group(1))
    return gates


def cmake_presets() -> tuple[set[str], set[str]]:
    document = json.loads((PROJECT_ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    configure = {preset["name"] for preset in document.get("configurePresets", [])}
    build = {preset["name"] for preset in document.get("buildPresets", [])}
    return configure, build


def validate_profile(
    board_id: str, values: dict[str, str], schema: dict, gates: set[str],
    presets: tuple[set[str], set[str]], check_files: bool = True,
) -> list[dict[str, str]]:
    placeholders: list[dict[str, str]] = []
    if not re.match(schema["board_id_pattern"], board_id):
        raise ProfileError("ID_INVALID", board_id)
    status = values.get("BHP_BOARD_STATUS", "")
    required = dict(schema["required_keys"])
    if status == "active":
        required.update(schema["required_keys_when_active"])
    allowed = set(schema["placeholder_allowed_keys"])
    for key, pattern in required.items():
        if key not in values:
            raise ProfileError("KEY_MISSING", f"{board_id}: {key}")
    for key, value in values.items():
        kind = placeholder_kind(value, schema)
        if kind is not None:
            if key not in allowed:
                raise ProfileError("PLACEHOLDER_NOT_ALLOWED", f"{board_id}: {key}")
            identifier = value.split(":", 1)[1]
            if status == "active" and kind == "unmeasured" and identifier not in gates:
                raise ProfileError("GATE_UNKNOWN", f"{board_id}: {key} names {identifier}")
            placeholders.append({"key": key, "kind": kind, "id": identifier})
            continue
        pattern = required.get(key)
        if pattern is not None and not re.match(pattern, value):
            raise ProfileError("VALUE_INVALID", f"{board_id}: {key}={value}")
    if values["BHP_BOARD_ID"] != board_id:
        raise ProfileError("ID_MISMATCH", f"{board_id}.env declares {values['BHP_BOARD_ID']}")
    if status == "active":
        for key, value in values.items():
            if value.startswith("UNMEASURED:") and not value.startswith("UNMEASURED:FRDM-GATE-"):
                raise ProfileError("GATE_UNKNOWN", f"{board_id}: {key} names {value}")
    if check_files:
        for key, template in schema["file_references"].items():
            reference = PROJECT_ROOT / template.format(value=values[key])
            if not reference.exists():
                raise ProfileError("REFERENCE_MISSING", f"{board_id}: {key} -> {reference}")
        for key in schema.get("project_file_references", []):
            if key in values and not (PROJECT_ROOT / values[key]).is_file():
                raise ProfileError("REFERENCE_MISSING", f"{board_id}: {key} -> {values[key]}")
        configure, build = presets
        preset = values[schema["cmake_preset_reference"]]
        if preset not in configure or preset not in build:
            raise ProfileError("CMAKE_PRESET_MISSING", f"{board_id}: {preset}")
    return placeholders


def shell_loader_accepts(text: str) -> bool:
    with tempfile.TemporaryDirectory(prefix="bhp-board-profile-") as temporary:
        profile = Path(temporary) / "fixture.env"
        profile.write_text(text, encoding="utf-8")
        result = subprocess.run(
            ["sh", "-c", '. "$1"; bhp_board_profile_load_file "$2"', "sh",
             str(SHELL_LOADER), str(profile)],
            check=False, capture_output=True, text=True,
        )
        return result.returncode == 0


def run_negative_fixtures(schema: dict, gates: set[str], presets) -> int:
    base = (BOARD_DIRECTORY / "frdm-imx8mp.env").read_text(encoding="utf-8")
    count = 0
    grammar_cases = {
        "command_substitution": base + "BHP_BOARD_EXTRA=$(id)\n",
        "backtick": base + "BHP_BOARD_EXTRA=`id`\n",
        "semicolon": base + "BHP_BOARD_EXTRA=a;id\n",
        "space": base + "BHP_BOARD_EXTRA=a b\n",
        "quote": base + "BHP_BOARD_EXTRA='a'\n",
        "foreign_key": base + "PATH=/tmp\n",
        "duplicate": base + "BHP_BOARD_ID=frdm-imx8mp\n",
    }
    reserved_cases = {
        "reserved_deploy_key": base + "BHP_BOARD_UPLOAD_DIR=/tmp/x\n",
    }
    grammar_cases.update(reserved_cases)
    for name, text in grammar_cases.items():
        try:
            parse_profile(text, schema, name)
        except ProfileError:
            pass
        else:
            raise ProfileError("SELF_TEST_ACCEPTED", f"validator accepted {name}")
        if shell_loader_accepts(text):
            raise ProfileError("SELF_TEST_ACCEPTED", f"shell loader accepted {name}")
        count += 1
    semantic_cases = {
        "unknown_gate": base.replace(
            "BHP_BOARD_PIR_GPIO_LINE=UNMEASURED:FRDM-GATE-PIR",
            "BHP_BOARD_PIR_GPIO_LINE=UNMEASURED:FRDM-GATE-GUESSED"),
        "guessed_expander_pin": base.replace(
            "BHP_BOARD_PIR_GPIO_CONTROLLER=UNMEASURED:FRDM-GATE-PIR",
            "BHP_BOARD_PIR_GPIO_CONTROLLER=pca6416_a"),
        "placeholder_on_build_key": base.replace(
            "BHP_BOARD_TARGET_ARCH=aarch64",
            "BHP_BOARD_TARGET_ARCH=UNMEASURED:FRDM-GATE-PIR"),
        "missing_required": base.replace("BHP_BOARD_BOOT_DTB=imx8mp-frdm-bhp-panel.dtb\n", ""),
        "bad_mem_sleep": base.replace(
            "BHP_BOARD_MEM_SLEEP_STATE=UNMEASURED:FRDM-GATE-SUSPEND",
            "BHP_BOARD_MEM_SLEEP_STATE=standby"),
        "bad_percent_escape": base.replace(
            "BHP_BOARD_TOUCH_INPUT_NAME=UNMEASURED:FRDM-GATE-TOUCH",
            "BHP_BOARD_TOUCH_INPUT_NAME=Goodix%2gCapacitive"),
    }
    for name, text in semantic_cases.items():
        if text == base:
            raise ProfileError("SELF_TEST_FIXTURE_STALE", name)
        try:
            values = parse_profile(text, schema, name)
            validate_profile("frdm-imx8mp", values, schema, gates, presets, check_files=False)
        except ProfileError:
            count += 1
            continue
        raise ProfileError("SELF_TEST_ACCEPTED", f"validator accepted {name}")
    encoded_name = base.replace(
        "BHP_BOARD_TOUCH_INPUT_NAME=UNMEASURED:FRDM-GATE-TOUCH",
        "BHP_BOARD_TOUCH_INPUT_NAME=Goodix%20Capacitive%20TouchScreen")
    if encoded_name == base:
        raise ProfileError("SELF_TEST_FIXTURE_STALE", "percent_encoded_touch_name")
    validate_profile("frdm-imx8mp", parse_profile(encoded_name, schema, "encoded"),
                     schema, gates, presets, check_files=False)
    return count


TOUCH_CLASSIFIER = (PROJECT_ROOT / "system_image/buildroot_external/board/beaglebone-house-panel"
                    / "rootfs-overlay/usr/libexec/beaglebone-house-panel/classify-touch-input")


def check_pir_consumers(board_id: str, values: dict[str, str], schema: dict) -> None:
    """Device-tree node names and PIR input names must match their consumers."""
    classifier = TOUCH_CLASSIFIER.read_text(encoding="utf-8")
    for key in ("BHP_BOARD_PIR_INPUT_NAME", "BHP_BOARD_PIR_CUSTOM_INPUT_NAME"):
        name = values.get(key)
        if name is None or placeholder_kind(name, schema) is not None:
            continue
        if classifier.count(f"'{name}'") < 2:
            raise ProfileError("PIR_NAME_NOT_DENIED_AS_TOUCH", f"{board_id}: {key}={name}")
    wake_label = values.get("BHP_BOARD_PIR_WAKE_INTERRUPT_LABEL")
    if wake_label is not None and "BHP_BOARD_PROJECT_OVERLAY_TEMPLATE" in values:
        template = (PROJECT_ROOT / values["BHP_BOARD_PROJECT_OVERLAY_TEMPLATE"]).read_text(
            encoding="utf-8")
        if f'label = "{wake_label}";' not in template:
            raise ProfileError("PIR_WAKE_LABEL_NOT_IN_OVERLAY", f"{board_id}: {wake_label}")
    node_sources = {
        "BHP_BOARD_PIR_DT_NODE": ("BHP_BOARD_PROJECT_OVERLAY_TEMPLATE",
                                  "presence_sensor/experiments/stock_gpio_keys_wake_baseline"),
        "BHP_BOARD_PIR_CUSTOM_DT_NODE": ("BHP_BOARD_PRESENCE_CUSTOM_OVERLAY_TEMPLATE",
                                         "presence_sensor/experiments/custom_pir_input_driver"),
    }
    for key, (template_key, history_directory) in node_sources.items():
        node = values[key]
        if template_key in values:
            candidates = [PROJECT_ROOT / values[template_key]]
        else:
            candidates = sorted((PROJECT_ROOT / history_directory).glob("beagle-house-*.dts"))
        if not any(re.search(rf"^\s+{re.escape(node)} \{{$", path.read_text(encoding="utf-8"),
                             re.MULTILINE) for path in candidates):
            raise ProfileError("PIR_DT_NODE_NOT_IN_OVERLAY", f"{board_id}: {key}={node}")


def main() -> int:
    try:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        if schema.get("schema_version") != 1:
            raise ProfileError("SCHEMA_UNSUPPORTED", str(SCHEMA_PATH))
        gates = active_gate_ids(schema)
        if not gates:
            raise ProfileError("GATE_SOURCE_EMPTY", schema["active_gate_source"]["path"])
        presets = cmake_presets()
        report: dict[str, object] = {}
        active: list[str] = []
        for path in sorted(BOARD_DIRECTORY.glob("*.env")):
            board_id = path.stem
            values = parse_profile(path.read_text(encoding="utf-8"), schema, path.name)
            if not SHELL_LOADER.exists() or not shell_loader_accepts(path.read_text(encoding="utf-8")):
                raise ProfileError("SHELL_LOADER_REJECTED", path.name)
            placeholders = validate_profile(board_id, values, schema, gates, presets)
            check_pir_consumers(board_id, values, schema)
            if values["BHP_BOARD_STATUS"] == "active":
                active.append(board_id)
            report[board_id] = {
                "status": values["BHP_BOARD_STATUS"],
                "unmeasured": sorted({p["id"] for p in placeholders if p["kind"] == "unmeasured"}),
                "human_decisions": sorted({p["id"] for p in placeholders if p["kind"] == "human_decision"}),
                "placeholder_count": len(placeholders),
            }
        if len(active) != 1:
            raise ProfileError("ACTIVE_COUNT_INVALID", f"expected 1 active profile, found {active}")
        negative = run_negative_fixtures(schema, gates, presets)
    except ProfileError as error:
        print(json.dumps({"status": "failed", "error_code": f"BOARD_PROFILE.{error.code}",
                          "detail": error.detail, "hardware_evidence": "not_performed"}))
        return 1
    print(json.dumps({"status": "passed", "active_board": active[0], "profiles": report,
                      "adr_0008_gates": sorted(gates), "negative_fixtures": negative,
                      "hardware_evidence": "not_performed"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
