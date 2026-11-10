"""A cross prefix written in a document is the active board's, which cross_build_applications.sh accepts.

The script refuses any BHP_BUILDROOT_CROSS_PREFIX other than the selected board profile's
(CROSS_BUILD.PREFIX_BOARD_MISMATCH), so a document showing another board's prefix gives a
command that fails as written.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SKIPPED_DIRECTORIES = {".build", ".git", "output", "downloads", "buildroot", "_deps"}


def active_prefixes() -> set[str]:
    prefixes = set()
    for profile in (ROOT / "config/boards").glob("*.env"):
        values = dict(re.findall(r"^([A-Z0-9_]+)=(.*)$", profile.read_text(encoding="utf-8"), re.M))
        if values.get("BHP_BOARD_STATUS") == "active" and values.get("BHP_BOARD_BUILDROOT_CROSS_PREFIX"):
            prefixes.add(values["BHP_BOARD_BUILDROOT_CROSS_PREFIX"].strip("'\""))
    return prefixes


class DocumentedCrossPrefixTest(unittest.TestCase):
    def test_documented_prefixes_are_the_active_boards(self) -> None:
        active = active_prefixes()
        self.assertEqual(len(active), 1, "exactly one active board profile names a cross prefix")
        wrong = []
        for path in ROOT.rglob("*.md"):
            if SKIPPED_DIRECTORIES.intersection(path.relative_to(ROOT).parts):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                for prefix in re.findall(r"BHP_BUILDROOT_CROSS_PREFIX=([A-Za-z0-9._+-]+)", line):
                    if prefix not in active:
                        wrong.append(f"{path.relative_to(ROOT)}:{number}: {prefix}")
        self.assertEqual(wrong, [], f"documented commands must use {sorted(active)}")


if __name__ == "__main__":
    unittest.main()
