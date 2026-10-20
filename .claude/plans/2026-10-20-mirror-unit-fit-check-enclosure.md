# Plan: The Mirror Unit's First Printed Enclosure (Fit Check)

<!-- @canonical mirror-unit-fit-check-enclosure-plan -->

**Status (2026-10-20):** in progress; Steps 1 and 2 are done. The owner asked for a 3D-printable case for
the prototype, with the cables and the fit of every part accounted for in the
drawings. This plan carries out Steps 4 and 5 of the
[mirror-unit packaging plan](2026-10-11-mirror-unit-packaging.md) in the
order the owner chose: design first from vendor data, flagged, then calipers.

**Goal:** a parametric, regenerable enclosure for the FRDM board, the
Waveshare 7inch DSI LCD (C), the reSpeaker Flex XVF3800 Linear-4, the
Adafruit 189 PIR, a micro speaker and the privacy/presence gate board. A
script checks that the parts, plugs and cables fit together. The first print
is a loose fit check that the owner assembles on the bench.

**Out of scope:** the final bezel styling, the custom carrier product, and
anything owner-gated on the board. Thermal soak, acoustic tests and mirror
mounting stay in the packaging plan's Steps 5 and 6.

## Decisions

1. **Start before calipers, and flag it (owner, 2026-10-20).** This amends
   the enclosure README's "no guessed geometry" rule for the fit-check stage
   only. Every dimension in `enclosure/params.toml` carries a status:
   `measured`, `vendor`, `estimate` (a photo or indirect source),
   `placeholder` (the part is not chosen or built) or `design` (our choice).
   The drawings stroke `estimate` dashed and `placeholder` dotted. The
   generated caliper list names every number not yet measured. A value
   becomes `measured` only with calipers and a photo under
   `hardware_tests/results/`.
2. **build123d for the solids (owner, 2026-10-20).** It is Python on the
   OpenCascade kernel and exports STEP, STL and 3MF. It runs in a
   hash-locked venv under `.build/enclosure/`, created from
   `enclosure/requirements.txt`. Nothing is installed system-wide.
3. **A 256 × 256 mm bed and PETG (owner, 2026-10-20).** PETG, not PLA, for
   a warm, humid room. Every printed part must fit the bed with a 5 mm
   margin.
4. **"Schematics" means the harness plus the layout (owner, 2026-10-20).**
   Every cable has its two connectors, its mated plug body, its section, its
   minimum bend radius, its length and its route through the unit. The fit
   check verifies all of these against the 3D layout. The gate board's
   electrical schematic waits for the packaging plan's Step 3.
5. **Two layers of checks (taken).**
   - The layout and harness check uses only the standard library and runs in
     `make verify` on every host. It covers parts as boxes, plugs, cable
     routes, keep-outs, openings, the radio zone and acoustic distance.
   - The solid model (build123d) runs where its venv exists, and CI creates
     one. It checks the printed parts against the same boxes, plus the bed
     size and minimum walls.
6. **Parameters in TOML, not YAML (taken).** Python's standard library reads
   TOML, so the check needs no package. The enclosure plan's
   `enclosure/params.yaml` becomes `enclosure/params.toml`.
7. **The layout (taken in Step 2; the numbers are in `params.toml`).**
   - **The DSI flex fixes the board's height.** A flex bends only about its
     width; it cannot twist or fold. So J15 must sit level with the panel's
     DSI connector (y = 3.09). J15 is 113.7 mm from the board's I/O edge, so
     the board hangs about 55 mm below the glass, and the unit gets a chin.
     The outline is 198.8 × 216.4 × 50 mm, and the bed takes it.
   - **Components face the rear plate.** The board is turned 180° about the
     vertical, so J15's flex enters from the right. The flex leaves the
     panel toward +x, makes a U-turn at the right wall, and comes back into
     J15. Its 12 cm length sets the board at x = −72 … 48.
   - **Depth.** The board's underside clears the panel's M2.5 standoffs
     (14 mm deep) by 1 mm over 2.5 mm pin tails, so the PCB sits at
     z = −18.4 … −20. The rear sits at z = −44, set by the J18 and J27
     Dupont housings (14 mm) and their wires. The heatsink's height is the
     largest unknown.
   - **The I/O edge is at the bottom.**
     - The debug port (J19) takes a plug through the bottom wall.
     - The USB-A port takes the gate board directly, so the privacy switch
       sits in the bottom wall and the gate board is the harness hub for
       the PIR.
   - **Power.** J2 points up behind the panel. The straight USB-C plug's
     cable loops left over the board and runs down the left side to a clamp
     in the bottom wall.
   - **Microphones and the rest of the chin.**
     - The mic strip sits centred above the glass, plain side to the room.
     - The XVF3800 core board is centred in the chin, so the 24-pin flex
       runs straight up behind the panel at z = −10.9.
     - The PIR is on the chin's right, with nothing behind it; the speaker
       is centre-right and the LED on the left.
   - **The panel keeps its native orientation** (the wider border at the
     bottom), so no display rotation is needed once it is in the case.

## Steps

- [x] **Step 1: The parameter model and the fit check.** (2026-10-20)
  - `enclosure/bhp_enclosure/`: the geometry, the model loaded from
    `params.toml` with provenance, the fit check, the drawings and the
    tables.
  - `enclosure/fit_check.py` runs the check and keeps
    `docs/hardware/mirror-unit/*.svg` and the generated blocks of
    `docs/hardware/mirror-unit-layout.md` current. `make verify` runs it
    with `--check`.
  - Done: 31 unit tests cover poses, box tests, bent route lengths, every
    check rule (overlap and clearance, the shell and its openings,
    keep-outs, metal near the antenna, access paths, hole placement, cable
    ends and exit direction, straight runs, bend radius, flex twist, length,
    collisions, leaving the shell, plug bodies) and the generator.
  - Verify:
    - `python3 -m unittest discover -s enclosure/tests` exit 0;
    - `python3 enclosure/fit_check.py --check` exit 0;
    - `make verify` exit 0.
- [x] **Step 2: The parts, the harness and the layout.** (2026-10-20)
  - Enter each part from its vendor data, with sources: the panel, the FRDM
    board (NXP's manual; hole positions estimated from its photo), the
    XVF3800 core board and mic strip, the PIR, a placeholder speaker and
    gate board.
  - Enter every cable: the DSI FPC, the J27 wires, the USB from the host
    port through the gate board to the XVF3800, the mic-strip FPC, the
    speaker leads, the PIR lead and the USB-C PD power cable.
  - Choose the layout (Decision 7) so the check passes. Write the
    harness/layout page, with its drawings and the caliper list.
  - Done: eight parts and nine cables pass with no finding. The outline is
    198.8 x 216.4 x 50 mm. 55 values are vendor, 54 estimates and 13
    placeholders; none is measured yet.
  - Verify:
    - `python3 enclosure/fit_check.py --check` exit 0;
    - the harness table lists every cable with its route length and bends;
    - `make verify` exit 0.
- [ ] **Step 3: The printable parts in build123d.**
  - The front shell (bezel with the glass pocket and lip, mic ports, chin
    with the PIR window, speaker grille and LED window, and vents only on
    the top and bottom edges), the panel frame, the rear plate carrying the
    board, the mirror mounting plate, and the small parts (PIR tilt inserts,
    cable clamps, the microSD and boot-switch cover).
  - `make enclosure` writes STEP, STL and 3MF to `.build/enclosure/out/`.
    The solid check fails if a printed part intersects a part box or a
    cable sweep, or exceeds the bed.
  - CI builds the venv and runs the solid check.
  - Verify:
    - `make verify-enclosure` exit 0;
    - `make verify` exit 0.
- [ ] **Step 4: The fit-check print package.**
  - Fast 1:1 hole templates for the board and the panel (one or two
    layers), so the hole estimates are checked before the large parts are
    printed.
  - The full set of parts with loose fits, the print orientation and
    settings, the hardware list (screws, heat-set inserts, gasket, VHB), and
    the assembly order.
  - Verify:
    - `make verify-enclosure` exit 0;
    - the page lists every printed part with its orientation and the bed fit.
- [ ] **Step 5: The owner checks the templates and measures (owner).**
  - Lay the board and the panel on the templates; take the caliper list.
  - Verify (owner): the caliper values and photos are under
    `hardware_tests/results/`, and `params.toml` marks them `measured`.
- [ ] **Step 6: The owner prints and assembles the fit check (owner).**
  - Assemble with every cable plugged in; note anything that binds, folds or
    does not reach.
  - Verify (owner): every cable plugs in without a fold or strain, the rear
    plate closes, and the unit hangs on its mounting plate on the bench.

## Open questions

- The heatsink's height and the tallest parts on the FRDM board (no public
  drawing). These set the unit's depth; the estimate is on the caliper list.
- The speaker and the gate board are not chosen or built; their boxes are
  placeholders.
- Whether J27's Dupont housings should become right-angle connectors to
  save depth (decided in Step 2 from the check).
