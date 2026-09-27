#!/usr/bin/env python3
"""Fail closed when the J18 PIR pad candidates overstate what documents can prove.

The candidates list is documentation-derived: it may rank native SoC pads and name their
GPIO alternates. J18 pin numbers and bank supplies appear only once a named schematic
revision is cited, never a measured value; it never offers a PCAL6416A expander line; and
it cannot stay current once the board profile is measured.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--root"
            else Path(__file__).resolve().parents[2]).resolve()
CANDIDATES = ROOT / "docs/hardware/frdm-j18-pir-candidates.json"
PROFILE = ROOT / "config/boards/frdm-imx8mp.env"
PAD_MUX = re.compile(r"^MX8MP_IOMUXC_([A-Z0-9_]+?)__GPIO([1-5])_IO([0-9]{2})$")
UNKNOWN = "UNKNOWN_SCHEMATIC"


class CandidateError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f"FRDM_J18_CANDIDATES.{code}: {detail}")


def fail(code: str, detail: str) -> None:
    raise CandidateError(code, detail)


def profile_value(key: str) -> str:
    for line in PROFILE.read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{key}="):
            return line.split("=", 1)[1]
    fail("PROFILE_KEY_MISSING", key)
    return ""


def main() -> int:
    document = json.loads(CANDIDATES.read_text(encoding="utf-8"))
    if (document.get("schema_version") != 1
            or document.get("canonical") != "frdm-j18-pir-pad-candidates"
            or document.get("evidence") != "documentation_derived_not_measured"):
        fail("SCHEMA_INVALID", str(CANDIDATES.relative_to(ROOT)))
    measured = not profile_value("BHP_BOARD_PIR_PAD_MUX").startswith("UNMEASURED:")
    if measured != bool(document.get("superseded_by_measurement", False)):
        fail("PROFILE_DIVERGED", "a measured PIR pad must mark this list superseded_by_measurement")
    if not measured and document.get("board_profile_state") != "UNMEASURED:FRDM-GATE-PIR":
        fail("PROFILE_DIVERGED", "board_profile_state must mirror the unmeasured profile")
    sources = {source["id"]: source for source in document["sources"]}
    for source_id in ("nxp_bsp_dts", "pinfunc"):
        if not re.fullmatch(r"[0-9a-f]{64}", sources.get(source_id, {}).get("sha256", "")):
            fail("SOURCE_UNPINNED", source_id)
    schematic = sources.get("frdm_schematic", {})
    if schematic.get("status") not in ("not_consulted", "consulted"):
        fail("SCHEMATIC_STATUS_INVALID", str(schematic.get("status")))
    consulted = schematic["status"] == "consulted"
    if consulted and not schematic.get("revision"):
        fail("SCHEMATIC_REVISION_MISSING", "a consulted schematic names its revision")
    seen = set()
    for expected_rank, candidate in enumerate(document["candidates"], start=1):
        name = candidate.get("pad_mux", "")
        if candidate.get("rank") != expected_rank:
            fail("RANK_ORDER", name)
        match = PAD_MUX.match(name)
        if not match:
            fail("PAD_MUX_INVALID", name)
        if "PCA6416" in json.dumps(candidate).upper() or "PCAL6416" in json.dumps(candidate).upper():
            fail("EXPANDER_CANDIDATE", name)
        pad, bank, line = match.group(1), int(match.group(2)), int(match.group(3))
        if (candidate.get("pad") != pad or candidate.get("gpio_controller") != f"gpio{bank}"
                or candidate.get("gpio_line") != line or line > 31):
            fail("GPIO_MISMATCH", name)
        # Pins and rails come only from the cited schematic; voltages only from a meter.
        if not consulted and (candidate.get("j18_pin") != UNKNOWN
                              or candidate.get("bank_supply") != UNKNOWN):
            fail("MEASUREMENT_CLAIMED", name)
        if consulted and (not re.fullmatch(r"J18-([1-9]|[1-3][0-9]|40)|NOT_ON_J18",
                                           str(candidate.get("j18_pin")))
                          or not candidate.get("bank_supply")
                          or candidate.get("bank_supply") == UNKNOWN):
            fail("SCHEMATIC_FIELDS_INVALID", name)
        if any(re.search(r"voltage|_mv$|measured", key) for key in candidate):
            fail("MEASUREMENT_CLAIMED", name)
        if not candidate.get("pad_owner_to_disable") or not candidate.get("default_owner"):
            fail("OWNER_MISSING", name)
        if name in seen:
            fail("DUPLICATE_CANDIDATE", name)
        seen.add(name)
    if not seen:
        fail("NO_CANDIDATES", str(CANDIDATES.relative_to(ROOT)))
    print(json.dumps({"frdm_j18_candidates": "verified", "candidates": len(seen),
                      "profile": "measured" if measured else "unmeasured",
                      "evidence": document["evidence"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CandidateError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
