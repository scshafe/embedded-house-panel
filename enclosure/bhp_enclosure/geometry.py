"""Axis-aligned geometry for the fit check: boxes, quarter-turn poses, routes.

Every part in the layout is a set of boxes in its own frame, placed in the
unit frame by a pose made of quarter turns and a translation, so a placed box
stays axis-aligned and every overlap test is exact. Cable routes are
polylines whose corners are bent at a radius; a corner's bend needs a
tangent length of ``r * tan(angle / 2)`` on both of its segments.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable, Sequence

Vector = tuple[float, float, float]
Matrix = tuple[Vector, Vector, Vector]

AXES = {"x": 0, "y": 1, "z": 2}
IDENTITY: Matrix = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
#: Geometry is in millimetres; anything closer than this is touching.
EPSILON = 1e-6


class GeometryError(ValueError):
    """A shape or pose that the fit check cannot represent."""


def add(a: Sequence[float], b: Sequence[float]) -> Vector:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Sequence[float], b: Sequence[float]) -> Vector:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a: Sequence[float], k: float) -> Vector:
    return (a[0] * k, a[1] * k, a[2] * k)


def dot(a: Sequence[float], b: Sequence[float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a: Sequence[float]) -> float:
    return math.sqrt(dot(a, a))


def unit(a: Sequence[float]) -> Vector:
    length = norm(a)
    if length < EPSILON:
        raise GeometryError("zero-length direction")
    return scale(a, 1.0 / length)


def parse_direction(text: str) -> Vector:
    """``"+x"``, ``"-z"`` and so on, as a unit vector."""
    if len(text) != 2 or text[0] not in "+-" or text[1] not in AXES:
        raise GeometryError(f"direction must be one of +x -x +y -y +z -z, not {text!r}")
    vector = [0.0, 0.0, 0.0]
    vector[AXES[text[1]]] = 1.0 if text[0] == "+" else -1.0
    return (vector[0], vector[1], vector[2])


def quarter_turn(axis: str, degrees: int) -> Matrix:
    """The rotation by a multiple of 90 degrees about ``axis`` (right-handed)."""
    if axis not in AXES:
        raise GeometryError(f"rotation axis must be x, y or z, not {axis!r}")
    if degrees % 90:
        raise GeometryError(f"rotations are quarter turns, not {degrees} degrees")
    turns = (degrees // 90) % 4
    cos = (1, 0, -1, 0)[turns]
    sin = (0, 1, 0, -1)[turns]
    if axis == "x":
        rows = ((1, 0, 0), (0, cos, -sin), (0, sin, cos))
    elif axis == "y":
        rows = ((cos, 0, sin), (0, 1, 0), (-sin, 0, cos))
    else:
        rows = ((cos, -sin, 0), (sin, cos, 0), (0, 0, 1))
    return tuple(tuple(float(value) for value in row) for row in rows)  # type: ignore[return-value]


def multiply(a: Matrix, b: Matrix) -> Matrix:
    return tuple(  # type: ignore[return-value]
        tuple(sum(a[row][k] * b[k][column] for k in range(3)) for column in range(3))
        for row in range(3))


def apply(matrix: Matrix, vector: Sequence[float]) -> Vector:
    return (dot(matrix[0], vector), dot(matrix[1], vector), dot(matrix[2], vector))


@dataclass(frozen=True)
class Pose:
    """Rotate (quarter turns, applied in order), then translate."""

    rotation: Matrix = IDENTITY
    translation: Vector = (0.0, 0.0, 0.0)

    @classmethod
    def from_steps(cls, steps: Iterable[tuple[str, int]], translation: Sequence[float]) -> "Pose":
        rotation = IDENTITY
        for axis, degrees in steps:
            rotation = multiply(quarter_turn(axis, degrees), rotation)
        return cls(rotation, (float(translation[0]), float(translation[1]), float(translation[2])))

    def point(self, local: Sequence[float]) -> Vector:
        return add(apply(self.rotation, local), self.translation)

    def direction(self, local: Sequence[float]) -> Vector:
        return apply(self.rotation, local)

    def then(self, outer: "Pose") -> "Pose":
        """This pose followed by ``outer`` (a part placed on a carrier)."""
        return Pose(multiply(outer.rotation, self.rotation), outer.point(self.translation))


@dataclass(frozen=True)
class Box:
    low: Vector
    high: Vector

    @classmethod
    def from_min_size(cls, low: Sequence[float], size: Sequence[float]) -> "Box":
        if any(value <= 0 for value in size):
            raise GeometryError(f"box size must be positive, not {list(size)}")
        return cls((float(low[0]), float(low[1]), float(low[2])),
                   (low[0] + size[0], low[1] + size[1], low[2] + size[2]))

    @property
    def size(self) -> Vector:
        return sub(self.high, self.low)

    @property
    def center(self) -> Vector:
        return scale(add(self.low, self.high), 0.5)

    def placed(self, pose: Pose) -> "Box":
        a, b = pose.point(self.low), pose.point(self.high)
        return Box((min(a[0], b[0]), min(a[1], b[1]), min(a[2], b[2])),
                   (max(a[0], b[0]), max(a[1], b[1]), max(a[2], b[2])))

    def grown(self, margin: float) -> "Box":
        return Box(sub(self.low, (margin,) * 3), add(self.high, (margin,) * 3))

    def overlap(self, other: "Box") -> Vector:
        """Penetration depth along each axis; all three positive means they intersect."""
        return tuple(min(self.high[i], other.high[i]) - max(self.low[i], other.low[i])  # type: ignore[return-value]
                     for i in range(3))

    def intersects(self, other: "Box", tolerance: float = EPSILON) -> bool:
        return all(depth > tolerance for depth in self.overlap(other))

    def contains(self, other: "Box", tolerance: float = EPSILON) -> bool:
        return all(self.low[i] - tolerance <= other.low[i] and other.high[i] <= self.high[i] + tolerance
                   for i in range(3))

    def gap(self, other: "Box") -> float:
        """Euclidean clearance between the two boxes (0 when they touch or overlap)."""
        separation = [max(0.0, -depth) for depth in self.overlap(other)]
        return math.sqrt(sum(value * value for value in separation))

    def union(self, other: "Box") -> "Box":
        return Box(tuple(min(self.low[i], other.low[i]) for i in range(3)),  # type: ignore[arg-type]
                   tuple(max(self.high[i], other.high[i]) for i in range(3)))  # type: ignore[arg-type]


@dataclass(frozen=True)
class Corner:
    index: int
    angle: float  # radians the route turns through here
    tangent: float  # length of each adjacent segment the bend uses


def corners(points: Sequence[Vector], radius: float) -> list[Corner]:
    """The bends at a polyline's interior points when each is bent at ``radius``."""
    result = []
    for index in range(1, len(points) - 1):
        incoming = unit(sub(points[index], points[index - 1]))
        outgoing = unit(sub(points[index + 1], points[index]))
        cosine = max(-1.0, min(1.0, dot(incoming, outgoing)))
        angle = math.acos(cosine)
        if angle > math.pi - 1e-9:
            raise GeometryError(f"route reverses on itself at point {index}; use two bends")
        result.append(Corner(index, angle, radius * math.tan(angle / 2.0)))
    return result


def route_length(points: Sequence[Vector], radius: float) -> float:
    """Length along the centre line with every corner bent at ``radius``."""
    straight = sum(norm(sub(b, a)) for a, b in zip(points, points[1:]))
    return straight - sum(2.0 * corner.tangent - radius * corner.angle
                          for corner in corners(points, radius))


def segment_box(a: Vector, b: Vector, half_widths: Vector) -> Box:
    """The box swept by a cable section of the given half extents along a straight segment."""
    low = tuple(min(a[i], b[i]) - half_widths[i] for i in range(3))
    high = tuple(max(a[i], b[i]) + half_widths[i] for i in range(3))
    return Box(low, high)  # type: ignore[arg-type]
