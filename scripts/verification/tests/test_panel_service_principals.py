"""Fail-closed cases for the service principal and device matrix verifier."""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VERIFIER = ROOT / "scripts/verification/verify_service_principals.py"


class ServicePrincipalMatrixTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bhp-principals-")
        self.root = Path(self.temporary.name)
        shutil.copytree(ROOT / "system_image", self.root / "system_image",
                        ignore=shutil.ignore_patterns("output", "downloads", "buildroot"))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def verify(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(VERIFIER), "--root", str(self.root)],
                              text=True, capture_output=True, check=False)

    def mutate(self, relative: str, old: str, new: str) -> None:
        path = self.root / relative
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count(old), 1, old)
        path.write_text(text.replace(old, new), encoding="utf-8")

    def assert_fails(self, code: str) -> None:
        result = self.verify()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(f"SERVICE_PRINCIPALS.{code}", result.stderr)

    def test_repository_matrix_passes(self) -> None:
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["created_users"], ["panel-ui"])

    def test_unlisted_display_grant_fails(self) -> None:
        self.mutate("system_image/buildroot_external/package/bhp-panel-ui/bhp-panel-ui.mk",
                    "panel-ui 1102 panel-ui 1102 * - - - BeagleBone",
                    "panel-ui 1102 panel-ui 1102 * - - video BeagleBone")
        self.assert_fails("PRINCIPAL_MISMATCH")

    def test_generic_audio_group_fails(self) -> None:
        matrix = self.root / "system_image/service-principals.json"
        document = json.loads(matrix.read_text(encoding="utf-8"))
        panel_ui = next(p for p in document["principals"] if p["role"] == "panel_ui")
        panel_ui["supplementary_groups"] = ["audio"]
        matrix.write_text(json.dumps(document), encoding="utf-8")
        self.mutate("system_image/buildroot_external/package/bhp-panel-ui/bhp-panel-ui.mk",
                    "panel-ui 1102 panel-ui 1102 * - - - BeagleBone",
                    "panel-ui 1102 panel-ui 1102 * - - audio BeagleBone")
        self.assert_fails("FORBIDDEN_GROUP")

    def test_sound_nodes_must_stay_root_only(self) -> None:
        self.mutate("system_image/buildroot_external/board/beaglebone-house-panel/post-build.sh",
                    'print $1 "\\troot:root 600"', 'print $1 "\\troot:audio 660"')
        self.assert_fails("SOUND_RULE_MISSING")

    def test_retired_service_cannot_return(self) -> None:
        (self.root / "system_image/buildroot_external/board/beaglebone-house-panel/"
         "rootfs-overlay/etc/init.d/S80bhp-vocabulary-panel").write_text("#!/bin/sh\n")
        self.assert_fails("RETIRED_PRINCIPAL_PRESENT")

    def test_reserved_uid_collision_fails(self) -> None:
        matrix = self.root / "system_image/service-principals.json"
        document = json.loads(matrix.read_text(encoding="utf-8"))
        broker = next(p for p in document["principals"] if p["role"] == "media_broker")
        broker["reserved_uid"] = 1102
        matrix.write_text(json.dumps(document), encoding="utf-8")
        self.assert_fails("UID_SHARED")


if __name__ == "__main__":
    unittest.main()
