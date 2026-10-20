# Mirror Unit: Printing and Assembling the Fit Check

<!-- @canonical mirror-unit-print -->

**Status (2026-10-20):** the first print, a fit check, made before anything
was measured. Its job is to show what binds, folds or misses on the real
parts, not to be the finished case. The layout, the harness and the caliper
list are in [mirror-unit-layout.md](mirror-unit-layout.md); the plan is the
[fit-check enclosure plan](../../.claude/plans/2026-10-20-mirror-unit-fit-check-enclosure.md).

## Getting the files

`make enclosure` builds every part and writes it to `.build/enclosure/out/`:
- `<part>.3mf` and `<part>.stl`, already turned onto the bed;
- `<part>.step`, in the unit's frame;
- `assembly.step`;
- `summary.json`, with each part's size, a mass estimate and the VHB area.

It needs the CAD venv, which `make enclosure-venv` creates from the
hash-locked `enclosure/requirements.txt`. CI builds the same files and keeps
them as the `mirror-unit-print` artifact of each run.

`make verify-enclosure` runs the solid check. It fails if a printed part
runs into a part, a plug, a keep-out or a cable, if two printed parts
collide, if a part misses the bed, or if a metal fastener sits near the
Wi-Fi antenna. `make verify` runs it whenever the CAD venv exists.

## Print the templates first

`frdm_template` and `panel_template` are 1.2 mm frames that take a few
minutes each. They check the guessed hole positions before about 10 hours
of plastic go into the large parts.
- **The board:** lay it on `frdm_template`, component side up, with J2
  (power) at the top. All four corner holes should line up. The notch on the
  left rail marks J15's middle; the one on the bottom rail marks J4's.
- **The panel:** lay `panel_template` on the panel's back. Each M3 standoff
  should drop into its 6.2 mm hole.

If either is off, measure the board (the caliper list's first items) and
send the numbers back before printing the rest.

## The parts

<!-- generated:printed-parts -->
| Part | What | On the bed | Footprint (mm) | Height (mm) |
|---|---|---|---|---|
| `front_shell` | Front shell: bezel, walls and mounts | face down: the bezel on the bed | 198.8 x 216.4 | 50.0 |
| `rear_plate` | Rear plate: back and cleat hook | inner face down, the cleat hook up | 193.2 x 210.8 | 7.6 |
| `mount_plate` | Mirror mounting plate with the cleat rail | back (the VHB face) down, the rail up | 190.0 x 193.6 | 7.5 |
| `panel_clamp_left` | Panel clamp, left | back down, pads up | 38.5 x 73.5 | 3.8 |
| `panel_clamp_right` | Panel clamp, right | back down, pads up | 36.4 x 73.5 | 3.8 |
| `cable_clamp` | Power-cable clamp | back down, channel up | 20.0 x 8.6 | 4.0 |
| `frdm_template` | FRDM hole template (print first) | flat | 120.3 x 130.3 | 1.2 |
| `panel_template` | Panel standoff template (print first) | flat | 165.0 x 100.0 | 1.2 |
<!-- /generated:printed-parts -->

PETG, 0.2 mm layers, 0.4 mm nozzle:
- **Front shell and rear plate:** 4 perimeters, so the heat-set inserts have
  material around them, and 25 % gyroid infill.
- **Everything else:** 3 perimeters and 20 % infill.

No part should need supports:
- the shell prints face-down;
- its inner bosses and wedges either grow from the bezel or lean on a wall
  at 45° or less;
- the slots in the walls bridge at most 13.3 mm.

Brim the shell and the plates if your bed lifts PETG corners.

## Hardware

<!-- generated:hardware -->
| Hardware | Material | Count | Where |
|---|---|---|---|
| M3 heat-set insert (for a 4.0 mm hole, 4-5 mm long) | brass | 18 | `front_shell` x18 |
| M2 self-tapping screw | steel | 4 | `front_shell` x4 |
| M3 screw | nylon | 4 | `panel_clamp_left` x2, `panel_clamp_right` x2 |
| M3 screw | steel | 26 | `cable_clamp` x2, `frdm_standoffs` x4, `front_shell` x6, `panel_clamp_left` x2, `panel_clamp_right` x2, `rear_plate` x10 |
| M3 x 24 hex standoff, female-female | brass | 4 | `frdm_standoffs` x4 |
<!-- /generated:hardware -->

Also:
- **Screws:**
  - M3 x 8 for the rim; countersunk M3 x 6 for the standoffs; M3 x 6
    elsewhere.
  - The clamp screws into the panel's standoffs are nylon M3 x 5, no longer:
    those standoffs are only 1.5 mm deep. The left pair sit in the Wi-Fi
    module's plastic-only zone, and the right pair match them.
- **Gasket:** 1 mm closed-cell foam tape, about 3 mm wide, for the bezel lip
  around the glass and a ring around each mic port.
- **VHB:** clear glass-grade tape (3M 4910 or 5952) for the mounting plate.
  `summary.json` gives the area for the estimated weight, with the
  enclosure plan's 3× margin. Weigh the real unit before bonding.

## Assembly order

1. **Inserts.** Press the heat-set inserts in with a soldering iron at about
   230 °C: the shell's rim, wedges, mic-strip posts, core-board posts and
   cable-clamp boss.
2. **Shell on a soft cloth, face down.**
   - Lay the foam gasket on the lip inside the window and a foam ring on
     each mic port.
   - Screw on the mic strip, plain side to the bezel.
3. **The panel.** Drop it into the pocket, then fit the two panel clamps:
   - into the panel's standoffs (nylon M3 on the left);
   - into the wall wedges (steel M3).
   - Snug, not tight: the foam only needs to close up.
4. **The chin.**
   - The XVF3800 core board goes on its four posts. Fit the 24-pin mic flex
     between it and the strip.
   - Then the PIR (two M2), the speaker (two M2) and the LED in its collar.
   - Plug the speaker leads into the core board.
5. **The board and the rear plate.**
   - Put the four standoffs on the rear plate (countersunk M3 from the
     back), then the board on them, component side down.
   - Plug in the J27 and J18 harnesses, the DSI flex at J15, and the gate
     board at J4.
6. **Close.**
   - Lay the rear plate onto the shell from the right: the DSI flex makes its
     U-turn at the right wall.
   - Check that nothing is pinched, then fit the six rim screws.
7. **Power.** Feed the USB-C cable through the bottom wall, then clamp it.
8. **Mounting.** Hang the unit on the mounting plate's rail on the bench
   first. Bond the plate to the mirror only after the packaging plan's
   Steps 5 and 6.

## What to report back

Say for each part whether it fits, binds or misses, and by how much:
- every cable: does it plug in without a fold or strain?
- the board's standoffs: do they meet its holes?
- the panel: does it sit flat on the gasket?
- the PIR dome: does it clear the window?
- the rear plate: does it close?
- the debug, microSD and privacy-switch openings: can you reach each one?

Photos with a ruler in them help.
