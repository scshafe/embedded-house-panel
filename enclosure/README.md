# Enclosure Workspace

The mirror unit's enclosure: its layout parameters, the fit check, and the
source CAD with its STEP, STL and 3MF exports (the
[fit-check enclosure plan](../.claude/plans/2026-10-20-mirror-unit-fit-check-enclosure.md)). The design
requirements are in the [enclosure plan](../docs/hardware/enclosure-plan.md)
and the [mirror-unit packaging plan](../.claude/plans/2026-10-11-mirror-unit-packaging.md).

| Path | What it is |
|---|---|
| `params.toml` | Every part, the shell, its openings and every cable, each number with its provenance |
| `bhp_enclosure/` | The layout model, the fit check and the drawings (standard library only) |
| `fit_check.py` | Runs the check; `--write` regenerates the drawings and tables, `--check` fails when they are stale |
| `tests/` | Unit tests for the geometry, the check rules and the generator |
| `cad/parts.py` | The printed parts in build123d, built in the unit frame from `params.toml` |
| `cad/build_enclosure.py` | The solid check and the export (STEP, STL, 3MF, `summary.json`) |
| `cad/tests/` | Tests for the printed parts and the solid check |
| `requirements.in`, `requirements.txt` | build123d and its hash-locked closure for the CAD venv |

The drawings, the harness table and the caliper list are in
[docs/hardware/mirror-unit-layout.md](../docs/hardware/mirror-unit-layout.md).
How to print and assemble the parts is in
[docs/hardware/mirror-unit-print.md](../docs/hardware/mirror-unit-print.md).

| Command | What it does |
|---|---|
| `make verify` | The layout tests and `fit_check.py --check`; also the solid check when the CAD venv exists |
| `make enclosure-venv` | Creates the CAD venv in `.build/enclosure/venv` from the hash-locked requirements |
| `make verify-enclosure` | The solid check, the CAD tests and the print page's tables |
| `make enclosure` | Writes the parts to `.build/enclosure/out/` |
| `make enclosure-doc` | Regenerates the print page's tables |

## Geometry before calipers

**Amended 2026-10-20 (owner):** the fit-check stage may use vendor
dimensions and estimates before the parts are measured, as long as each
number says so.
- Every item in `params.toml` has a status: `measured`, `vendor`,
  `estimate`, `placeholder` or `design`.
- The drawings stroke `estimate` dashed and `placeholder` dotted.
- The caliper list names everything not yet measured.

A number becomes `measured` only with calipers and a photo under
`hardware_tests/results/`. The first print is a loose fit check, not a
product.

**Amended 2026-09-25:** replaced the BeagleBone board/cape stack with the FRDM
board and Waveshare DSI panel as the measurement subject.
