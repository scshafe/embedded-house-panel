"""The printed parts against the layout (needs the CAD environment: make enclosure-venv)."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "enclosure"), str(ROOT / "enclosure/cad")]

import build_enclosure  # noqa: E402
import parts  # noqa: E402
from bhp_enclosure import model  # noqa: E402

LAYOUT = model.load(ROOT / "enclosure/params.toml")
BUILT = parts.build(LAYOUT) + parts.templates(LAYOUT)


def codes(built: list) -> list[str]:
    return [finding.code for finding in build_enclosure.solid_check(LAYOUT, built)]


class PrintedPartsTest(unittest.TestCase):
    def test_the_parts_pass_the_solid_check(self) -> None:
        self.assertEqual([str(f) for f in build_enclosure.solid_check(LAYOUT, BUILT)], [])

    def test_every_part_is_one_solid_on_the_bed(self) -> None:
        bed = LAYOUT.printer.bed[0] - 2 * LAYOUT.printer.margin
        for item in BUILT:
            if item.kind == "hardware":
                continue
            with self.subTest(part=item.id):
                self.assertEqual(len(item.solid.solids()), 1)
                bounds = build_enclosure.oriented(item).bounding_box()
                self.assertLessEqual(max(bounds.size.X, bounds.size.Y), bed)
                self.assertAlmostEqual(bounds.min.Z, 0.0, places=6)

    def test_the_shell_has_its_openings(self) -> None:
        shell = next(item for item in BUILT if item.id == "front_shell")
        for opening in LAYOUT.shell.openings:
            with self.subTest(opening=opening.id):
                probe = parts.cylinder_z(*opening.box.center[:2], -0.1, 0.1, 0.5) if opening.face == "+z" \
                    else parts.box_solid(opening.box)
                inside = build_enclosure._overlap_volume(shell.solid, probe)
                self.assertLess(inside, probe.volume, f"{opening.id} is not cut")

    def test_a_part_in_the_way_is_found(self) -> None:
        intruder = parts.Printed("intruder", "test", parts.block((-10, -10, -5), (10, 10, -1)), [])
        self.assertIn("CAD.PART_COLLISION", codes([intruder]))

    def test_a_cable_in_the_way_is_found(self) -> None:
        # Across the DSI flex's U-turn at the right wall.
        intruder = parts.Printed("intruder", "test", parts.block((90, 0, -18), (96, 6, -12)), [])
        self.assertIn("CAD.CABLE_COLLISION", codes([intruder]))

    def test_metal_near_the_antenna_must_be_nylon(self) -> None:
        zone = next(shape.world for shape in LAYOUT.shapes() if shape.kind == "rf_keepout")
        steel = parts.Printed("bracket", "test", parts.block((-99, 70, -46), (-98, 71, -45)), [],
                              fasteners=[parts.Fastener("screw", zone.center, "screw_m3", "steel")])
        self.assertIn("CAD.METAL_NEAR_ANTENNA", codes([steel]))
        steel.fasteners[0] = parts.Fastener("screw", zone.center, "screw_m3", "nylon")
        self.assertNotIn("CAD.METAL_NEAR_ANTENNA", codes([steel]))

    def test_too_big_for_the_bed(self) -> None:
        big = parts.Printed("big", "test", parts.block((-150, 200, -60), (150, 210, -59)), [])
        self.assertIn("CAD.BED_FIT", codes([big]))


if __name__ == "__main__":
    unittest.main()
