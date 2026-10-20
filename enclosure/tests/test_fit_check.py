"""The real layout passes, and the generated blocks are replaced in place."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "enclosure"))

import fit_check  # noqa: E402
from bhp_enclosure import checks, drawings, model  # noqa: E402


class RealLayoutTest(unittest.TestCase):
    def test_params_fit(self) -> None:
        layout = model.load(fit_check.PARAMS)
        self.assertEqual([str(finding) for finding in checks.run(layout)], [])

    def test_every_cable_is_in_the_harness_table(self) -> None:
        layout = model.load(fit_check.PARAMS)
        table = drawings.harness_table(layout)
        for cable in layout.cables:
            self.assertIn(f"`{cable.id}`", table)

    def test_drawings_are_deterministic(self) -> None:
        layout = model.load(fit_check.PARAMS)
        self.assertEqual(drawings.drawings(layout), drawings.drawings(model.load(fit_check.PARAMS)))


class RenderDocumentTest(unittest.TestCase):
    def test_blocks_are_replaced_and_the_rest_kept(self) -> None:
        text = "intro\n<!-- generated:harness -->\nold\n<!-- /generated:harness -->\noutro\n"
        rendered = fit_check.render_document(text, {"harness": "new\n"})
        self.assertEqual(rendered, "intro\n<!-- generated:harness -->\nnew\n<!-- /generated:harness -->\noutro\n")

    def test_a_missing_block_fails(self) -> None:
        with self.assertRaises(SystemExit):
            fit_check.render_document("no blocks\n", {"harness": "x\n"})


if __name__ == "__main__":
    unittest.main()
