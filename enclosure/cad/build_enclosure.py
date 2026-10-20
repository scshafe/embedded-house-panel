#!/usr/bin/env python3
"""Build the mirror unit's printed parts, check them, and export them.

    python enclosure/cad/build_enclosure.py [--out DIR] [--no-export]

Needs build123d: run it with the venv that ``make enclosure-venv`` creates
(``make verify-enclosure`` does both). For each printed part it writes, to
``.build/enclosure/out/`` by default:
- ``<part>.step``, in the unit frame;
- ``<part>.stl`` and ``<part>.3mf``, turned onto the bed;
- ``assembly.step`` and ``summary.json`` (bed fit, volumes, the mass
  estimate and the VHB area).

The solid check fails, with stable codes, when a printed part:
- intersects a layout part, plug, keep-out or cable sweep it is not meant to
  touch;
- intersects another printed part;
- does not fit the bed;
- puts metal (an insert or a steel screw) near the Wi-Fi antenna.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "enclosure"))
sys.path.insert(0, str(ROOT / "enclosure/cad"))

import build123d as bd  # noqa: E402

import parts as printed_parts  # noqa: E402
from bhp_enclosure import checks, model  # noqa: E402
from bhp_enclosure.checks import Finding  # noqa: E402
from bhp_enclosure.geometry import Box  # noqa: E402

PARAMS = ROOT / "enclosure/params.toml"
DEFAULT_OUT = ROOT / ".build/enclosure/out"
PRINT_DOC = ROOT / "docs/hardware/mirror-unit-print.md"
FASTENER_NAMES = {
    "insert_m3": "M3 heat-set insert (for a 4.0 mm hole, 4-5 mm long)",
    "screw_m3": "M3 screw",
    "screw_m2": "M2 self-tapping screw",
    "standoff_m3x24": "M3 x 24 hex standoff, female-female",
}
#: Intersections smaller than this (mm^3) are tessellation noise.
VOLUME_TOLERANCE = 1e-2
#: PETG density (g/cm^3) and the share of a part's volume a typical print
#: (three perimeters, 25 % infill) actually fills; an estimate.
PETG_DENSITY = 1.27
FILL_SHARE = 0.55
#: 3M's static-load guideline: 57 cm^2 of VHB per kg, times the enclosure
#: plan's safety factor of 3.
VHB_CM2_PER_KG = 57.0
VHB_FACTOR = 3.0


def _bbox(shape: bd.Shape) -> Box:
    bounds = shape.bounding_box()
    return Box((bounds.min.X, bounds.min.Y, bounds.min.Z), (bounds.max.X, bounds.max.Y, bounds.max.Z))


def _overlap_volume(solid: bd.Part, other: bd.Part) -> float:
    result = solid & other
    shapes = result if isinstance(result, list) else [result]
    return sum(getattr(shape, "volume", 0.0) for shape in shapes if shape is not None)


def oriented(part: printed_parts.Printed) -> bd.Part:
    """The part turned onto the bed, its lowest point at z = 0 and centred on x and y."""
    solid = part.solid
    for axis, degrees in part.print_turns:
        rotation = {"x": bd.Rot(degrees, 0, 0), "y": bd.Rot(0, degrees, 0), "z": bd.Rot(0, 0, degrees)}[axis]
        solid = rotation * solid
    bounds = solid.bounding_box()
    return bd.Pos(-(bounds.min.X + bounds.max.X) / 2.0, -(bounds.min.Y + bounds.max.Y) / 2.0, -bounds.min.Z) * solid


def solid_check(layout: model.Layout, built: list[printed_parts.Printed]) -> list[Finding]:
    findings: list[Finding] = []
    targets: list[tuple[str, str, Box]] = []
    for shape in layout.shapes():
        if shape.kind in ("solid", "keepout", "access"):
            targets.append((shape.key, shape.part, shape.world))
    for part in layout.parts:
        for connector in part.connectors:
            if connector.plug is not None:
                targets.append((f"{connector.key} plug", part.id, connector.plug))
    for cable in layout.cables:
        for index, box in enumerate(checks.cable_boxes(cable)):
            targets.append((f"cable:{cable.id} segment {index + 1}", f"cable:{cable.id}", box))
    in_unit = [item for item in built if item.kind != "template"]
    for item in in_unit:
        bounds = _bbox(item.solid)
        for key, owner, box in targets:
            if owner in item.touches or key.split(" ")[0] in item.touches:
                continue
            if not bounds.intersects(box):
                continue
            volume = _overlap_volume(item.solid, printed_parts.box_solid(box))
            if volume > VOLUME_TOLERANCE:
                code = "CAD.CABLE_COLLISION" if owner.startswith("cable:") else "CAD.PART_COLLISION"
                findings.append(Finding(code, f"{item.id} x {key}", f"{volume:.2f} mm^3 overlap"))
    for a_index, a in enumerate(in_unit):
        for b in in_unit[a_index + 1:]:
            if not _bbox(a.solid).intersects(_bbox(b.solid)):
                continue
            volume = _overlap_volume(a.solid, b.solid)
            if volume > VOLUME_TOLERANCE:
                findings.append(Finding("CAD.PRINTED_PARTS_COLLIDE", f"{a.id} x {b.id}", f"{volume:.2f} mm^3 overlap"))
    bed_x, bed_y = layout.printer.bed
    margin = layout.printer.margin
    for item in built:
        if item.kind == "hardware":
            continue
        size = _bbox(oriented(item)).size
        if size[0] > bed_x - 2 * margin or size[1] > bed_y - 2 * margin or size[2] > max(bed_x, bed_y):
            findings.append(Finding("CAD.BED_FIT", item.id,
                                    f"{size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} on a {bed_x:.0f} x {bed_y:.0f} "
                                    f"bed with {margin:.0f} mm margins"))
    zones = [shape.world.grown(layout.clearances.rf_metal) for shape in layout.shapes() if shape.kind == "rf_keepout"]
    for item in built:
        for fastener in item.fasteners:
            if fastener.material == "nylon":
                continue
            point = Box(fastener.at, fastener.at)
            if any(zone.intersects(point, tolerance=-1e-9) for zone in zones):
                findings.append(Finding("CAD.METAL_NEAR_ANTENNA", f"{item.id}.{fastener.id}",
                                        f"{fastener.material} {fastener.kind} within "
                                        f"{layout.clearances.rf_metal:.0f} mm of the antenna; use nylon"))
    return findings


def summary(layout: model.Layout, built: list[printed_parts.Printed]) -> dict[str, object]:
    parts = {}
    printed_mass = 0.0
    for item in built:
        if item.kind == "hardware":
            continue
        volume_cm3 = item.solid.volume / 1000.0
        mass = volume_cm3 * PETG_DENSITY * FILL_SHARE
        if item.kind == "printed" and item.id != "mount_plate":
            printed_mass += mass
        size = _bbox(oriented(item)).size
        parts[item.id] = {"name": item.name, "kind": item.kind, "volume_cm3": round(volume_cm3, 1),
                          "mass_g_estimate": round(mass, 1), "print_turns": [list(turn) for turn in item.print_turns],
                          "bed_footprint_mm": [round(size[0], 1), round(size[1], 1)],
                          "height_on_bed_mm": round(size[2], 1),
                          "fasteners": sorted(f"{f.kind} ({f.material})" for f in item.fasteners)}
    electronics = sum(part.mass_g.value for part in layout.parts if part.mass_g is not None)
    hanging = printed_mass + electronics
    return {
        "printed_parts": parts,
        "mass_g_estimate": {"printed_on_the_unit": round(printed_mass, 1), "electronics": round(electronics, 1),
                            "hanging_total": round(hanging, 1)},
        "vhb_cm2_needed": round(hanging / 1000.0 * VHB_CM2_PER_KG * VHB_FACTOR, 1),
        "note": "Masses are estimates (PETG at 55 % fill and unweighed parts); weigh the unit before bonding.",
    }


def export(layout: model.Layout, built: list[printed_parts.Printed], out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for item in built:
        if item.kind == "hardware":
            continue
        bd.export_step(item.solid, out / f"{item.id}.step")
        on_bed = oriented(item)
        bd.export_stl(on_bed, out / f"{item.id}.stl", tolerance=0.01, angular_tolerance=0.1)
        mesher = bd.Mesher()
        mesher.add_shape(on_bed, linear_deflection=0.01, angular_deflection=0.1)
        mesher.write(str(out / f"{item.id}.3mf"))
    assembly = bd.Compound(children=[item.solid for item in built if item.kind != "template"])
    bd.export_step(assembly, out / "assembly.step")
    (out / "summary.json").write_text(json.dumps(summary(layout, built), indent=2, sort_keys=True) + "\n",
                                      encoding="utf-8")


def parts_table(built: list[printed_parts.Printed]) -> str:
    rows = ["| Part | What | On the bed | Footprint (mm) | Height (mm) |", "|---|---|---|---|---|"]
    for item in built:
        if item.kind == "hardware":
            continue
        size = _bbox(oriented(item)).size
        rows.append(f"| `{item.id}` | {item.name} | {item.orientation} | {size[0]:.1f} x {size[1]:.1f} | "
                    f"{size[2]:.1f} |")
    return "\n".join(rows) + "\n"


def hardware_table(built: list[printed_parts.Printed]) -> str:
    counts: dict[tuple[str, str], list[str]] = {}
    for item in built:
        for fastener in item.fasteners:
            counts.setdefault((fastener.kind, fastener.material), []).append(item.id)
    rows = ["| Hardware | Material | Count | Where |", "|---|---|---|---|"]
    for (kind, material), owners in sorted(counts.items()):
        where = ", ".join(f"`{owner}` x{owners.count(owner)}" for owner in sorted(set(owners)))
        rows.append(f"| {FASTENER_NAMES.get(kind, kind)} | {material} | {len(owners)} | {where} |")
    return "\n".join(rows) + "\n"


def render_print_doc(text: str, built: list[printed_parts.Printed]) -> str:
    sys.path.insert(0, str(ROOT / "enclosure"))
    import fit_check
    return fit_check.render_document(text, {"printed-parts": parts_table(built), "hardware": hardware_table(built)},
                                     PRINT_DOC)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--params", type=Path, default=PARAMS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--no-export", action="store_true", help="check only")
    doc = parser.add_mutually_exclusive_group()
    doc.add_argument("--check-doc", action="store_true", help="fail if the print page's tables are stale")
    doc.add_argument("--write-doc", action="store_true", help="regenerate the print page's tables")
    arguments = parser.parse_args(argv)
    layout = model.load(arguments.params)
    layout_findings = checks.run(layout)
    built = printed_parts.build(layout) + printed_parts.templates(layout)
    findings = layout_findings + solid_check(layout, built)
    for finding in findings:
        print(finding, file=sys.stderr)
    if not arguments.no_export:
        export(layout, built, arguments.out)
    if arguments.check_doc or arguments.write_doc:
        current = PRINT_DOC.read_text(encoding="utf-8")
        rendered = render_print_doc(current, built)
        if arguments.write_doc:
            PRINT_DOC.write_text(rendered, encoding="utf-8")
        elif rendered != current:
            print("ENCLOSURE.PRINT_DOC_STALE docs/hardware/mirror-unit-print.md; run "
                  "make enclosure-doc", file=sys.stderr)
            findings.append(Finding("ENCLOSURE.PRINT_DOC_STALE", "mirror-unit-print.md", "stale"))
    facts = summary(layout, built)
    print(f"ENCLOSURE.SOLID_CHECK findings={len(findings)} parts={len(built)} "
          f"hanging_mass_g~{facts['mass_g_estimate']['hanging_total']} vhb_cm2~{facts['vhb_cm2_needed']}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
