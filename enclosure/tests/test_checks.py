"""The fit check's rules against small hand-made layouts."""

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bhp_enclosure import checks, model  # noqa: E402

BASE = """
schema_version = 1

[printer]
bed = [256.0, 256.0]
material = "PETG"

[clearance]
part = 1.0
cable = 0.5
rf_metal = 10.0
mic_to_speaker = 50.0

[shell]
outer_low = [-100.0, -100.0, -50.0]
outer_high = [100.0, 100.0, 3.0]
wall = 2.0
rear = 3.0
front = 2.0
gasket = 1.0
lip_overlap = 2.0

[[shell.openings]]
id = "bottom_port"
face = "-y"
min = [-10.0, -101.0, -30.0]
size = [20.0, 5.0, 10.0]

[[parts]]
id = "a"
provenance = {{ status = "vendor", source = "test" }}
pose = {{ at = [0.0, 0.0, -20.0] }}
[[parts.shapes]]
id = "pcb"
min = [-40.0, -40.0, 0.0]
size = [80.0, 80.0, 2.0]
[[parts.connectors]]
id = "out"
at = [40.0, 0.0, 1.0]
exit = "+x"
straight = 3.0

[[parts]]
id = "b"
provenance = {{ status = "estimate", source = "test" }}
pose = {{ at = [0.0, 0.0, -40.0] }}
[[parts.shapes]]
id = "pcb"
min = [-40.0, -40.0, 0.0]
size = [80.0, 80.0, 2.0]
[[parts.connectors]]
id = "in"
at = [40.0, 0.0, 1.0]
exit = "+x"
straight = 3.0
{extra}
"""

FLEX = """
[[cables]]
id = "flex"
kind = "fpc"
ends = ["a.out", "b.in"]
section = [10.0, 0.2]
width_axis = "+y"
min_bend_radius = 3.0
provenance = {{ status = "vendor", source = "test" }}
length = {length}
max_excess = 10.0
route = {route}
"""

GOOD_ROUTE = "[[40.0, 0.0, -19.0], [50.0, 0.0, -19.0], [50.0, 0.0, -39.0], [40.0, 0.0, -39.0]]"


def layout(extra: str = "") -> model.Layout:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "params.toml"
        path.write_text(textwrap.dedent(BASE.format(extra=extra)), encoding="utf-8")
        return model.load(path)


def codes(extra: str = "") -> list[str]:
    return [finding.code for finding in checks.run(layout(extra))]


def flex(route: str = GOOD_ROUTE, length: float = 40.0) -> str:
    return FLEX.format(route=route, length=length)


class PartRulesTest(unittest.TestCase):
    def test_the_base_layout_fits(self) -> None:
        self.assertEqual(codes(), [])

    def test_overlapping_parts(self) -> None:
        extra = """
[[parts]]
id = "c"
provenance = { status = "placeholder", source = "test" }
pose = { at = [0.0, 0.0, -19.0] }
[[parts.shapes]]
id = "blob"
min = [0.0, 0.0, 0.0]
size = [5.0, 5.0, 5.0]
"""
        self.assertIn("LAYOUT.OVERLAP", codes(extra))
        self.assertIn("LAYOUT.TOO_CLOSE", codes(extra.replace("-19.0", "-17.5")))
        self.assertEqual(codes(extra.replace("-19.0", "-16.0")), [])
        self.assertEqual(codes(extra + '\n[allow]\ncontact = [["a.pcb", "c"]]\n'), [])

    def test_a_shape_may_leave_the_shell_only_through_its_opening(self) -> None:
        extra = """
[[parts]]
id = "dome"
provenance = { status = "vendor", source = "test" }
pose = { at = [0.0, -99.0, -25.0] }
[[parts.shapes]]
id = "lens"
min = [-5.0, -2.0, -2.0]
size = [10.0, 5.0, 4.0]
"""
        self.assertIn("LAYOUT.OUTSIDE_SHELL", codes(extra))
        self.assertEqual(codes(extra.replace('id = "lens"', 'id = "lens"\nthrough = "bottom_port"')), [])

    def test_keepouts_and_metal_near_the_antenna(self) -> None:
        extra = """
[[parts]]
id = "c"
provenance = { status = "placeholder", source = "test" }
pose = { at = [60.0, 50.0, -30.0] }
[[parts.shapes]]
id = "zone"
kind = "rf_keepout"
min = [0.0, 0.0, 0.0]
size = [5.0, 5.0, 5.0]

[[parts]]
id = "screw"
metal = true
provenance = { status = "design", source = "test" }
pose = { at = [70.0, 50.0, -30.0] }
[[parts.shapes]]
id = "head"
min = [0.0, 0.0, 0.0]
size = [3.0, 3.0, 3.0]
"""
        self.assertIn("RF.METAL_NEAR_ANTENNA", codes(extra))
        self.assertEqual(codes(extra.replace("[70.0, 50.0, -30.0]", "[80.0, 50.0, -30.0]")), [])
        self.assertIn("LAYOUT.KEEPOUT", codes(extra.replace('kind = "rf_keepout"', 'kind = "keepout"')
                                              .replace("[70.0, 50.0, -30.0]", "[62.0, 50.0, -30.0]")))

    def test_an_access_path_must_reach_its_opening(self) -> None:
        extra = """
[[parts]]
id = "sd"
provenance = { status = "vendor", source = "test" }
pose = { at = [0.0, -97.0, -27.0] }
[[parts.shapes]]
id = "slot"
kind = "access"
access = "bottom_port"
min = [-2.0, -2.0, 0.0]
size = [4.0, 4.0, 2.0]
"""
        self.assertEqual(codes(extra), [])
        self.assertIn("ACCESS.NO_OPENING", codes(extra.replace("[0.0, -97.0, -27.0]", "[0.0, -60.0, -27.0]")))

    def test_a_hole_must_be_in_its_part(self) -> None:
        extra = """
[[parts.holes]]
id = "h1"
at = [50.0, 0.0, 1.0]
d = 3.2
"""
        self.assertIn("LAYOUT.HOLE_OUTSIDE_PART", codes(extra))
        self.assertEqual(codes(extra.replace("[50.0, 0.0, 1.0]", "[35.0, 0.0, 1.0]")), [])


class CableRulesTest(unittest.TestCase):
    def test_a_good_flex_route(self) -> None:
        self.assertEqual(codes(flex()), [])

    def test_length(self) -> None:
        self.assertIn("CABLE.TOO_SHORT", codes(flex(length=30.0)))
        self.assertIn("CABLE.EXCESS", codes(flex(length=60.0)))

    def test_ends_and_exit_direction(self) -> None:
        self.assertIn("CABLE.END_MISMATCH", codes(flex(route=GOOD_ROUTE.replace("[40.0, 0.0, -19.0]",
                                                                                 "[41.0, 0.0, -19.0]"))))
        route = "[[40.0, 0.0, -19.0], [40.0, 0.0, -30.0], [40.0, 0.0, -39.0]]"
        self.assertIn("CABLE.EXIT_DIRECTION", codes(flex(route=route)))

    def test_straight_run_and_bend_radius(self) -> None:
        tight = "[[40.0, 0.0, -19.0], [44.0, 0.0, -19.0], [44.0, 0.0, -39.0], [40.0, 0.0, -39.0]]"
        self.assertIn("CABLE.STRAIGHT_RUN", codes(flex(route=tight, length=25.0)))
        crimp = ("[[40.0, 0.0, -19.0], [50.0, 0.0, -19.0], [50.0, 0.0, -23.0], [60.0, 0.0, -23.0], "
                 "[60.0, 0.0, -39.0], [40.0, 0.0, -39.0]]")
        self.assertIn("CABLE.BEND_RADIUS", codes(flex(route=crimp, length=60.0)))

    def test_a_flex_may_not_bend_across_its_width(self) -> None:
        route = ("[[40.0, 0.0, -19.0], [50.0, 0.0, -19.0], [50.0, 10.0, -19.0], [50.0, 10.0, -39.0], "
                 "[50.0, 0.0, -39.0], [40.0, 0.0, -39.0]]")
        self.assertIn("CABLE.FPC_TWIST", codes(flex(route=route, length=60.0)))

    def test_collision_and_leaving_the_shell(self) -> None:
        through = "[[40.0, 0.0, -19.0], [50.0, 0.0, -19.0], [50.0, 0.0, -39.0], [40.0, 0.0, -39.0]]"
        blocker = """
[[parts]]
id = "wall"
provenance = { status = "design", source = "test" }
pose = { at = [48.0, -20.0, -35.0] }
[[parts.shapes]]
id = "rib"
min = [0.0, 0.0, 0.0]
size = [5.0, 40.0, 5.0]
"""
        self.assertIn("CABLE.COLLISION", codes(flex(route=through) + blocker))
        wide = "[[40.0, 0.0, -19.0], [99.0, 0.0, -19.0], [99.0, 0.0, -39.0], [40.0, 0.0, -39.0]]"
        self.assertIn("CABLE.OUTSIDE_SHELL", codes(flex(route=wide, length=136.0)))

    def test_a_cable_to_outside_needs_its_opening(self) -> None:
        power = """
[[cables]]
id = "power"
kind = "round"
ends = ["b.in", "outside"]
section = [4.0, 4.0]
min_bend_radius = 5.0
provenance = { status = "design", source = "test" }
exit_opening = "bottom_port"
route = [[40.0, 0.0, -39.0], [60.0, 0.0, -39.0], [60.0, -80.0, -39.0], [0.0, -80.0, -39.0], [0.0, -80.0, -25.0], [0.0, -110.0, -25.0]]
"""
        self.assertEqual(codes(power), [])
        self.assertIn("CABLE.OUTSIDE_SHELL", codes(power.replace('"bottom_port"', '"bottom_port"\n')
                                                   .replace("[0.0, -80.0, -25.0], [0.0, -110.0, -25.0]",
                                                            "[0.0, -80.0, -10.0], [0.0, -110.0, -10.0]")))

    def test_a_plug_body_may_not_run_into_another_part(self) -> None:
        extra = flex() + """
[[parts]]
id = "c"
provenance = { status = "placeholder", source = "test" }
pose = { at = [41.0, -2.0, -19.5] }
[[parts.shapes]]
id = "blob"
min = [0.0, 0.0, 0.0]
size = [2.0, 4.0, 0.5]
"""
        self.assertNotIn("CABLE.PLUG_BLOCKED", codes(extra))
        plugged = BASE.replace('exit = "+x"\nstraight = 3.0\n\n[[parts]]\nid = "b"',
                               'exit = "+x"\nstraight = 3.0\nplug = {{ min = [40.0, -3.0, 0.0], size = [4.0, 6.0, 2.0] }}'
                               '\n\n[[parts]]\nid = "b"')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "params.toml"
            path.write_text(textwrap.dedent(plugged.format(extra=extra)), encoding="utf-8")
            found = [finding.code for finding in checks.run(model.load(path))]
        self.assertIn("CABLE.PLUG_BLOCKED", found)


class ModelErrorsTest(unittest.TestCase):
    def test_every_item_needs_a_known_status_and_a_source(self) -> None:
        with self.assertRaises(model.ModelError) as caught:
            layout(flex().replace('status = "vendor", source = "test"', 'status = "guess", source = "test"'))
        self.assertEqual(caught.exception.code, "PARAMS.BAD_STATUS")

    def test_a_cable_must_name_real_connectors(self) -> None:
        with self.assertRaises(model.ModelError) as caught:
            layout(flex().replace('"b.in"', '"b.nope"'))
        self.assertEqual(caught.exception.code, "PARAMS.UNKNOWN_CONNECTOR")


if __name__ == "__main__":
    unittest.main()
