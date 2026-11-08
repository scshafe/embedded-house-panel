"""The agent's commands: list, show, set-status, public-summary and retention."""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from support import REPORT_ID, bundle

from bhp_report_inbox.__main__ import main
from bhp_report_inbox.store import Store


class CliTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = self.temporary.name
        store = Store(Path(self.root))
        manifest, files = bundle("text", description="Skip did nothing while Alex was cooking")
        for name, data in files.items():
            store.put_range(REPORT_ID, name, f"bytes 0-{len(data) - 1}/{len(data)}", data)
        store.complete(REPORT_ID, json.dumps(manifest).encode())

    def run_cli(self, *args: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = main(["--root", self.root, *args])
        return code, out.getvalue(), err.getvalue()

    def test_the_agents_flow(self) -> None:
        code, out, _ = self.run_cli("list", "--new")
        self.assertEqual(code, 0)
        self.assertEqual([row["report_id"] for row in json.loads(out)], [REPORT_ID])
        code, out, _ = self.run_cli("show", REPORT_ID)
        shown = json.loads(out)
        self.assertTrue(shown["has_description_text"])
        self.assertNotIn("Alex", out, "show prints paths and closed fields, never the description")
        self.assertEqual(self.run_cli("set-status", REPORT_ID, "fix_in_pr", "--pr", "81")[0], 0)
        self.assertEqual(json.loads(self.run_cli("list", "--new")[1]), [], "a taken report is no longer new")
        code, out, _ = self.run_cli("public-summary", REPORT_ID, "--markdown")
        self.assertEqual(code, 0)
        self.assertNotIn("Alex", out)
        self.assertIn("Panel report: text report on music", out)
        code, _, err = self.run_cli("set-status", REPORT_ID, "merged", "--pr", "-3")
        self.assertEqual((code, json.loads(err)["error_code"]), (1, "REPORT_INBOX.PR_INVALID"))
        code, _, err = self.run_cli("show", "rpt-" + "e" * 32)
        self.assertEqual((code, json.loads(err)["error_code"]), (1, "REPORT_INBOX.UNKNOWN_REPORT"))
        self.assertEqual(json.loads(self.run_cli("retention")[1]), {"removed": []})


if __name__ == "__main__":
    unittest.main()
