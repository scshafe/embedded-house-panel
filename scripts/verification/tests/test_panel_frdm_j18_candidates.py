"""Fail-closed cases for the documentation-derived FRDM J18 PIR pad candidates."""

from __future__ import annotations

import contextlib
import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/verification"))
import check_frdm_j18_candidates as checker  # noqa: E402


class FrdmJ18CandidatesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.document = json.loads(
            (ROOT / "docs/hardware/frdm-j18-pir-candidates.json").read_text())
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-j18-")
        self.addCleanup(self.temporary.cleanup)
        for name in ("CANDIDATES", "PROFILE"):
            self.addCleanup(setattr, checker, name, getattr(checker, name))

    def run_checker(self, document: dict, profile_text: str | None = None) -> int:
        path = Path(self.temporary.name) / "candidates.json"
        path.write_text(json.dumps(document))
        checker.CANDIDATES = path
        if profile_text is not None:
            profile = Path(self.temporary.name) / "frdm-imx8mp.env"
            profile.write_text(profile_text)
            checker.PROFILE = profile
        with contextlib.redirect_stdout(io.StringIO()):
            return checker.main()

    def assert_fails(self, document: dict, code: str, profile_text: str | None = None) -> None:
        with self.assertRaises(checker.CandidateError) as raised:
            self.run_checker(document, profile_text)
        self.assertIn(f"FRDM_J18_CANDIDATES.{code}", str(raised.exception))

    def test_repository_candidates_pass(self) -> None:
        self.assertEqual(self.run_checker(self.document), 0)

    def test_a_claimed_pin_or_voltage_is_rejected(self) -> None:
        document = copy.deepcopy(self.document)
        document["candidates"][0]["j18_pin"] = "J18-7"
        self.assert_fails(document, "MEASUREMENT_CLAIMED")

    def test_a_cited_schematic_revision_allows_pin_numbers(self) -> None:
        document = copy.deepcopy(self.document)
        schematic = next(source for source in document["sources"]
                         if source["id"] == "frdm_schematic")
        schematic["status"] = "consulted"
        self.assert_fails(document, "SCHEMATIC_REVISION_MISSING")
        schematic["revision"] = "SPF-XXXXX rev A"
        self.assert_fails(document, "SCHEMATIC_FIELDS_INVALID")
        for candidate in document["candidates"]:
            candidate["j18_pin"] = "NOT_ON_J18"
            candidate["bank_supply"] = "NVCC_GPIO"
        document["candidates"][0]["j18_pin"] = "J18-7"
        self.assertEqual(self.run_checker(document), 0)
        document["candidates"][0]["j18_pin"] = "J18-41"
        self.assert_fails(document, "SCHEMATIC_FIELDS_INVALID")

    def test_measurement_fields_are_rejected(self) -> None:
        document = copy.deepcopy(self.document)
        document["candidates"][0]["header_voltage_mv"] = 3300
        self.assert_fails(document, "MEASUREMENT_CLAIMED")

    def test_gpio_fields_must_match_the_pad_mux(self) -> None:
        document = copy.deepcopy(self.document)
        document["candidates"][0]["gpio_line"] = 11
        self.assert_fails(document, "GPIO_MISMATCH")

    def test_expander_lines_are_never_candidates(self) -> None:
        document = copy.deepcopy(self.document)
        document["candidates"][0]["default_owner"] = "pcal6416 line 3"
        self.assert_fails(document, "EXPANDER_CANDIDATE")

    def test_ranks_are_contiguous(self) -> None:
        document = copy.deepcopy(self.document)
        document["candidates"][1]["rank"] = 5
        self.assert_fails(document, "RANK_ORDER")

    def test_a_measured_profile_supersedes_the_list(self) -> None:
        profile = (ROOT / "config/boards/frdm-imx8mp.env").read_text().replace(
            "BHP_BOARD_PIR_PAD_MUX=UNMEASURED:FRDM-GATE-PIR",
            "BHP_BOARD_PIR_PAD_MUX=MX8MP_IOMUXC_GPIO1_IO10__GPIO1_IO10")
        self.assert_fails(copy.deepcopy(self.document), "PROFILE_DIVERGED", profile)
        document = copy.deepcopy(self.document)
        document["superseded_by_measurement"] = True
        self.assertEqual(self.run_checker(document, profile), 0)


if __name__ == "__main__":
    unittest.main()
