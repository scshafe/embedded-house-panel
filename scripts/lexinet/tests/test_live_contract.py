"""Dry-run contract of the live Lexinet study.v1 runner (no Lexinet needed)."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/lexinet/live_contract.py"


class LiveContractDryRunTest(unittest.TestCase):
    def run_script(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(SCRIPT), *arguments], text=True,
                              capture_output=True, check=False)

    def test_dry_run_prints_plan_without_side_effects(self) -> None:
        result = self.run_script("--lexinet-source-dir", "/nonexistent/lexinet",
                                 "--state-root", "/nonexistent/state")
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertFalse(plan["execute"])
        self.assertEqual(plan["evidence"], "simulation_not_hardware_evidence")
        for scenario in ("create_resume", "study_and_sync", "auth_failure", "offline_queue",
                         "recover", "crash_after_send", "resume_after_server_restart",
                         "exhaust_and_successor", "atomic_rejection"):
            self.assertIn(scenario, plan["scenarios"])
        self.assertFalse(Path("/nonexistent/state").exists())

    def test_without_a_checkout_the_plan_is_the_pinned_fetch(self) -> None:
        result = self.run_script("--state-root", "/nonexistent/state")
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["lexinet_source_dir"],
                         "pinned_fetch:ce42272df315f357e10998dd48e9ffd1a935c2ff")

    def test_invalid_checkout_fails_closed_without_secrets(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bhp-live-contract-test-") as temporary:
            state = Path(temporary) / "state"
            result = self.run_script("--execute", "--lexinet-source-dir", temporary,
                                     "--client", sys.executable, "--state-root", str(state))
            self.assertEqual(result.returncode, 1)
            report = json.loads(result.stdout.strip().splitlines()[-1])
            self.assertFalse(report["ok"])
            self.assertEqual(report["error"]["code"], "LEXINET_SOURCE_INVALID")
            self.assertFalse((state / "secrets").exists())


if __name__ == "__main__":
    unittest.main()
