# Enclosure Workspace

The mirror unit's enclosure: its layout parameters, the fit check, and (from
the [fit-check enclosure plan](../.claude/plans/2026-10-20-mirror-unit-fit-check-enclosure.md)'s
Step 3) the source CAD and its STEP, STL and 3MF exports. The design
requirements are in the [enclosure plan](../docs/hardware/enclosure-plan.md)
and the [mirror-unit packaging plan](../.claude/plans/2026-10-11-mirror-unit-packaging.md).

| Path | What it is |
|---|---|
| `params.toml` | Every part, the shell, its openings and every cable, each number with its provenance |
| `bhp_enclosure/` | The layout model, the fit check and the drawings (standard library only) |
| `fit_check.py` | Runs the check; `--write` regenerates the drawings and tables, `--check` fails when they are stale |
| `tests/` | Unit tests for the geometry, the check rules and the generator |

The drawings, the harness table and the caliper list are in
[docs/hardware/mirror-unit-layout.md](../docs/hardware/mirror-unit-layout.md).
`make verify` runs the tests and `fit_check.py --check`.

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
