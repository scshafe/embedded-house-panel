#!/usr/bin/env python3
"""Host tests for scripts/deploy/frdm_board_health.sh (the hardware gate) with a
fake `ssh` on PATH and an invented factory backup.

Every value here is invented test data, never hardware evidence. The tests
prove the record's pass/fail/unobserved logic and that the script sends only
read-only commands to the board.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = PROJECT_ROOT / "scripts/deploy/frdm_board_health.sh"
P1_START, P1_SECTORS = 4, 2
IMAGE = bytes(range(256)) * (2 * (P1_START + P1_SECTORS + 2))  # 8 sectors of invented bytes
BOOT0, BOOT1, QSPI = b"boot0" * 100, b"boot0" * 100, b"qspi" * 100
WRITE_WORDS = ("rm ", "mkfs", "of=/dev", "opkg", "apt", "dnf", "mount ", "> /", ">/dev", "uuu", "reboot", "flash")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class BoardHealthTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "ssh.log"

    def tearDown(self):
        self.tmp.cleanup()

    def fake_ssh(self, *, reachable=True, p1=None):
        p1 = sha(IMAGE[P1_START * 512:(P1_START + P1_SECTORS) * 512]) if p1 is None else p1
        system = (f"os=Invented Distro\nkernel=0.0.0\nmodel=NXP FRDM-IMX8MPLUS\nuptime_seconds=10.0\n"
                  f"failed_units=weston.service\nthermal_max_millicelsius=40000\n"
                  f"mmcblk2p1_start={P1_START}\nmmcblk2p1_sectors={P1_SECTORS}\n")
        storage = (f"mmcblk2boot0={sha(BOOT0)}\nmmcblk2boot1={sha(BOOT1)}\nmtdblock0={sha(QSPI)}\n"
                   f"mbr={sha(IMAGE[:512])}\nbootloader_region={sha(IMAGE[:P1_START * 512])}\nboot_partition={p1}\n")
        (self.root / "system.out").write_text(system)
        (self.root / "storage.out").write_text(storage)
        script = self.bin / "ssh"
        script.write_text("#!/bin/sh\n"
                          f'for a; do last=$a; done; printf "%s\\n---\\n" "$last" >>"{self.log}"\n'
                          + ("" if reachable else "exit 255\n")
                          + f'case "$last" in *mmcblk2boot0*) cat "{self.root}/storage.out" ;; '
                          f'*) cat "{self.root}/system.out" ;; esac\n')
        script.chmod(0o755)

    def backup(self, image=IMAGE) -> Path:
        directory = self.root / "backup"
        directory.mkdir()
        (directory / "SHA256SUMS").write_text(
            f"{sha(BOOT0)}  mmcblk2boot0.img\n{sha(BOOT1)}  mmcblk2boot1.img\n{sha(QSPI)}  mtdblock0.img\n")
        if shutil.which("zstd"):
            raw = self.root / "emmc.img"
            raw.write_bytes(image)
            subprocess.run(["zstd", "-q", "-o", str(directory / "frdm-factory-emmc.img.zst"), str(raw)], check=True)
        return directory

    def run_script(self, **env):
        out = self.root / "out"
        full = {**os.environ, "PATH": f"{self.bin}:{os.environ['PATH']}", "BHP_BOARD_SSH_TARGET": "root@board.invalid",
                **env}
        proc = subprocess.run(["sh", str(SCRIPT), "--output-dir", str(out)], env=full, capture_output=True, text=True)
        record = json.loads((out / "frdm_board_health.json").read_text())
        return proc, {c["name"]: c["status"] for c in record["checks"]}, record

    def test_a_target_is_required(self):
        proc = subprocess.run(["sh", str(SCRIPT)], env={**os.environ, "BHP_BOARD_SSH_TARGET": ""},
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("FRDM_BOARD_HEALTH.SSH_TARGET_INVALID", proc.stdout + proc.stderr)

    def test_unchanged_storage_is_complete(self):
        self.fake_ssh()
        proc, checks, record = self.run_script(BHP_FRDM_FACTORY_BACKUP_DIR=str(self.backup()),
                                               BHP_BOARD_CONSOLE=os.devnull)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(checks["ssh_reachable"], "pass")
        self.assertEqual(checks["console_device"], "pass")
        for name in ("storage_boot0", "storage_boot1", "storage_qspi"):
            self.assertEqual(checks[name], "pass", name)
        regions = ("storage_mbr_partition_table", "storage_bootloader_region", "storage_boot_partition")
        expected = "pass" if shutil.which("zstd") else "unobserved"
        self.assertEqual([checks[n] for n in regions], [expected] * 3)
        self.assertEqual(record["failed_units"], "weston.service")
        self.assertEqual(record["result"], "complete" if shutil.which("zstd") else "incomplete")

    def test_a_changed_boot_partition_is_a_mismatch(self):
        if not shutil.which("zstd"):
            self.skipTest("zstd is not installed")
        self.fake_ssh(p1="0" * 64)
        proc, checks, record = self.run_script(BHP_FRDM_FACTORY_BACKUP_DIR=str(self.backup()))
        self.assertEqual(proc.returncode, 1)
        self.assertEqual((checks["storage_boot_partition"], record["result"]), ("fail", "mismatch"))

    def test_without_a_backup_storage_is_unobserved(self):
        self.fake_ssh()
        proc, checks, record = self.run_script()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual((checks["storage_qspi"], checks["console_device"], record["result"]),
                         ("unobserved", "unobserved", "incomplete"))

    def test_an_unreachable_board_fails(self):
        self.fake_ssh(reachable=False)
        proc, checks, _ = self.run_script()
        self.assertEqual((proc.returncode, checks["ssh_reachable"]), (1, "fail"))

    def test_board_commands_only_read(self):
        self.fake_ssh()
        self.run_script(BHP_FRDM_FACTORY_BACKUP_DIR=str(self.backup()))
        sent = self.log.read_text().replace("2>/dev/null", "")  # discarding stderr is not a write
        self.assertIn("dd if=/dev/mmcblk2 ", sent)
        for word in WRITE_WORDS:
            self.assertNotIn(word, sent, word)


if __name__ == "__main__":
    unittest.main()
