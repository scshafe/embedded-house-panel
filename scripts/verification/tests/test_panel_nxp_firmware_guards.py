"""ADR 0016's conditions on the NXP firmware: none of it, and no image holding it, ever leaves the PC.

- No tracked file is NXP firmware or a boot image, and .gitignore keeps naming them.
- No workflow uploads anything but the enclosure's printable parts, and none publishes a release.
"""

from __future__ import annotations

import fnmatch
import re
import subprocess
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[3]
FIRMWARE_PATTERNS = ("*.img", "*.img.xz", "imx8-boot-sd.bin", "ddr*_fw*.bin", "lpddr4*.bin", "firmware-imx-*.bin",
                     "*.bin.se", "helper_uart_*.bin", "wifi_mod_para.conf", "sdcard.img*")
IGNORE_LINES = ("/images/", "imx8-boot-sd.bin", "ddr*_fw*.bin", "lpddr4*.bin", "firmware-imx-*/", "*.bin.se")
ALLOWED_UPLOAD_PATHS = {".build/enclosure/out/"}
RELEASE_PATTERN = re.compile(r"action-gh-release|actions/create-release|gh release|upload-release-asset", re.I)


class NxpFirmwareGuardTest(unittest.TestCase):
    def test_no_tracked_file_is_nxp_firmware_or_a_boot_image(self) -> None:
        tracked = subprocess.run(["git", "-C", str(PROJECT_ROOT), "ls-files"], capture_output=True, text=True,
                                 check=True).stdout.splitlines()
        offending = [path for path in tracked
                     if any(fnmatch.fnmatch(Path(path).name, pattern) for pattern in FIRMWARE_PATTERNS)]
        self.assertEqual(offending, [])

    def test_gitignore_keeps_the_firmware_out(self) -> None:
        lines = set((PROJECT_ROOT/".gitignore").read_text().splitlines())
        self.assertEqual([line for line in IGNORE_LINES if line not in lines], [])

    def test_workflows_publish_no_image(self) -> None:
        for workflow in sorted((PROJECT_ROOT/".github/workflows").glob("*.y*ml")):
            text = workflow.read_text()
            self.assertIsNone(RELEASE_PATTERN.search(text), f"{workflow.name} publishes a release")
            lines = text.splitlines()
            for index, line in enumerate(lines):
                if not re.match(r"\s*-?\s*uses:\s*actions/upload-artifact@", line):
                    continue
                paths = []
                for following in lines[index + 1:]:
                    if re.match(r"\s*- ", following):
                        break  # the next step
                    match = re.match(r"\s*path:\s*(\S.*)$", following)
                    if match:
                        paths.append(match[1].strip())
                self.assertTrue(paths, f"{workflow.name}: an upload without a single-line path")
                for path in paths:
                    self.assertIn(path, ALLOWED_UPLOAD_PATHS, f"{workflow.name} uploads {path}")


if __name__ == "__main__":
    unittest.main()
