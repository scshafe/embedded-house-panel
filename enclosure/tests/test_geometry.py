"""Quarter-turn poses, box tests and bent route lengths."""

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bhp_enclosure.geometry import (Box, GeometryError, Pose, corners, parse_direction,  # noqa: E402
                                    route_length, segment_box)


class PoseTest(unittest.TestCase):
    def test_a_quarter_turn_about_z_takes_x_to_y(self) -> None:
        pose = Pose.from_steps([("z", 90)], (10, 0, 0))
        self.assertEqual(pose.point((1, 0, 0)), (10.0, 1.0, 0.0))
        self.assertEqual(pose.direction(parse_direction("+y")), (-1.0, 0.0, 0.0))

    def test_steps_apply_in_order(self) -> None:
        pose = Pose.from_steps([("z", 90), ("x", 90)], (0, 0, 0))
        # +x -> +y under z90, then +y -> +z under x90.
        self.assertEqual(pose.direction((1, 0, 0)), (0.0, 0.0, 1.0))

    def test_only_quarter_turns(self) -> None:
        with self.assertRaises(GeometryError):
            Pose.from_steps([("z", 45)], (0, 0, 0))
        with self.assertRaises(GeometryError):
            parse_direction("x")

    def test_a_placed_box_stays_axis_aligned(self) -> None:
        box = Box.from_min_size((0, 0, 0), (130, 120, 1.6))
        placed = box.placed(Pose.from_steps([("z", 180)], (0, 0, -30)))
        self.assertEqual(placed.low, (-130.0, -120.0, -30.0))
        self.assertAlmostEqual(placed.high[2], -28.4)


class BoxTest(unittest.TestCase):
    def test_touching_boxes_do_not_intersect(self) -> None:
        a = Box.from_min_size((0, 0, 0), (1, 1, 1))
        b = Box.from_min_size((1, 0, 0), (1, 1, 1))
        self.assertFalse(a.intersects(b))
        self.assertEqual(a.gap(b), 0.0)
        self.assertTrue(a.intersects(Box.from_min_size((0.5, 0.5, 0.5), (1, 1, 1))))

    def test_gap_is_euclidean(self) -> None:
        a = Box.from_min_size((0, 0, 0), (1, 1, 1))
        b = Box.from_min_size((4, 5, 0), (1, 1, 1))
        self.assertAlmostEqual(a.gap(b), 5.0)

    def test_sizes_must_be_positive(self) -> None:
        with self.assertRaises(GeometryError):
            Box.from_min_size((0, 0, 0), (1, 0, 1))


class RouteTest(unittest.TestCase):
    def test_a_right_angle_bend_shortens_the_route(self) -> None:
        points = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (10.0, 10.0, 0.0)]
        self.assertAlmostEqual(route_length(points, 0.0), 20.0)
        # Two tangents of r replaced by a quarter circle.
        self.assertAlmostEqual(route_length(points, 4.0), 20.0 - 8.0 + 2.0 * math.pi)
        (corner,) = corners(points, 4.0)
        self.assertAlmostEqual(corner.tangent, 4.0)
        self.assertAlmostEqual(math.degrees(corner.angle), 90.0)

    def test_a_u_turn_needs_two_bends(self) -> None:
        with self.assertRaises(GeometryError):
            corners([(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 0.0, 0.0)], 1.0)

    def test_segment_box_sweeps_the_section(self) -> None:
        box = segment_box((0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 8.0, 0.1))
        self.assertEqual(box.low, (0.0, -8.0, -0.1))
        self.assertEqual(box.high, (10.0, 8.0, 0.1))


if __name__ == "__main__":
    unittest.main()
