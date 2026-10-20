"""The fit check: does everything in ``params.toml`` fit, and can every cable reach?

Each rule returns findings with a stable code. An empty list means the layout
fits as described. It says nothing about whether the described dimensions are
right; that is the caliper list's job.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from itertools import combinations

from .geometry import EPSILON, Box, Vector, corners, dot, norm, route_length, segment_box, sub, unit
from .model import Cable, Layout, Shape

#: Directions closer than this (as a dot product) count as the same.
ALIGNED = 1.0 - 1e-9


@dataclass(frozen=True)
class Finding:
    code: str
    subject: str
    detail: str

    def __str__(self) -> str:
        return f"{self.code} {self.subject}: {self.detail}"


def _allowed(layout: Layout, a: Shape, b: Shape) -> bool:
    keys = {frozenset((a.key, b.key)), frozenset((a.part, b.part)),
            frozenset((a.key, b.part)), frozenset((a.part, b.key))}
    return any(key in layout.allowed_pairs for key in keys)


def check_parts(layout: Layout) -> list[Finding]:
    findings = []
    solids = [shape for shape in layout.shapes() if shape.kind == "solid"]
    for a, b in combinations(solids, 2):
        if a.part == b.part or _allowed(layout, a, b):
            continue
        if a.world.intersects(b.world):
            depth = min(a.world.overlap(b.world))
            findings.append(Finding("LAYOUT.OVERLAP", f"{a.key} x {b.key}", f"intersect by {depth:.2f} mm"))
        elif a.world.gap(b.world) < layout.clearances.part - EPSILON:
            findings.append(Finding("LAYOUT.TOO_CLOSE", f"{a.key} x {b.key}",
                                    f"{a.world.gap(b.world):.2f} mm apart, clearance is "
                                    f"{layout.clearances.part:.2f} mm"))
    return findings


def check_shell(layout: Layout) -> list[Finding]:
    findings = []
    interior = layout.shell.interior
    for shape in layout.shapes():
        if shape.kind == "access":
            opening = layout.opening(shape.access) if shape.access else None
            if opening is None or not shape.world.intersects(opening.box, tolerance=-EPSILON):
                findings.append(Finding("ACCESS.NO_OPENING", shape.key,
                                        f"its access path does not reach opening {shape.access!r}"))
            continue
        if shape.kind != "solid" or interior.contains(shape.world):
            continue
        if shape.through is None:
            findings.append(Finding("LAYOUT.OUTSIDE_SHELL", shape.key,
                                    "leaves the shell's interior and names no opening"))
            continue
        opening = layout.opening(shape.through)
        if not (shape.world.intersects(opening.box) and interior.union(opening.box).contains(shape.world)):
            findings.append(Finding("LAYOUT.OUTSIDE_SHELL", shape.key,
                                    f"leaves the interior outside opening {opening.id!r}"))
    return findings


def check_keepouts(layout: Layout) -> list[Finding]:
    findings = []
    shapes = layout.shapes()
    keepouts = [shape for shape in shapes if shape.kind in ("keepout", "access")]
    solids = [shape for shape in shapes if shape.kind == "solid"]
    for zone in keepouts:
        for solid in solids:
            if solid.part != zone.part and not _allowed(layout, zone, solid) \
                    and zone.world.intersects(solid.world):
                findings.append(Finding("LAYOUT.KEEPOUT", f"{zone.key} x {solid.key}",
                                        f"{solid.key} sits in the {zone.kind} zone"))
        for cable in layout.cables:
            for index, box in enumerate(cable_boxes(cable)):
                if zone.world.intersects(box):
                    findings.append(Finding("LAYOUT.KEEPOUT", f"{zone.key} x cable {cable.id}",
                                            f"segment {index + 1} crosses the {zone.kind} zone"))
    for zone in (shape for shape in shapes if shape.kind == "rf_keepout"):
        grown = zone.world.grown(layout.clearances.rf_metal)
        for solid in solids:
            if solid.metal and solid.part != zone.part and grown.intersects(solid.world, tolerance=-EPSILON):
                findings.append(Finding("RF.METAL_NEAR_ANTENNA", f"{zone.key} x {solid.key}",
                                        f"metal within {layout.clearances.rf_metal:.1f} mm of the antenna"))
    return findings


def cable_half_widths(cable: Cable, direction: Vector) -> Vector:
    """Half extents of the cable's section around a segment running along ``direction``."""
    half = [0.0, 0.0, 0.0]
    axis = [abs(component) for component in direction]
    aligned = max(axis) > ALIGNED
    for i in range(3):
        if aligned and axis[i] > ALIGNED:
            continue
        if cable.width_axis is not None and aligned:
            half[i] = cable.width / 2.0 if abs(cable.width_axis[i]) > ALIGNED else cable.thickness / 2.0
        else:
            half[i] = max(cable.width, cable.thickness) / 2.0
    return (half[0], half[1], half[2])


def cable_boxes(cable: Cable) -> list[Box]:
    return [segment_box(a, b, cable_half_widths(cable, unit(sub(b, a))))
            for a, b in zip(cable.points, cable.points[1:])]


def _end_findings(layout: Layout, cable: Cable, end: str, point: Vector, segment: Vector,
                  first_tangent: float, segment_length: float, label: str) -> list[Finding]:
    findings = []
    if end == "outside":
        if layout.shell.outer.contains(Box(point, point)):
            findings.append(Finding("CABLE.DOES_NOT_LEAVE", cable.id, f"its {label} end is inside the shell"))
        return findings
    connector = layout.connector(end)
    if norm(sub(point, connector.at)) > 0.01:
        findings.append(Finding("CABLE.END_MISMATCH", cable.id,
                                f"its {label} point is not {end} at {list(connector.at)}"))
        return findings
    if dot(unit(segment), connector.direction) < ALIGNED:
        findings.append(Finding("CABLE.EXIT_DIRECTION", cable.id, f"does not leave {end} straight out"))
    elif segment_length + EPSILON < connector.straight + first_tangent:
        findings.append(Finding("CABLE.STRAIGHT_RUN", cable.id,
                                f"needs {connector.straight + first_tangent:.1f} mm straight from {end}, "
                                f"has {segment_length:.1f}"))
    return findings


def check_cable(layout: Layout, cable: Cable) -> list[Finding]:
    findings: list[Finding] = []
    points = cable.points
    try:
        bends = corners(points, cable.min_bend_radius)
    except ValueError as error:
        return [Finding("CABLE.BAD_ROUTE", cable.id, str(error))]
    tangents = [0.0] + [corner.tangent for corner in bends] + [0.0]
    lengths = [norm(sub(b, a)) for a, b in zip(points, points[1:])]
    findings += _end_findings(layout, cable, cable.ends[0], points[0], sub(points[1], points[0]),
                              tangents[1], lengths[0], "first")
    findings += _end_findings(layout, cable, cable.ends[1], points[-1], sub(points[-2], points[-1]),
                              tangents[-2], lengths[-1], "last")
    for index, length in enumerate(lengths):
        if index in (0, len(lengths) - 1):
            continue
        if tangents[index] + tangents[index + 1] > length + EPSILON:
            findings.append(Finding("CABLE.BEND_RADIUS", cable.id,
                                    f"segment {index + 1} is {length:.1f} mm; its bends at "
                                    f"r={cable.min_bend_radius:.1f} need "
                                    f"{tangents[index] + tangents[index + 1]:.1f}"))
    if cable.kind == "fpc" and cable.width_axis is not None:
        for index, (a, b) in enumerate(zip(points, points[1:])):
            if abs(dot(unit(sub(b, a)), cable.width_axis)) > EPSILON:
                findings.append(Finding("CABLE.FPC_TWIST", cable.id,
                                        f"segment {index + 1} runs across the cable's width"))
    if cable.length is not None:
        needed = route_length(points, cable.min_bend_radius)
        if needed + cable.min_slack > cable.length.value + EPSILON:
            findings.append(Finding("CABLE.TOO_SHORT", cable.id,
                                    f"route needs {needed:.1f} + {cable.min_slack:.1f} mm slack; "
                                    f"cable is {cable.length.value:.1f}"))
        elif cable.length.value - needed > cable.max_excess + EPSILON:
            findings.append(Finding("CABLE.EXCESS", cable.id,
                                    f"{cable.length.value - needed:.1f} mm left over; route allows "
                                    f"{cable.max_excess:.1f}"))
    end_parts = [None if end == "outside" else layout.connector(end).part for end in cable.ends]
    boxes = cable_boxes(cable)
    solids = [shape for shape in layout.shapes() if shape.kind == "solid"]
    for index, box in enumerate(boxes):
        for solid in solids:
            if (index == 0 and solid.part == end_parts[0]) or \
                    (index == len(boxes) - 1 and solid.part == end_parts[1]):
                continue
            if box.intersects(solid.world):
                findings.append(Finding("CABLE.COLLISION", f"{cable.id} x {solid.key}",
                                        f"segment {index + 1} runs through it"))
    interior = layout.shell.interior
    for index, box in enumerate(boxes):
        if interior.contains(box):
            continue
        opening = layout.opening(cable.exit_opening) if cable.exit_opening else None
        if opening is None or not box.intersects(opening.box, tolerance=-EPSILON):
            findings.append(Finding("CABLE.OUTSIDE_SHELL", cable.id,
                                    f"segment {index + 1} leaves the shell outside an opening"))
    for end in cable.ends:
        if end == "outside":
            continue
        plug = layout.connector(end).plug
        if plug is None:
            continue
        for solid in solids:
            if solid.part != layout.connector(end).part and plug.intersects(solid.world):
                findings.append(Finding("CABLE.PLUG_BLOCKED", f"{end} x {solid.key}",
                                        "the mated plug runs into it"))
    return findings


def check_acoustics(layout: Layout, mic_part: str, speaker_shape: str) -> list[Finding]:
    findings = []
    speaker_part, _, shape_id = speaker_shape.partition(".")
    speaker = next(shape for shape in layout.part(speaker_part).shapes if shape.id == shape_id)
    for point in layout.part(mic_part).points:
        distance = speaker.world.gap(Box(point.at, point.at))
        if distance < layout.clearances.mic_to_speaker - EPSILON:
            findings.append(Finding("ACOUSTIC.MIC_NEAR_SPEAKER", f"{point.part}.{point.id}",
                                    f"{distance:.1f} mm from {speaker_shape}; want "
                                    f"{layout.clearances.mic_to_speaker:.1f}"))
    return findings


def check_holes(layout: Layout) -> list[Finding]:
    """Every mounting hole lies inside one of its own part's solids (normally the PCB)."""
    findings = []
    for part in layout.parts:
        for hole in part.holes:
            probe = Box(hole.at, hole.at)
            if not any(shape.kind == "solid" and shape.world.contains(probe) for shape in part.shapes):
                findings.append(Finding("LAYOUT.HOLE_OUTSIDE_PART", f"{part.id}.{hole.id}",
                                        "is not inside any of its part's solids"))
    return findings


def run(layout: Layout) -> list[Finding]:
    findings = check_parts(layout) + check_shell(layout) + check_keepouts(layout) + check_holes(layout)
    for cable in layout.cables:
        findings += check_cable(layout, cable)
    if layout.acoustic is not None:
        findings += check_acoustics(layout, *layout.acoustic)
    return findings


def bend_summary(cable: Cable) -> list[tuple[int, float]]:
    """Each corner's index and turn in degrees, for the harness table."""
    return [(corner.index, math.degrees(corner.angle)) for corner in corners(cable.points, cable.min_bend_radius)]
