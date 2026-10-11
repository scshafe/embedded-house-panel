# Enclosure Intention

The working electronics should eventually become a neat, serviceable 3D-printed product rather than a loose board stack.

## Future deliverable

A parameterized CAD model and printable files for:

- FRDM i.MX 8M Plus board (130 × 120 mm) mounting bosses on a removable back plate;
- flush bezel for the Waveshare 7inch DSI LCD (C) with touch clearance and a
  straight, fold-free DSI cable path to J15;
- PIR aperture and adjustable sensor angle;
- USB-C PD power entry (45–65 W charger) with a strain-relief clamp, plus
  Ethernet/USB, microSD, and debug access from the bottom edge;
- strain relief, ventilation, and guarded reset/power controls;
- mounting on the front glass of a bathroom mirror, centered at eye level,
  through an adhesive-bonded mounting plate and a lift-off unit (see
  [Mirror mounting and bathroom environment](#mirror-mounting-and-bathroom-environment));
- bottom-intake and top-exhaust vents for vertical wall orientation, and a
  plastic-only zone around the Wi-Fi module;
- removable rear panel so the board can be serviced without disturbing the screen.

## Owner requirements, 2026-10-11

The owner added these on 2026-10-11. The
[mirror-unit packaging plan](../../.claude/plans/2026-10-11-mirror-unit-packaging.md)
turns them and this page into a build sequence for the FRDM stage.

- Voice is central: reliable speech pickup from a few feet in front of the
  mirror. Speech band first; a wider (singing) band if possible.
- Wall power for now. Deep suspend-to-RAM is the normal idle state, both for
  low power and so the unit is off when deactivated.
- When deactivated, the microphone must be off in a way that holds even if
  Linux is compromised. That means a hardware power cut, not a software mute.
- A small, compact speaker for spoken replies and chimes; quality is
  secondary.
- Tolerate fog if possible: the current bathroom is well ventilated, but a
  future one may not be.

## Mirror mounting and bathroom environment

**Requirement (stated 2026-09-24):** the panel mounts on the user's bathroom
mirror, centered on eye level. This is a fixed requirement. The prototype uses
wall power.

### Attachment

- Do not drill the mirror. Most bathroom mirrors cannot be drilled safely, and
  tempered glass shatters if drilled. Attachment is adhesive-first.
- **Primary bond:** 3M VHB tape (a clear glass-grade tape such as 4910 or
  5952) between the mirror's front glass and a thin mounting plate. The mirror
  silvering is behind the glass, so the bond is glass-to-plate.
- **Sizing:** 3M's static-load guideline is about 57 cm² of tape per kg
  (4 in² per lb). Use at least 3× that area to cover humidity, heat cycling,
  and the peel moment from a deep enclosure. Weigh the finished unit and record
  the bonded area in `enclosure/params.yaml`.
- **Application:** clean with 50:50 isopropyl alcohol and water; apply at
  21–38 °C; press at about 15 psi (J-roller); wait 72 hours for full bond
  strength before hanging the unit.
- **Two-part mount:** the mounting plate stays bonded to the mirror. The unit
  slides or hooks onto the plate and locks with a screw or magnets, so service
  never loads or breaks the adhesive.
- **Peel control:** VHB is strong in shear and weak in peel. Keep the center of
  mass close to the glass (board on the rear plate, display in front), make the
  plate as tall as the unit, and give it a bottom lip that carries the unit's
  weight.
- Suction cups are not a primary mount; they leak over time with temperature
  cycling. Duct tape is an emergency fallback only; humidity weakens it and it
  leaves residue.
- Removal later: slide fishing line or floss behind the plate, warm it with a
  hair dryer, then clean the glass with a razor scraper.

### Humidity and water

- Target splash resistance (roughly IP44). Treat steam and condensation as
  normal operating conditions.
- Conformal-coat the sensor and harness boards where practical. Put a gasket
  between bezel and display glass.
- Vents cannot face the mirror. Use labyrinth or louvered vents on the bottom
  and top edges, with a drip edge above the top vents. Verify thermals with the
  enclosure closed.
- Warm, moving air after a shower can trigger the PIR falsely. Test false wakes
  with shower steam as part of the PIR field-of-view test.

### Power and cable routing

- Plug the USB-C PD charger into a GFCI-protected outlet away from splash.
  Only low-voltage USB-C reaches the mirror.
- Route the cable down the mirror edge in clear adhesive clips or a slim
  raceway, with a drip loop below the unit's cable entry.

### Privacy

- A bathroom camera is a privacy risk. If a camera module is ever fitted, it
  needs a physical shutter and a hardware power cut, and raw video must never
  leave the device.

## Modular sensor bays

**Requirement (stated 2026-09-24):** the motion sensor, and optionally the
microphone and camera, must be positionable around the screen. Their placement
is a per-installation configuration, not a fixed part of the enclosure.

Design rules:

- The bezel perimeter exposes a fixed set of identical **bay openings**, for
  example top-center, bottom-center, and each corner/side position. Every bay
  accepts any module insert or a blank cover.
- Each **module insert** carries its part (PIR plus lens, microphone plus
  acoustic port and gasket, or camera plus privacy shutter) and presents one
  keyed connector (for example JST-SH) at a common location on the insert.
- A **perimeter cable channel** behind the bezel routes a harness from every
  bay to a small hub near the board header, so moving a module never needs
  re-soldering.
- The CAD model is parameterized by a bay list in `enclosure/params.yaml`
  (position, module type, tilt), so an agent can regenerate the bezel and
  harness lengths from configuration.
- Per-module constraints limit which bays are valid:
  - **PIR:** unobstructed field of view with an adjustable downward tilt;
    away from vents and warm air.
  - **Microphone:** away from the speaker, vents, and any fan; use a digital
    (PDM/I2S) or short shielded analog run to limit noise pickup.
  - **Camera:** MIPI-CSI ribbon length is short, so camera bays must sit near
    the board's CSI connector; top-center is preferred for framing.
- Software already treats presence as a replaceable `PresenceSource`. Module
  position must not change device-tree pin assignments; any position-dependent
  setting (camera rotation, PIR aim notes) belongs in configuration.

## Evidence needed before CAD

Record board/display/sensor revisions, assembled bounding box, connector keep-outs, standoff sizes, cable bend radii, PIR lens field of view, display viewing angle, temperatures in each power state, and whether the board requires airflow. Photograph a ruler/caliper reference with every measurement.

## Prototype sequence

1. Cardboard/foam fit mockup and cable-routing check.
2. Low-detail printed chassis with oversized openings.
3. Thermal and PIR field-of-view test in the intended location.
4. Refined bezel, fasteners, strain relief, and mounting option.
5. Publish source CAD, exported STEP, printable 3MF/STL, bill of materials, print orientation, material, and assembly instructions.

No enclosure geometry should be guessed from online drawings when the actual stacked assembly can be measured.

**Amended 2026-09-24:** retargeted from the lost BeagleBone Black and 4D cape
to the FRDM i.MX 8M Plus board and Waveshare 7inch DSI LCD (C) per
[ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md). Waveshare does not publish the
display's mechanical dimensions in text, so the measure-before-CAD rule still
applies.
