"""The mirror unit's printed parts, built from ``params.toml`` with build123d.

Every part is built in the unit frame of ``params.toml``, where it sits in
the assembled unit, so the solid check can test it against the layout's part
boxes and cable sweeps. Each part also names the turn that puts it on the bed.

The parts:
- ``front_shell``: the bezel, the side walls and every opening, mount and
  boss. It prints face-down, and every internal feature grows from the front
  plate or leans on a wall at 45 degrees or less.
- ``rear_plate``: the back, with the FRDM board's posts and the mirror cleat's
  hook.
- ``mount_plate``: VHB-bonded to the mirror, with the cleat rail.
- ``panel_clamp_left`` and ``panel_clamp_right``: they pull the panel onto
  the bezel's gasket, from its M3 standoffs to wedge bosses on the walls.
- ``cable_clamp``: grips the USB-C power cable at the bottom wall.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Iterable, Sequence

import build123d as bd

from bhp_enclosure.geometry import Box, Vector
from bhp_enclosure.model import Layout

#: Heat-set insert for M3 (hole diameter, depth), M3 clearance, M2 pilot.
INSERT_M3 = (4.0, 5.0)
CLEAR_M3 = 3.4
PILOT_M2 = 1.6
#: Printed-fit clearance between mating printed parts.
FIT = 0.4
#: Cleat geometry: the 45-degree rail and hook, their y band.
CLEAT_Y = (40.0, 58.0)
MOUNT_THICKNESS = 3.0
CLEAT_DEPTH = 4.5
#: Thickness of the panel clamps and how far behind the standoff faces they sit.
CLAMP_THICKNESS = 2.8
CLAMP_FACE = -10.5
#: The wall wedges under the clamps: 9.5 mm wide and as tall, so they end at
#: z = -1.0, behind the glass; the insert sits 4 mm from the wall.
WEDGE_WIDTH = 9.5
WEDGE_INSERT_INSET = 4.0
#: The rim bosses' wedges: 10 mm wide and as tall, the insert 4 mm from the wall.
RIM_WEDGE_WIDTH = 10.0
#: How far the power-cable clamp's boss stands off the bottom wall.
CABLE_BOSS_DEPTH = 9.0
#: The FRDM board's component face (its posts end there).
FRDM_COMPONENT_FACE = -20.0


@dataclass(frozen=True)
class Fastener:
    """A metal (or nylon) part the solid check keeps away from the antenna."""

    id: str
    at: Vector
    kind: str
    material: str


@dataclass
class Printed:
    id: str
    name: str
    solid: bd.Part
    #: Quarter turns that put the part on the bed, applied in order.
    print_turns: Sequence[tuple[str, int]]
    #: Layout parts or shape keys ("part.shape") this part touches by design.
    touches: set[str] = field(default_factory=set)
    fasteners: list[Fastener] = field(default_factory=list)
    #: "printed", "template" (a flat check print, not in the unit) or
    #: "hardware" (bought parts, checked for fit but not exported).
    kind: str = "printed"
    #: How it goes on the bed, for the print page.
    orientation: str = ""


def block(low: Sequence[float], high: Sequence[float]) -> bd.Part:
    size = [high[i] - low[i] for i in range(3)]
    if any(value <= 0 for value in size):
        raise ValueError(f"empty block {list(low)} .. {list(high)}")
    center = [(low[i] + high[i]) / 2.0 for i in range(3)]
    return bd.Pos(*center) * bd.Box(*size)


def box_solid(box: Box) -> bd.Part:
    return block(box.low, box.high)


def cylinder_z(x: float, y: float, z0: float, z1: float, diameter: float) -> bd.Part:
    """A round post or hole along z between z0 and z1."""
    low, high = min(z0, z1), max(z0, z1)
    return bd.Pos(x, y, (low + high) / 2.0) * bd.Cylinder(diameter / 2.0, high - low)


def cylinder_y(x: float, z: float, y0: float, y1: float, diameter: float) -> bd.Part:
    low, high = min(y0, y1), max(y0, y1)
    return bd.Pos(x, (low + high) / 2.0, z) * bd.Rot(90, 0, 0) * bd.Cylinder(diameter / 2.0, high - low)


def prism_x(points: Iterable[tuple[float, float]], x0: float, x1: float) -> bd.Part:
    """A prism along x from a (y, z) polygon."""
    face = bd.Plane.YZ.offset(min(x0, x1)) * bd.Polygon(*points, align=None)
    return bd.extrude(face, amount=abs(x1 - x0), dir=(1, 0, 0))


def prism_y(points: Iterable[tuple[float, float]], y0: float, y1: float) -> bd.Part:
    """A prism along y from an (x, z) polygon."""
    plane = bd.Plane(origin=(0, min(y0, y1), 0), x_dir=(1, 0, 0), z_dir=(0, 1, 0))
    # This plane's local y runs along -z, so the polygon's z is negated.
    face = plane * bd.Polygon(*[(x, -z) for x, z in points], align=None)
    return bd.extrude(face, amount=abs(y1 - y0), dir=(0, 1, 0))


def union(parts: Iterable[bd.Part]) -> bd.Part:
    result = None
    for part in parts:
        result = part if result is None else result + part
    if result is None:
        raise ValueError("nothing to join")
    return result


def subtract(base: bd.Part, cuts: Iterable[bd.Part]) -> bd.Part:
    for cut in cuts:
        base = base - cut
    return base


def _hole(layout: Layout, part_id: str, hole_id: str) -> Vector:
    for hole in layout.part(part_id).holes:
        if hole.id == hole_id:
            return hole.at
    raise KeyError(f"{part_id}.{hole_id}")


def _shape(layout: Layout, key: str) -> Box:
    part_id, _, shape_id = key.partition(".")
    for shape in layout.part(part_id).shapes:
        if shape.id == shape_id:
            return shape.world
    raise KeyError(key)


# ---------------------------------------------------------------------------
# Front shell.

def rim_boss_positions(layout: Layout) -> list[tuple[float, float]]:
    """Where the rear plate's screws go: the four corners and the middle of each side."""
    inner = layout.shell.interior
    inset = 4.0
    x0, x1 = inner.low[0] + inset, inner.high[0] - inset
    y0, y1 = inner.low[1] + inset, inner.high[1] - inset
    return [(x0, y1), (x1, y1), (x0, y0), (x1, y0), (x0, -32.0), (x1, -32.0)]


def panel_clamp_boss_positions(layout: Layout) -> list[tuple[float, float]]:
    """Wall bosses for the panel clamps, level with the panel's M3 standoffs."""
    inner = layout.shell.interior
    xs = (inner.low[0] + WEDGE_INSERT_INSET, inner.high[0] - WEDGE_INSERT_INSET)
    ys = (_hole(layout, "panel", "m3_tl")[1], _hole(layout, "panel", "m3_bl")[1])
    return [(x, y) for x in xs for y in ys]


def _front_openings(layout: Layout) -> list[bd.Part]:
    cuts = []
    for opening in layout.shell.openings:
        box = opening.box
        if opening.id.startswith("mic") and opening.id[3:].isdigit():
            cx, cy = box.center[0], box.center[1]
            cuts.append(cylinder_z(cx, cy, -1.0, box.high[2] + 1.0, 1.2))
        elif opening.id == "pir_window":
            cuts.append(cylinder_z(box.center[0], box.center[1], -2.0, 5.0, 24.0))
        elif opening.id == "led_window":
            cuts.append(cylinder_z(box.center[0], box.center[1], -9.0, 5.0, 5.2))
        elif opening.id == "speaker_grille":
            # Slots 1.6 mm wide on a 3.2 mm pitch across the speaker's face.
            pitch, width = 3.2, 1.6
            count = int((box.size[0] - width) // pitch) + 1
            start = box.center[0] - (count - 1) * pitch / 2.0
            for index in range(count):
                x = start + index * pitch
                cuts.append(block((x - width / 2, box.low[1] + 2.0, -1.0), (x + width / 2, box.high[1] - 2.0, 5.0)))
        elif opening.id.startswith("vent"):
            pitch, width = 4.0, 2.0
            count = int((box.size[0] - width) // pitch) + 1
            start = box.center[0] - (count - 1) * pitch / 2.0
            for index in range(count):
                x = start + index * pitch
                cuts.append(block((x - width / 2, box.low[1] - 1.0, box.low[2]),
                                  (x + width / 2, box.high[1] + 1.0, box.high[2])))
        else:
            cuts.append(box_solid(opening.box))
    return cuts


def front_shell(layout: Layout) -> Printed:
    shell = layout.shell
    outer, inner = shell.outer, shell.interior
    glass = _shape(layout, "panel.glass")
    body = box_solid(outer) - block((inner.low[0], inner.low[1], outer.low[2] - 1.0),
                                    (inner.high[0], inner.high[1], 0.0))
    # Gasket pocket over the glass border, and the window inside the lip.
    pocket = block((glass.low[0] - 0.5, glass.low[1] - 0.5, -0.01), (glass.high[0] + 0.5, glass.high[1] + 0.5,
                                                                     shell.gasket))
    window = block((glass.low[0] + shell.lip_overlap, glass.low[1] + shell.lip_overlap, -1.0),
                   (glass.high[0] - shell.lip_overlap, glass.high[1] - shell.lip_overlap, outer.high[2] + 1.0))
    body = body - pocket - window
    fasteners: list[Fastener] = []
    features: list[bd.Part] = []
    cuts: list[bd.Part] = []
    rear_face = inner.low[2]
    # Rim bosses: columns in the corners and on the sides, 10 mm deep, with an
    # insert from the back for each rear-plate screw.
    for index, (x, y) in enumerate(rim_boss_positions(layout)):
        x_wall = inner.low[0] if x < 0 else inner.high[0]
        y_low, y_high = y - 4.0, y + 4.0
        if index < 2:
            y_high = inner.high[1]
        elif index < 4:
            y_low = inner.low[1]
        # A 45-degree wedge on the side wall: it prints face-down without support.
        tip = x_wall - math.copysign(RIM_WEDGE_WIDTH, x_wall)
        features.append(prism_y([(x_wall, rear_face), (tip, rear_face), (x_wall, rear_face + RIM_WEDGE_WIDTH)],
                                y_low, y_high))
        cuts.append(cylinder_z(x, y, rear_face - 1.0, rear_face + INSERT_M3[1], INSERT_M3[0]))
        fasteners.append(Fastener(f"rim_insert_{index}", (x, y, rear_face + 2.5), "insert_m3", "brass"))
    # Panel-clamp bosses: 45-degree wedges on the side walls, behind the glass.
    for index, (x, y) in enumerate(panel_clamp_boss_positions(layout)):
        x_wall = inner.low[0] if x < 0 else inner.high[0]
        # A right triangle in (x, z): its back face carries the clamp, its
        # 45-degree face prints without support and stays behind the glass.
        tip = x_wall - math.copysign(WEDGE_WIDTH, x_wall)
        features.append(prism_y([(x_wall, CLAMP_FACE), (tip, CLAMP_FACE), (x_wall, CLAMP_FACE + WEDGE_WIDTH)],
                                y - 5.0, y + 5.0))
        cuts.append(cylinder_z(x, y, CLAMP_FACE - 1.0, CLAMP_FACE + INSERT_M3[1], INSERT_M3[0]))
        fasteners.append(Fastener(f"clamp_insert_{index}", (x, y, CLAMP_FACE + 2.5), "insert_m3", "brass"))
    # Mic strip: two short posts through the gasket gap, M3 inserts from behind.
    for hole_id in ("m3_left", "m3_right"):
        x, y, _ = _hole(layout, "mic_strip", hole_id)
        strip_face = _shape(layout, "mic_strip.pcb").high[2]
        features.append(cylinder_z(x, y, strip_face, 0.0, 6.0))
        cuts.append(cylinder_z(x, y, strip_face - 1.0, strip_face + 3.0, INSERT_M3[0]))
        fasteners.append(Fastener(f"strip_insert_{hole_id}", (x, y, strip_face + 1.5), "insert_m3", "brass"))
        fasteners.append(Fastener(f"strip_screw_{hole_id}", (x, y, strip_face - 4.0), "screw_m3", "steel"))
    # XVF3800 core board: four posts from the front plate to its component face.
    core_face = _shape(layout, "xvf_core.pcb").high[2]
    glass_low_y = glass.low[1] - 0.5
    for hole_id in ("m3_tl", "m3_tr", "m3_bl", "m3_br"):
        x, y, _ = _hole(layout, "xvf_core", hole_id)
        post = cylinder_z(x, y, core_face, -1.2, 7.0)
        if y + 3.5 > glass_low_y:
            # The glass's bottom border overhangs this post: join it to the
            # front plate below the glass with a bridge.
            post = post + block((x - 3.5, y - 3.5, -1.2), (x + 3.5, min(glass_low_y, y + 3.5) - 0.01, 0.0))
        else:
            post = post + cylinder_z(x, y, -1.2, 0.0, 7.0)
        features.append(post)
        cuts.append(cylinder_z(x, y, core_face - 1.0, core_face + INSERT_M3[1] + 1.0, INSERT_M3[0]))
        fasteners.append(Fastener(f"core_insert_{hole_id}", (x, y, core_face + 2.5), "insert_m3", "brass"))
        fasteners.append(Fastener(f"core_screw_{hole_id}", (x, y, core_face - 3.0), "screw_m3", "steel"))
    # PIR: two 1 mm posts, M2 self-tapping screws from behind.
    pir_face = _shape(layout, "pir.pcb").high[2]
    for hole_id in ("left", "right"):
        x, y, _ = _hole(layout, "pir", hole_id)
        features.append(cylinder_z(x, y, pir_face, 0.0, 4.5))
        cuts.append(cylinder_z(x, y, pir_face - 1.0, 2.0, PILOT_M2))
        fasteners.append(Fastener(f"pir_screw_{hole_id}", (x, y, pir_face), "screw_m2", "steel"))
    # Speaker: pads under its flange holes, M2 self-tapping screws.
    ears = _shape(layout, "speaker.ears")
    for side, x in (("left", ears.center[0] - 20.45), ("right", ears.center[0] + 20.45)):
        y = ears.center[1]
        features.append(cylinder_z(x, y, ears.high[2], 0.0, 4.5))
        cuts.append(cylinder_z(x, y, ears.high[2] - 1.0, 2.0, PILOT_M2))
        fasteners.append(Fastener(f"speaker_screw_{side}", (x, y, ears.high[2]), "screw_m2", "steel"))
    # LED: a collar that holds a 5 mm LED behind its window.
    led = _shape(layout, "mic_led.body")
    features.append(cylinder_z(led.center[0], led.center[1], -5.0, 0.0, 8.5))
    cuts.append(cylinder_z(led.center[0], led.center[1], -6.0, 0.5, 5.2))
    # Power-cable clamp boss on the bottom wall, two inserts beside the cable.
    power = layout.opening("power").box
    cx, cz = power.center[0], power.center[2]
    # A column from the front plate to the cable, the cable in a channel on
    # its back face; the clamp presses from behind.
    features.append(block((cx - 10.0, inner.low[1], cz), (cx + 10.0, inner.low[1] + CABLE_BOSS_DEPTH, 0.0)))
    cuts.append(block((cx - 2.6, inner.low[1] - 3.0, cz - 0.01), (cx + 2.6, inner.low[1] + CABLE_BOSS_DEPTH + 1.0,
                                                                     cz + 2.6)))
    for side in (-1, 1):
        x = cx + side * 6.5
        y = inner.low[1] + CABLE_BOSS_DEPTH / 2.0
        cuts.append(cylinder_z(x, y, cz - 0.01, cz + INSERT_M3[1], INSERT_M3[0]))
        fasteners.append(Fastener(f"cable_clamp_insert_{side}", (x, y, cz + 2.5), "insert_m3", "brass"))
    solid = subtract(union([body, *features]), [*cuts, *_front_openings(layout)])
    return Printed("front_shell", "Front shell: bezel, walls and mounts", solid, [("x", 180)],
                   orientation="face down: the bezel on the bed",
                   # The core's posts stand on its component face; the LED sits in its
                   # collar; the PIR's dome (round, 23 mm) passes the 24 mm window,
                   # which its square layout box overstates.
                   touches={"xvf_core.low_parts", "mic_led.body", "pir.lens"},
                   fasteners=fasteners)


# ---------------------------------------------------------------------------
# Rear plate.

def rear_plate(layout: Layout) -> Printed:
    shell = layout.shell
    inner, outer = shell.interior, shell.outer
    z0, z1 = outer.low[2], inner.low[2]
    plate = block((inner.low[0] + FIT, inner.low[1] + FIT, z0), (inner.high[0] - FIT, inner.high[1] - FIT, z1))
    features: list[bd.Part] = []
    cuts: list[bd.Part] = []
    fasteners: list[Fastener] = []
    for index, (x, y) in enumerate(rim_boss_positions(layout)):
        cuts.append(cylinder_z(x, y, z0 - 1.0, z1 + 1.0, CLEAR_M3))
        cuts.append(cylinder_z(x, y, z0 - 1.0, z0 + 1.8, 6.0))  # counterbore for the head
        fasteners.append(Fastener(f"rim_screw_{index}", (x, y, z0 + 1.0), "screw_m3", "steel"))
    # The FRDM board stands on bought M3 standoffs (frdm_standoffs); a
    # countersunk screw from the back holds each one.
    for hole_id in ("bh3", "bh4", "bh5", "bh6"):
        x, y, _ = _hole(layout, "frdm", hole_id)
        cuts.append(cylinder_z(x, y, z0 - CLEAT_DEPTH - 1.0, z1 + 1.0, CLEAR_M3))
        cuts.append(bd.Pos(x, y, z0 + 0.85) * bd.Cone(3.25, CLEAR_M3 / 2.0, 1.7))
        fasteners.append(Fastener(f"frdm_screw_{hole_id}", (x, y, z0 + 1.0), "screw_m3", "steel"))
    # The cleat hook on the back: its 45-degree face rests on the mount
    # plate's rail. (y, z) profile, z decreasing toward the mirror.
    y_low, y_high = CLEAT_Y
    gap = 0.3 * math.sqrt(2.0)
    hook = [(y_low + 7.5 - 0.0 + gap, z0), (y_high, z0), (y_high, z0 - CLEAT_DEPTH - 0.1),
            (y_low + 7.5 + CLEAT_DEPTH + gap - 0.1, z0 - CLEAT_DEPTH - 0.1)]
    features.append(prism_x(hook, inner.low[0] + 15.0, inner.high[0] - 15.0))
    # A spacer bar at the bottom keeps the plate parallel to the mirror.
    features.append(block((inner.low[0] + 15.0, inner.low[1] + 6.0, z0 - CLEAT_DEPTH - 0.1),
                          (inner.high[0] - 15.0, inner.low[1] + 14.0, z0)))
    solid = subtract(union([plate, *features]), cuts)
    return Printed("rear_plate", "Rear plate: back and cleat hook", solid, [("x", 180)], fasteners=fasteners,
                   orientation="inner face down, the cleat hook up")


def frdm_standoffs(layout: Layout) -> Printed:
    """Four bought M3 x 24 hex standoffs (female-female) under the board's corners."""
    z1 = layout.shell.interior.low[2]
    posts = [cylinder_z(*_hole(layout, "frdm", hole_id)[:2], z1, FRDM_COMPONENT_FACE, 6.4)
             for hole_id in ("bh3", "bh4", "bh5", "bh6")]
    fasteners = [Fastener(f"standoff_{hole_id}", (*_hole(layout, "frdm", hole_id)[:2], -32.0), "standoff_m3x24",
                          "brass") for hole_id in ("bh3", "bh4", "bh5", "bh6")]
    fasteners += [Fastener(f"board_screw_{hole_id}", (*_hole(layout, "frdm", hole_id)[:2], -17.0), "screw_m3",
                           "steel") for hole_id in ("bh3", "bh4", "bh5", "bh6")]
    return Printed("frdm_standoffs", "M3 x 24 standoffs for the FRDM board (bought)", union(posts), [],
                   touches={"frdm.pcb", "frdm.low_parts"}, fasteners=fasteners, kind="hardware")


# ---------------------------------------------------------------------------
# Mirror mounting plate.

def mount_plate(layout: Layout) -> Printed:
    """Bonded to the mirror's glass with VHB; the unit hangs on its rail."""
    inner, outer = layout.shell.interior, layout.shell.outer
    z_front = outer.low[2] - CLEAT_DEPTH - 0.5
    z_back = z_front - MOUNT_THICKNESS
    plate = block((inner.low[0] + 2.0, inner.low[1] + 2.0, z_back), (inner.high[0] - 2.0, CLEAT_Y[1], z_front))
    y_low = CLEAT_Y[0]
    rail = [(y_low, z_front), (y_low + 7.5 + CLEAT_DEPTH, z_front), (y_low + 7.5, z_front + CLEAT_DEPTH),
            (y_low, z_front + CLEAT_DEPTH)]
    solid = plate + prism_x(rail, inner.low[0] + 15.0, inner.high[0] - 15.0)
    return Printed("mount_plate", "Mirror mounting plate with the cleat rail", solid, [],
                   orientation="back (the VHB face) down, the rail up")


# ---------------------------------------------------------------------------
# Panel clamps.

def panel_clamp(layout: Layout, side: str) -> Printed:
    """A C-shaped clamp: a bar on two panel standoffs, arms out to the wall bosses."""
    sign = -1.0 if side == "left" else 1.0
    standoffs = [_hole(layout, "panel", hole) for hole in
                 (("m3_tl", "m3_bl") if side == "left" else ("m3_tr", "m3_br"))]
    x_post = standoffs[0][0]
    bosses = [(x, y) for x, y in panel_clamp_boss_positions(layout) if math.copysign(1.0, x) == sign]
    x_wall = bosses[0][0] + sign * (WEDGE_INSERT_INSET - FIT)
    z0, z1 = CLAMP_FACE - CLAMP_THICKNESS, CLAMP_FACE
    ys = sorted(y for _, _, y in ((s[0], s[2], s[1]) for s in standoffs))
    bar = block((x_post - 4.0, ys[0] - 4.0, z0), (x_post + 4.0, ys[1] + 4.0, z1))
    arms = [block((min(x_post, x_wall), y - 4.0, z0), (max(x_post, x_wall), y + 4.0, z1)) for y in ys]
    pads = [cylinder_z(x_post, y, z1, standoffs[0][2], 6.0) for y in ys]
    cuts = [cylinder_z(x_post, y, z0 - 1.0, standoffs[0][2] + 1.0, CLEAR_M3) for y in ys]
    cuts += [cylinder_z(x, y, z0 - 1.0, z1 + 1.0, CLEAR_M3) for x, y in bosses]
    fasteners = [Fastener(f"clamp_{side}_panel_screw_{index}", (x_post, y, z0), "screw_m3", "nylon")
                 for index, y in enumerate(ys)]
    fasteners += [Fastener(f"clamp_{side}_wall_screw_{index}", (x, y, z0), "screw_m3", "steel")
                  for index, (x, y) in enumerate(bosses)]
    solid = subtract(union([bar, *arms, *pads]), cuts)
    return Printed(f"panel_clamp_{side}", f"Panel clamp, {side}", solid, [], orientation="back down, pads up",
                   touches={"panel.m3_standoff_tl", "panel.m3_standoff_bl", "panel.m3_standoff_tr",
                            "panel.m3_standoff_br"}, fasteners=fasteners)


# ---------------------------------------------------------------------------
# Power-cable clamp.

def cable_clamp(layout: Layout) -> Printed:
    """A saddle that presses the USB-C cable into the bottom wall's boss."""
    inner = layout.shell.interior
    power = layout.opening("power").box
    cx, cz = power.center[0], power.center[2]
    y0, y1 = inner.low[1] + FIT, inner.low[1] + CABLE_BOSS_DEPTH
    plate = block((cx - 10.0, y0, cz - 4.0), (cx + 10.0, y1, cz))
    # The cable's channel is 0.4 mm shallower than the cable, so the clamp grips it.
    cuts = [block((cx - 2.6, y0 - 1.0, cz - 1.85), (cx + 2.6, y1 + 1.0, cz + 0.01))]
    y = inner.low[1] + CABLE_BOSS_DEPTH / 2.0
    cuts += [cylinder_z(cx + side * 6.5, y, cz - 5.0, cz + 1.0, CLEAR_M3) for side in (-1, 1)]
    fasteners = [Fastener(f"cable_clamp_screw_{side}", (cx + side * 6.5, y, cz - 4.0), "screw_m3", "steel")
                 for side in (-1, 1)]
    return Printed("cable_clamp", "Power-cable clamp", subtract(plate, cuts), [], orientation="back down, channel up",
                   touches={"cable:power"}, fasteners=fasteners)


def build(layout: Layout) -> list[Printed]:
    return [front_shell(layout), rear_plate(layout), mount_plate(layout), panel_clamp(layout, "left"),
            panel_clamp(layout, "right"), cable_clamp(layout), frdm_standoffs(layout)]


# ---------------------------------------------------------------------------
# Hole templates: 1.2 mm frames printed first, to check the estimated holes
# against the real board and panel before the large parts are printed.

TEMPLATE_THICKNESS = 1.2


def _frame(width: float, height: float, rail: float) -> bd.Part:
    outer = block((0.0, 0.0, 0.0), (width, height, TEMPLATE_THICKNESS))
    inner = block((rail, rail, -1.0), (width - rail, height - rail, TEMPLATE_THICKNESS + 1.0))
    return outer - inner


def frdm_template(layout: Layout) -> Printed:
    """The FRDM board's outline and corner holes, in the manual's top view.

    Lay the board on it component side up, J2 at the top edge: the four holes
    should line up with the board's. The notch on the left rail is J15's
    middle, the one on the bottom rail J4's.
    """
    part = layout.part("frdm")
    pcb = next(shape.local for shape in part.shapes if shape.id == "pcb")
    width, height = pcb.size[0], pcb.size[1]
    solid = _frame(width, height, 4.0)
    braces = []
    for hole in part.holes:
        local = _local_point(part, hole.at)
        braces.append(cylinder_z(local[0], local[1], 0.0, TEMPLATE_THICKNESS, 9.0))
    braces.append(block((width / 2 - 2.0, 0.0, 0.0), (width / 2 + 2.0, height, TEMPLATE_THICKNESS)))
    solid = union([solid, *braces])
    cuts = [cylinder_z(*_local_point(part, hole.at)[:2], -1.0, TEMPLATE_THICKNESS + 1.0, hole.diameter)
            for hole in part.holes]
    j15 = _local_point(part, layout.connector("frdm.j15").at)
    j4 = next(shape.local for shape in part.shapes if shape.id == "j4_usba")
    cuts.append(block((-1.0, j15[1] - 1.0, -1.0), (2.0, j15[1] + 1.0, TEMPLATE_THICKNESS + 1.0)))
    cuts.append(block((j4.center[0] - 1.0, -1.0, -1.0), (j4.center[0] + 1.0, 2.0, TEMPLATE_THICKNESS + 1.0)))
    return Printed("frdm_template", "FRDM hole template (print first)", subtract(solid, cuts), [], kind="template",
                   orientation="flat")


def panel_template(layout: Layout) -> Printed:
    """The panel PCB's outline and its four M3 standoffs, seen from behind.

    Lay it on the panel's back: each standoff (5.5 mm) should sit in its 6.2 mm
    hole, and the frame on the PCB's edges.
    """
    part = layout.part("panel")
    pcb = next(shape.world for shape in part.shapes if shape.id == "back_parts")
    width, height = pcb.size[0], pcb.size[1]
    solid = _frame(width, height, 4.0)
    pads, cuts = [], []
    for hole in part.holes:
        # Seen from behind, x is mirrored.
        x = pcb.high[0] - hole.at[0]
        y = hole.at[1] - pcb.low[1]
        pads.append(cylinder_z(x, y, 0.0, TEMPLATE_THICKNESS, 11.0))
        cuts.append(cylinder_z(x, y, -1.0, TEMPLATE_THICKNESS + 1.0, 6.2))
    for x in sorted({round(pcb.high[0] - hole.at[0], 3) for hole in part.holes}):
        pads.append(block((x - 2.0, 0.0, 0.0), (x + 2.0, height, TEMPLATE_THICKNESS)))
    return Printed("panel_template", "Panel standoff template (print first)",
                   subtract(union([solid, *pads]), cuts), [], kind="template", orientation="flat")


def _local_point(part, world: Vector) -> Vector:
    rotation, translation = part.pose.rotation, part.pose.translation
    offset = [world[i] - translation[i] for i in range(3)]
    return tuple(sum(rotation[row][column] * offset[row] for row in range(3)) for column in range(3))


def templates(layout: Layout) -> list[Printed]:
    return [frdm_template(layout), panel_template(layout)]
