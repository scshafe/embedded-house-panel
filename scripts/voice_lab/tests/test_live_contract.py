"""Dry-run contract of the live corpus-client.v1 runner (no voice-lab needed)."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/voice_lab/live_contract.py"
sys.path.insert(0, str(ROOT / "scripts/voice_lab"))

import live_contract  # noqa: E402


class LiveContractDryRunTest(unittest.TestCase):
    def run_script(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, "-B", str(SCRIPT), *arguments], text=True,
                              capture_output=True, check=False)

    def test_dry_run_prints_the_plan_without_side_effects(self) -> None:
        result = self.run_script("--voice-lab-source-dir", "/nonexistent/voice-lab",
                                 "--state-root", "/nonexistent/state")
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertFalse(plan["execute"])
        self.assertEqual(plan["evidence"], "simulation_not_hardware_evidence")
        self.assertEqual(plan["scenarios"], ["sign_in", "library", "intake_interrupted", "intake_resume",
                                             "decide", "outbox", "reuse_revokes"])
        self.assertEqual(plan["fixture"], "cole_voice_lab.contract_fixture")
        self.assertEqual(plan["locked_closure"], "requirements/vault-linux.txt")
        self.assertFalse(Path("/nonexistent/state").exists())

    def test_without_a_checkout_the_plan_is_the_pinned_fetch(self) -> None:
        result = self.run_script("--state-root", "/nonexistent/state")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["voice_lab_source_dir"],
                         "pinned_fetch:8f516567a38a1ef10cae23060623633791b24b2f")

    def test_an_invalid_checkout_fails_closed_and_leaves_no_secrets(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bhp-voice-lab-live-test-") as temporary:
            base = Path(temporary)
            client = base / "client"
            client.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            client.chmod(0o755)
            (base / "voice-lab").mkdir()
            result = self.run_script("--execute", "--voice-lab-source-dir", str(base / "voice-lab"),
                                     "--client", str(client), "--state-root", str(base / "state"))
            self.assertEqual(result.returncode, 1)
            report = json.loads(result.stdout)
            self.assertFalse(report["ok"])
            self.assertEqual(report["error"]["code"], "VOICE_LAB_SOURCE_INVALID")
            self.assertFalse((base / "state/tls").exists())
            self.assertFalse((base / "state/panel").exists())

    def test_a_missing_client_is_reported_first(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bhp-voice-lab-live-test-") as temporary:
            base = Path(temporary)
            result = self.run_script("--execute", "--voice-lab-source-dir", str(base),
                                     "--client", str(base / "missing"), "--state-root", str(base / "state"))
            self.assertEqual(result.returncode, 1)
            self.assertEqual(json.loads(result.stdout)["error"]["code"], "CLIENT_MISSING")

    def test_an_explicit_python_must_be_cpython_3_12(self) -> None:
        if sys.version_info[:2] == (3, 12):
            self.skipTest("the test interpreter is 3.12")
        with self.assertRaises(live_contract.ContractError) as caught:
            live_contract.find_python(sys.executable)
        self.assertEqual(caught.exception.code, "PYTHON_VERSION_MISMATCH")


if __name__ == "__main__":
    unittest.main()
