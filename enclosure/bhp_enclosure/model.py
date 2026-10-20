"""The mirror unit's parts, shell and harness, loaded from ``enclosure/params.toml``.

Every dimension carries its provenance, so the caliper list and the drawings
can say which numbers are measured and which are not:

- ``measured``: calipers on the real part, with evidence under
  ``hardware_tests/results/``;
- ``vendor``: the vendor's drawing, CAD model or manual;
- ``estimate``: derived from a photo or another indirect source;
- ``placeholder``: the part is not chosen or not built yet;
- ``design``: a choice made here, not a property of a bought part.

A part's ``provenance`` table is the default for its items; any item may
override it with its own ``status`` and ``source``.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping, Sequence

from .geometry import Box, GeometryError, Pose, Vector, parse_direction

STATUSES = ("measured", "vendor", "estimate", "placeholder", "design")
SHAPE_KINDS = ("solid", "keepout", "rf_keepout", "access")
CABLE_KINDS = ("fpc", "round", "wires")
SCHEMA_VERSION = 1


class ModelError(ValueError):
    """``params.toml`` does not describe a well-formed layout."""

    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


@dataclass(frozen=True)
class Provenance:
    status: str
    source: str


@dataclass(frozen=True)
class Shape:
    part: str
    id: str
    local: Box
    world: Box
    kind: str
    metal: bool
    through: str | None
    access: str | None
    provenance: Provenance

    @property
    def key(self) -> str:
        return f"{self.part}.{self.id}"


@dataclass(frozen=True)
class Connector:
    part: str
    id: str
    at: Vector
    direction: Vector
    plug: Box | None
    straight: float
    provenance: Provenance

    @property
    def key(self) -> str:
        return f"{self.part}.{self.id}"


@dataclass(frozen=True)
class Hole:
    part: str
    id: str
    at: Vector
    diameter: float
    axis: Vector
    provenance: Provenance


@dataclass(frozen=True)
class Point:
    part: str
    id: str
    at: Vector
    provenance: Provenance


@dataclass(frozen=True)
class Measure:
    value: float
    provenance: Provenance


@dataclass(frozen=True)
class Part:
    id: str
    name: str
    pose: Pose
    mass_g: Measure | None
    shapes: tuple[Shape, ...]
    connectors: tuple[Connector, ...]
    holes: tuple[Hole, ...]
    points: tuple[Point, ...]
    provenance: Provenance


@dataclass(frozen=True)
class Opening:
    id: str
    face: str
    box: Box
    purpose: str


@dataclass(frozen=True)
class Shell:
    outer: Box
    wall: float
    rear: float
    front: float
    gasket: float
    lip_overlap: float
    openings: tuple[Opening, ...]

    @property
    def interior(self) -> Box:
        """Inside the side walls and rear plate, up to the glass front (z = 0)."""
        low, high = self.outer.low, self.outer.high
        return Box((low[0] + self.wall, low[1] + self.wall, low[2] + self.rear),
                   (high[0] - self.wall, high[1] - self.wall, 0.0))


@dataclass(frozen=True)
class Cable:
    id: str
    name: str
    kind: str
    ends: tuple[str, str]
    width: float
    thickness: float
    width_axis: Vector | None
    min_bend_radius: float
    length: Measure | None
    min_slack: float
    max_excess: float
    service_loop: float
    points: tuple[Vector, ...]
    exit_opening: str | None
    provenance: Provenance
    notes: str
    conductors: tuple[str, ...] = ()


@dataclass(frozen=True)
class Printer:
    bed: tuple[float, float]
    margin: float
    material: str


@dataclass(frozen=True)
class Clearances:
    part: float
    cable: float
    rf_metal: float
    mic_to_speaker: float


@dataclass(frozen=True)
class Layout:
    parts: tuple[Part, ...]
    shell: Shell
    cables: tuple[Cable, ...]
    printer: Printer
    clearances: Clearances
    allowed_pairs: frozenset[frozenset[str]] = field(default_factory=frozenset)
    #: The part whose points are microphone ports, and the speaker's shape key.
    acoustic: tuple[str, str] | None = None

    def part(self, part_id: str) -> Part:
        for part in self.parts:
            if part.id == part_id:
                return part
        raise KeyError(part_id)

    def connector(self, key: str) -> Connector:
        part_id, _, connector_id = key.partition(".")
        for connector in self.part(part_id).connectors:
            if connector.id == connector_id:
                return connector
        raise KeyError(key)

    def shapes(self) -> list[Shape]:
        return [shape for part in self.parts for shape in part.shapes]

    def opening(self, opening_id: str) -> Opening:
        for opening in self.shell.openings:
            if opening.id == opening_id:
                return opening
        raise KeyError(opening_id)


def _require(table: Mapping[str, Any], key: str, where: str) -> Any:
    if key not in table:
        raise ModelError("PARAMS.MISSING_KEY", f"{where} needs {key!r}")
    return table[key]


def _vector(value: Any, where: str, length: int = 3) -> tuple[float, ...]:
    if (not isinstance(value, list) or len(value) != length
            or not all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in value)):
        raise ModelError("PARAMS.BAD_VECTOR", f"{where} must be {length} numbers")
    return tuple(float(item) for item in value)


def _number(value: Any, where: str, positive: bool = False) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ModelError("PARAMS.BAD_NUMBER", f"{where} must be a number")
    if positive and value <= 0:
        raise ModelError("PARAMS.BAD_NUMBER", f"{where} must be positive")
    return float(value)


def _provenance(table: Mapping[str, Any], default: Provenance | None, where: str) -> Provenance:
    status = table.get("status", default.status if default else None)
    source = table.get("source", default.source if default else None)
    if status not in STATUSES:
        raise ModelError("PARAMS.BAD_STATUS", f"{where} status must be one of {', '.join(STATUSES)}")
    if not isinstance(source, str) or not source.strip():
        raise ModelError("PARAMS.MISSING_SOURCE", f"{where} needs a source")
    return Provenance(status, source)


def _measure(value: Any, default: Provenance | None, where: str) -> Measure:
    if isinstance(value, Mapping):
        return Measure(_number(_require(value, "value", where), where, positive=True),
                       _provenance(value, default, where))
    if default is None:
        raise ModelError("PARAMS.MISSING_SOURCE", f"{where} needs a source")
    return Measure(_number(value, where, positive=True), default)


def _box(table: Mapping[str, Any], where: str) -> Box:
    try:
        return Box.from_min_size(_vector(_require(table, "min", where), f"{where}.min"),
                                 _vector(_require(table, "size", where), f"{where}.size"))
    except GeometryError as error:
        raise ModelError("PARAMS.BAD_BOX", f"{where}: {error}") from error


def _pose(table: Mapping[str, Any], where: str) -> Pose:
    steps = []
    for step in table.get("rotate", []):
        if not isinstance(step, list) or len(step) != 2:
            raise ModelError("PARAMS.BAD_POSE", f"{where}.rotate entries are [axis, degrees]")
        steps.append((step[0], step[1]))
    try:
        return Pose.from_steps(steps, _vector(_require(table, "at", where), f"{where}.at"))
    except GeometryError as error:
        raise ModelError("PARAMS.BAD_POSE", f"{where}: {error}") from error


def _direction(text: Any, where: str) -> Vector:
    try:
        return parse_direction(text)
    except GeometryError as error:
        raise ModelError("PARAMS.BAD_DIRECTION", f"{where}: {error}") from error


def _ids_unique(items: Sequence[Any], where: str) -> None:
    seen = set()
    for item in items:
        if item.id in seen:
            raise ModelError("PARAMS.DUPLICATE_ID", f"{where} repeats {item.id!r}")
        seen.add(item.id)


def _part(table: Mapping[str, Any]) -> Part:
    part_id = _require(table, "id", "parts[]")
    where = f"parts.{part_id}"
    provenance = _provenance(_require(table, "provenance", where), None, f"{where}.provenance")
    pose = _pose(_require(table, "pose", where), f"{where}.pose")
    metal = bool(table.get("metal", False))
    shapes = []
    for item in table.get("shapes", []):
        shape_id = _require(item, "id", f"{where}.shapes[]")
        here = f"{where}.shapes.{shape_id}"
        kind = item.get("kind", "solid")
        if kind not in SHAPE_KINDS:
            raise ModelError("PARAMS.BAD_KIND", f"{here} kind must be one of {', '.join(SHAPE_KINDS)}")
        local = _box(item, here)
        shapes.append(Shape(part_id, shape_id, local, local.placed(pose), kind,
                            bool(item.get("metal", metal)), item.get("through"), item.get("access"),
                            _provenance(item, provenance, here)))
    connectors = []
    for item in table.get("connectors", []):
        connector_id = _require(item, "id", f"{where}.connectors[]")
        here = f"{where}.connectors.{connector_id}"
        direction = _direction(_require(item, "exit", here), f"{here}.exit")
        at = _vector(_require(item, "at", here), f"{here}.at")
        plug = _box(item["plug"], f"{here}.plug").placed(pose) if "plug" in item else None
        connectors.append(Connector(part_id, connector_id, pose.point(at), pose.direction(direction), plug,
                                    _number(item.get("straight", 0.0), f"{here}.straight"),
                                    _provenance(item, provenance, here)))
    holes = []
    for item in table.get("holes", []):
        hole_id = _require(item, "id", f"{where}.holes[]")
        here = f"{where}.holes.{hole_id}"
        holes.append(Hole(part_id, hole_id, pose.point(_vector(_require(item, "at", here), f"{here}.at")),
                          _number(_require(item, "d", here), f"{here}.d", positive=True),
                          pose.direction(_direction(item.get("axis", "+z"), f"{here}.axis")),
                          _provenance(item, provenance, here)))
    points = []
    for item in table.get("points", []):
        point_id = _require(item, "id", f"{where}.points[]")
        here = f"{where}.points.{point_id}"
        points.append(Point(part_id, point_id, pose.point(_vector(_require(item, "at", here), f"{here}.at")),
                            _provenance(item, provenance, here)))
    for group, name in ((shapes, "shapes"), (connectors, "connectors"), (holes, "holes"), (points, "points")):
        _ids_unique(group, f"{where}.{name}")
    mass = _measure(table["mass_g"], provenance, f"{where}.mass_g") if "mass_g" in table else None
    return Part(part_id, str(table.get("name", part_id)), pose, mass, tuple(shapes), tuple(connectors),
                tuple(holes), tuple(points), provenance)


def _shell(table: Mapping[str, Any]) -> Shell:
    where = "shell"
    outer = Box(_vector(_require(table, "outer_low", where), f"{where}.outer_low"),  # type: ignore[arg-type]
                _vector(_require(table, "outer_high", where), f"{where}.outer_high"))  # type: ignore[arg-type]
    if any(outer.low[i] >= outer.high[i] for i in range(3)):
        raise ModelError("PARAMS.BAD_BOX", "shell outer_low must be below outer_high")
    openings = []
    for item in table.get("openings", []):
        opening_id = _require(item, "id", f"{where}.openings[]")
        here = f"{where}.openings.{opening_id}"
        face = _require(item, "face", here)
        _direction(face, f"{here}.face")
        openings.append(Opening(opening_id, face, _box(item, here), str(item.get("purpose", ""))))
    _ids_unique(openings, f"{where}.openings")
    return Shell(outer, *(_number(_require(table, key, where), f"{where}.{key}", positive=True)
                          for key in ("wall", "rear", "front", "gasket", "lip_overlap")),
                 tuple(openings))


def _cable(table: Mapping[str, Any]) -> Cable:
    cable_id = _require(table, "id", "cables[]")
    where = f"cables.{cable_id}"
    kind = _require(table, "kind", where)
    if kind not in CABLE_KINDS:
        raise ModelError("PARAMS.BAD_KIND", f"{where} kind must be one of {', '.join(CABLE_KINDS)}")
    provenance = _provenance(_require(table, "provenance", where), None, f"{where}.provenance")
    section = _vector(_require(table, "section", where), f"{where}.section", 2)
    width_axis = None
    if kind == "fpc":
        width_axis = _direction(_require(table, "width_axis", where), f"{where}.width_axis")
    length = _measure(table["length"], provenance, f"{where}.length") if "length" in table else None
    points = tuple(_vector(point, f"{where}.route[]") for point in _require(table, "route", where))
    if len(points) < 2:
        raise ModelError("PARAMS.BAD_ROUTE", f"{where} route needs at least two points")
    ends = _require(table, "ends", where)
    if not isinstance(ends, list) or len(ends) != 2 or not all(isinstance(end, str) for end in ends):
        raise ModelError("PARAMS.BAD_ROUTE", f"{where} ends are [connector, connector or \"outside\"]")
    return Cable(cable_id, str(table.get("name", cable_id)), kind, (ends[0], ends[1]),
                 section[0], section[1], width_axis,
                 _number(_require(table, "min_bend_radius", where), f"{where}.min_bend_radius", positive=True),
                 length, _number(table.get("min_slack", 0.0), f"{where}.min_slack"),
                 _number(table.get("max_excess", 0.0), f"{where}.max_excess"),
                 _number(table.get("service_loop", 0.0), f"{where}.service_loop"),
                 points, table.get("exit_opening"), provenance, str(table.get("notes", "")),  # type: ignore[arg-type]
                 tuple(str(item) for item in table.get("conductors", [])))


def load(path: Path) -> Layout:
    with path.open("rb") as handle:
        data = tomllib.load(handle)
    if data.get("schema_version") != SCHEMA_VERSION:
        raise ModelError("PARAMS.SCHEMA_VERSION", f"expected schema_version = {SCHEMA_VERSION}")
    printer_table = _require(data, "printer", "params")
    bed = _vector(_require(printer_table, "bed", "printer"), "printer.bed", 2)
    printer = Printer((bed[0], bed[1]), _number(printer_table.get("margin", 5.0), "printer.margin"),
                      str(_require(printer_table, "material", "printer")))
    clearance_table = _require(data, "clearance", "params")
    clearances = Clearances(*(_number(_require(clearance_table, key, "clearance"), f"clearance.{key}")
                              for key in ("part", "cable", "rf_metal", "mic_to_speaker")))
    parts = tuple(_part(table) for table in _require(data, "parts", "params"))
    _ids_unique(parts, "parts")
    cables = tuple(_cable(table) for table in data.get("cables", []))
    _ids_unique(cables, "cables")
    allowed = frozenset(frozenset(pair) for pair in data.get("allow", {}).get("contact", []))
    acoustic = None
    if "acoustic" in data:
        table = data["acoustic"]
        acoustic = (str(_require(table, "mic_part", "acoustic")), str(_require(table, "speaker_shape", "acoustic")))
    layout = Layout(parts, _shell(_require(data, "shell", "params")), cables, printer, clearances, allowed,
                    acoustic)
    for cable in cables:
        for end in cable.ends:
            if end == "outside":
                continue
            try:
                layout.connector(end)
            except KeyError as error:
                raise ModelError("PARAMS.UNKNOWN_CONNECTOR", f"cables.{cable.id} names {end!r}") from error
        if "outside" in cable.ends and cable.exit_opening is None:
            raise ModelError("PARAMS.BAD_ROUTE", f"cables.{cable.id} leaves the unit but names no exit_opening")
    for shape in layout.shapes():
        for name in (shape.through, shape.access):
            if name is not None:
                try:
                    layout.opening(name)
                except KeyError as error:
                    raise ModelError("PARAMS.UNKNOWN_OPENING", f"{shape.key} names {name!r}") from error
    return layout
