"""Layout drawings (SVG) and the harness and caliper tables (Markdown).

All of it comes from the same ``Layout`` the fit check uses, so a drawing can
never show a cable route or a part position the check did not see. Output is
deterministic: the same parameters always give the same bytes.
"""

from __future__ import annotations

import html
from dataclasses import dataclass
from typing import Callable, Iterable

from .checks import bend_summary
from .geometry import Box, Vector, route_length
from .model import Cable, Layout, Provenance

PALETTE = ("#2563eb", "#059669", "#d97706", "#7c3aed", "#db2777", "#0891b2", "#65a30d", "#dc2626",
           "#4b5563", "#9333ea", "#0d9488", "#ca8a04")
CABLE_COLORS = ("#e11d48", "#0284c7", "#16a34a", "#ea580c", "#9333ea", "#0f766e", "#b45309", "#be123c")
#: How each provenance status is stroked, so unmeasured geometry looks unmeasured.
DASH = {"measured": "", "vendor": "", "design": "", "estimate": "4 2", "placeholder": "1 2"}
SCALE = 3.0  # pixels per millimetre
MARGIN = 40.0
MIN_WIDTH = 760.0


@dataclass(frozen=True)
class View:
    name: str
    title: str
    #: (horizontal, vertical-up) screen coordinates of a unit-frame point.
    project: Callable[[Vector], tuple[float, float]]
    #: Larger draws on top.
    depth: Callable[[Vector], float]
    notes: tuple[str, str]


VIEWS = (
    View("front", "Front (as the person at the mirror sees it)",
         lambda p: (p[0], p[1]), lambda p: p[2], ("left", "right")),
    View("side", "Side, from the unit's right (mirror on the right, glass on the left)",
         lambda p: (-p[2], p[1]), lambda p: p[0], ("glass front", "mirror")),
    View("top", "Top, looking down (mirror at the top, glass at the bottom)",
         lambda p: (p[0], -p[2]), lambda p: p[1], ("left", "right")),
)


def _rect(view: View, box: Box) -> tuple[float, float, float, float]:
    a, b = view.project(box.low), view.project(box.high)
    return min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])


def _fmt(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text == "-0" else text


class _Canvas:
    def __init__(self, bounds: tuple[float, float, float, float]) -> None:
        self.x0, self.y0, self.x1, self.y1 = bounds
        self.items: list[str] = []

    def x(self, value: float) -> str:
        return _fmt((value - self.x0) * SCALE + MARGIN)

    def y(self, value: float) -> str:
        return _fmt((self.y1 - value) * SCALE + MARGIN)

    def rect(self, rect: tuple[float, float, float, float], style: str, title: str = "") -> None:
        x0, y0, x1, y1 = rect
        tooltip = f"<title>{html.escape(title)}</title>" if title else ""
        self.items.append(f'<rect x="{self.x(x0)}" y="{self.y(y1)}" width="{_fmt((x1 - x0) * SCALE)}" '
                          f'height="{_fmt((y1 - y0) * SCALE)}" {style}>{tooltip}</rect>')

    def polyline(self, points: Iterable[tuple[float, float]], style: str, title: str = "") -> None:
        text = " ".join(f"{self.x(px)},{self.y(py)}" for px, py in points)
        tooltip = f"<title>{html.escape(title)}</title>" if title else ""
        self.items.append(f'<polyline points="{text}" fill="none" {style}>{tooltip}</polyline>')

    def text(self, at: tuple[float, float], body: str, size: float = 9.0, anchor: str = "start",
             color: str = "#111827") -> None:
        self.items.append(f'<text x="{self.x(at[0])}" y="{self.y(at[1])}" font-size="{_fmt(size)}" '
                          f'text-anchor="{anchor}" fill="{color}">{html.escape(body)}</text>')

    def svg(self, title: str, legend: list[tuple[str, str]]) -> str:
        width = max((self.x1 - self.x0) * SCALE + 2 * MARGIN, MIN_WIDTH)
        height = (self.y1 - self.y0) * SCALE + 2 * MARGIN + 14 * len(legend) + 10
        lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{_fmt(width)}" height="{_fmt(height)}" '
                 f'viewBox="0 0 {_fmt(width)} {_fmt(height)}" font-family="sans-serif">',
                 f'<rect width="{_fmt(width)}" height="{_fmt(height)}" fill="#ffffff"/>',
                 f'<text x="{_fmt(MARGIN)}" y="22" font-size="13" fill="#111827">{html.escape(title)}</text>']
        lines += self.items
        base = (self.y1 - self.y0) * SCALE + 2 * MARGIN + 6
        for index, (color, entry) in enumerate(legend):
            y = base + 14 * index
            if color:
                lines.append(f'<rect x="{_fmt(MARGIN)}" y="{_fmt(y - 9)}" width="10" height="10" '
                             f'fill="{color}" fill-opacity="0.5" stroke="{color}"/>')
            lines.append(f'<text x="{_fmt(MARGIN + 16)}" y="{_fmt(y)}" font-size="10" '
                         f'fill="#374151">{html.escape(entry)}</text>')
        lines.append("</svg>")
        return "\n".join(lines) + "\n"


def _part_colors(layout: Layout) -> dict[str, str]:
    return {part.id: PALETTE[index % len(PALETTE)] for index, part in enumerate(layout.parts)}


def render(layout: Layout, view: View) -> str:
    shell = layout.shell
    x0, y0, x1, y1 = _rect(view, shell.outer)
    pad = 25.0
    canvas = _Canvas((x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    canvas.rect(_rect(view, shell.outer), 'fill="#f3f4f6" stroke="#111827" stroke-width="1.5"',
                "shell outer")
    canvas.rect(_rect(view, shell.interior), 'fill="none" stroke="#6b7280" stroke-dasharray="6 3"',
                "shell interior")
    colors = _part_colors(layout)
    shapes = sorted(layout.shapes(), key=lambda shape: (view.depth(shape.world.high), shape.key))
    for shape in shapes:
        color = colors[shape.part]
        dash = DASH[shape.provenance.status]
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        if shape.kind == "solid":
            style = f'fill="{color}" fill-opacity="0.08" stroke="{color}" stroke-width="0.8"{dash_attr}'
        elif shape.kind == "rf_keepout":
            style = 'fill="#fde68a" fill-opacity="0.35" stroke="#b45309" stroke-dasharray="2 2"'
        else:
            style = f'fill="none" stroke="{color}" stroke-dasharray="1 3" stroke-width="0.8"'
        canvas.rect(_rect(view, shape.world), style,
                    f"{shape.key} ({shape.kind}, {shape.provenance.status}: {shape.provenance.source})")
    for opening in shell.openings:
        canvas.rect(_rect(view, opening.box), 'fill="#fecaca" fill-opacity="0.5" stroke="#dc2626"',
                    f"opening {opening.id}: {opening.purpose}")
    for index, cable in enumerate(layout.cables):
        color = CABLE_COLORS[index % len(CABLE_COLORS)]
        projected = [view.project(point) for point in cable.points]
        width = max(2.0, min(cable.thickness, 6.0) * SCALE * 0.6)
        canvas.polyline(projected, f'stroke="{color}" stroke-width="{_fmt(width)}" stroke-linejoin="round" '
                                   'stroke-opacity="0.85"', f"cable {cable.id}: {cable.name}")
        middle = projected[len(projected) // 2]
        canvas.text(middle, cable.id, 8.0, "middle", color)
    for part in layout.parts:
        for connector in part.connectors:
            canvas.text(view.project(connector.at), f"{part.id}.{connector.id}", 6.5, "start", "#374151")
    sx0, sy0, sx1, sy1 = _rect(view, shell.outer)
    canvas.text((sx0, sy0 - 8.0), view.notes[0], 9.0, "start", "#6b7280")
    canvas.text((sx1, sy0 - 8.0), view.notes[1], 9.0, "end", "#6b7280")
    canvas.text((sx0, sy1 + 4.0), f"outer {_fmt(shell.outer.size[0])} x {_fmt(shell.outer.size[1])} x "
                                  f"{_fmt(shell.outer.size[2])} mm (W x H x D)", 9.0)
    legend = [(colors[part.id], f"{part.id}: {part.name}") for part in layout.parts]
    legend.append(("", "Outline: solid = vendor or measured, dashed = estimate, dotted = placeholder; "
                       "red = shell openings; thick lines = cables (labelled at their middle)."))
    return canvas.svg(view.title, legend)


def drawings(layout: Layout) -> dict[str, str]:
    return {f"{view.name}.svg": render(layout, view) for view in VIEWS}


def _md(text: str) -> str:
    """Text for a Markdown table cell."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("|", "\\|")


def harness_table(layout: Layout) -> str:
    rows = ["| Cable | Kind | From | To | Conductors | Section (mm) | Min bend r | Route | Length | Bends | Notes |",
            "|---|---|---|---|---|---|---|---|---|---|---|"]
    for cable in layout.cables:
        needed = route_length(cable.points, cable.min_bend_radius)
        if cable.length is not None:
            length = f"{_fmt(cable.length.value)} ({cable.length.provenance.status})"
        elif "outside" in cable.ends:
            length = f"{_fmt(needed)} inside the unit"
        else:
            length = f"cut to {_fmt(needed + cable.service_loop)} ({_fmt(cable.service_loop)} loop)"
        bends = ", ".join(f"{round(angle)}°" for _, angle in bend_summary(cable)) or "none"
        section = (f"{_fmt(cable.width)} x {_fmt(cable.thickness)}" if cable.kind == "fpc"
                   else f"Ø{_fmt(cable.width)}")
        conductors = ", ".join(cable.conductors) or "-"
        rows.append(f"| `{cable.id}` {_md(cable.name)} | {cable.kind} | `{cable.ends[0]}` | "
                    f"`{cable.ends[1]}` | {_md(conductors)} | {section} | {_fmt(cable.min_bend_radius)} | "
                    f"{_fmt(needed)} | {length} | {bends} | {_md(cable.notes)} |")
    return "\n".join(rows) + "\n"


def _dims(box: Box) -> str:
    return " x ".join(_fmt(value) for value in box.size)


def caliper_table(layout: Layout) -> str:
    """What to measure: every estimate and placeholder, then a count of vendor numbers per part."""
    rows = ["| Part | Item | What | Value (mm) | Status | Source |", "|---|---|---|---|---|---|"]
    vendor: list[str] = []
    for part in layout.parts:
        entries: list[tuple[str, str, str, Provenance]] = []
        for shape in part.shapes:
            entries.append((shape.id, f"{shape.kind} box", f"{_dims(shape.local)} at {_vec(shape.local.low)}",
                            shape.provenance))
        for hole in part.holes:
            entries.append((hole.id, "hole", f"Ø{_fmt(hole.diameter)} at {_vec(_local(part, hole.at))}",
                            hole.provenance))
        for connector in part.connectors:
            entries.append((connector.id, "connector exit", _vec(_local(part, connector.at)),
                            connector.provenance))
        for point in part.points:
            entries.append((point.id, "point", _vec(_local(part, point.at)), point.provenance))
        if part.mass_g is not None:
            entries.append(("mass", "mass (g)", _fmt(part.mass_g.value), part.mass_g.provenance))
        vendor_count = 0
        for item, what, value, provenance in entries:
            if provenance.status == "vendor":
                vendor_count += 1
            elif provenance.status in ("estimate", "placeholder"):
                source = "" if provenance == part.provenance else _md(provenance.source)
                rows.append(f"| {part.id} | {item} | {what} | {value} | {provenance.status} | {source} |")
        if vendor_count:
            vendor.append(f"- `{part.id}`: {vendor_count} vendor values ({_md(part.provenance.source)} "
                          "where the row does not say otherwise). Spot-check the outline, the holes "
                          "and each connector's position.")
    for cable in layout.cables:
        if cable.length is not None and cable.length.provenance.status in ("estimate", "placeholder"):
            rows.append(f"| cable {cable.id} | length | cable length | {_fmt(cable.length.value)} | "
                        f"{cable.length.provenance.status} | {_md(cable.length.provenance.source)} |")
    sources = [f"- `{part.id}`: {_md(part.provenance.source)}" for part in layout.parts]
    return ("Estimates and placeholders, which decide the fit:\n\n" + "\n".join(rows) +
            "\n\nA blank source means the part's own:\n\n" + "\n".join(sources) +
            "\n\nVendor values, to spot-check against the real part:\n\n" + "\n".join(vendor) + "\n")


def _vec(vector: Vector) -> str:
    return "(" + ", ".join(_fmt(value) for value in vector) + ")"


def _local(part, world: Vector) -> Vector:
    """Back from the unit frame to the part's frame (rotations are orthonormal)."""
    rotation, translation = part.pose.rotation, part.pose.translation
    offset = tuple(world[i] - translation[i] for i in range(3))
    return tuple(sum(rotation[row][column] * offset[row] for row in range(3)) for column in range(3))  # type: ignore[return-value]
