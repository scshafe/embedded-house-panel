# Plan: Package the FRDM Build as a Mirror-Mounted Unit

<!-- @canonical mirror-unit-packaging-plan -->

**Status (2026-10-11):** proposed; nothing below is built. The display has not
yet shown a frame and the microphone array is not ordered yet. The PIR is the
owner's Adafruit 189 (see "PIR").

**Goal:** turn the FRDM-IMX8MPLUS, the Waveshare 7inch DSI LCD (C), a Seeed
XVF3800 microphone array (the Flex Linear-4 variant, see "Which XVF3800"), a
PIR and a small speaker into one
serviceable unit on the bathroom mirror, with wall power, deep
suspend-to-RAM as the normal idle state, and a microphone that a compromised
Linux cannot turn on while the unit is deactivated.

**Out of scope:** the custom SOM/carrier product and its battery and MCU
(sketched at the end only); CAD geometry before the real stack is measured
([enclosure plan](../../docs/hardware/enclosure-plan.md), "Evidence needed
before CAD"); any owner-gated board action.

## Requirements

Stated by the owner on 2026-10-11, in addition to the
[enclosure plan](../../docs/hardware/enclosure-plan.md)'s 2026-09-24 mirror
requirement (front glass, centered at eye level, adhesive-only, no drilling):

- **R1. Voice is central.** Reliable speech pickup from a few feet away, in
  front of the mirror. Speech band first; a wider (singing) band if possible.
- **R2. Wall power.** A USB-C cable down the mirror; no battery in this stage.
- **R3. Deep suspend-to-RAM is a core feature,** for low power and so the unit
  is off when deactivated.
- **R4. Reliably off.** When the unit is deactivated, the microphone must be
  off in a way that holds even if Linux is compromised.
- **R5. A small speaker** for spoken replies and chimes. Compact matters more
  than quality.
- **R6. Fog tolerance if possible.** The current bathroom is well ventilated;
  a future one may not be.

Constraints already in the repository:

- Board input is 12–20 V USB-C PD at 45 W or more (UM12340). The PC's USB-C
  boots the board but is not that; the Apple charger reset-loops U-Boot. A
  working PD supply is a prerequisite (FRDM-GATE-POWER).
- `panel_supervisor` alone owns display and power policy; the media broker is
  the only microphone owner ([AGENTS.md](../../AGENTS.md)).
- The enclosure plan's design rules stand: splash target about IP44, vents on
  the top and bottom edges only, a plastic-only zone around the Wi-Fi module,
  a two-part mount (bonded plate plus lift-off unit), modular sensor bays.

## Which XVF3800

Both Seeed boards use the same XMOS XVF3800 processor (echo cancelling,
beamforming, noise suppression, de-reverberation, USB Audio Class 2.0). Their
shapes decide which fits the unit:

| | reSpeaker XVF3800 USB 4-Mic Array | reSpeaker Flex XVF3800 Linear-4 |
|---|---|---|
| Shape | one round board, Ø100 mm, 9.0 mm envelope | mic strip 110 × 11 mm (2.9 mm tall) plus core board 70.5 × 52.2 mm, joined by a 24-pin 0.5 mm FPC (20 cm included) |
| Mics | 4 × Infineon IM69D130 on a 66 mm square | 4 in a line, 33 mm pitch (99 mm span) |
| Host link | USB-C, UAC 2.0 | USB-C on the core board, UAC 2.0, or I2S |
| Speaker | on-board class-D amp (LM48511), JST PH 2-pin | on-board amp, upright JST; up to 10 W into 4 Ω with its 12 V terminal |
| Pickup | 360° | about 180°, front-facing, rear suppressed |
| Processing | AEC, AGC, DoA, beamforming, VAD, noise suppression, de-reverberation | the same set |
| USB firmware | 16 kHz 6-channel; 16 kHz or 48 kHz 2-channel | the same three, in linear builds |
| Lights and buttons | ring of 12 RGB LEDs, mute button with red LED | power LED, reset and boot buttons only |
| Price (2026-10-10) | $60.99 | $57.99 |

Sources: Seeed wikis (including the reSpeaker Flex introduction page for
pickup, firmware and controls) and 2D/STEP/DXF files for both boards, measured by a
research agent on 2026-10-11 (values from the vendor's CAD, not printed
drawings).

**Choice: the Flex Linear-4.** The panel's glass is 192.96 × 110.76 mm with
borders of 19 mm at the sides, 11 mm at the top and 13 mm at the bottom, and
the FRDM board (130 × 120 mm) is taller than the glass, so the enclosure has a
strip above or below the glass anyway. The 11 mm mic strip fits there, its
four mics in a line facing the person at the mirror. The round board would
need a 100 mm-wide, 100 mm-tall area beside the panel, or would sit behind it
with its mics ducted to the front. On a wall-mounted unit the rear half of a
circular array's 360° faces the wall and mirror, so the Linear-4's 180°
pattern loses nothing the unit needs. It drops the LED ring and the mute
button; R4 does not rely on either.

## What a compromised Linux can and cannot do (R4)

Anything Linux decides, a compromised Linux can undo: it can refuse to
suspend, re-enable a regulator, or ignore a mute request. The vendor DTB
already has the USB host's 5 V as a GPIO-switched regulator (`USB_VBUS`,
`regulator-always-on`), so software could switch the XVF3800's power for
saving energy, but that switch is not a privacy control. R4 needs the mic's
power to depend on something outside Linux:

1. **Manual privacy switch (always).** A slide switch in series with the
   XVF3800's USB 5 V. Off means no power to the array, whatever the software
   does. Reachable on the unit's edge.
2. **Presence gate (proposed).** The PIR's output, held high for a few
   minutes after the last motion, drives the enable pin of a load switch on
   the same 5 V line. With nobody in front of the mirror, the array has no
   power, and Linux has no path to change that. The PIR output also goes to a
   native SoC GPIO for wake, as today's plan has it. The Adafruit 189 can
   provide the hold itself (see "PIR"), so the gate board may need no timer.
3. **Hardware indicator.** An LED powered from the switched mic rail itself,
   not from a GPIO, so "lit" means "the mic has power" by construction.

Trade-off to measure before committing to (2): each time presence starts, the
XVF3800 powers up and re-enumerates on USB, which delays the first spoken
command. If that delay is too long, the hold time grows, or the gate cuts only
the mic strip's supply (its FPC carries it) instead of the whole board; that
needs the Linear-4's schematic.

What this does not cover: someone standing in front of the unit is, by
design, in range of the mic while the gate is open. The speaker is an output
only (an amplifier cannot record).

## Power and suspend (R2, R3)

| Load | Fed from | In deep suspend | Who switches it | Evidence still needed |
|---|---|---|---|---|
| FRDM board | J2, USB-C PD, wall supply | SoC in DSM | Linux | FRDM-GATE-POWER trace across states |
| Panel logic + backlight | J27 5 V (board rail) | backlight 0 over I2C; panel logic still powered | Linux (bridge register) | panel current dark vs lit; whether to add a load switch |
| XVF3800 core board, mics and speaker amp | USB-A host 5 V via privacy switch and presence gate | off unless someone is present and the switch is on | hardware (R4) | power-up-to-ready time; current (not published) |
| PIR (Adafruit 189) | 5 V from J18 (its own 3.3 V regulator) | on (it is the wake source) | none | PIR current (not published); wake reliability |

- Panel: Waveshare's FAQ gives about 0.5 A lit and 0.1 A with the backlight
  off (about 0.5 W dark, unmeasured here). If that holds, the panel alone
  dominates suspend power; add a GPIO-driven load switch on its 5 V and
  re-initialise the bridge on resume, but only after measuring.
- Target to measure, not a promise: the suspend power of the whole unit at the
  wall, with the wall-plug supply's own idle draw included.

## Audio layout (R1, R5, R6)

- **Mic:** the XVF3800 does the echo cancelling, beamforming, noise
  suppression and de-reverberation on the board. Put the Linear-4 mic strip
  in the bezel strip above the glass, each mic aligned to its own acoustic
  port facing the room and sealed to it with a gasket, so air inside the
  enclosure does not leak into the ports. Keep it as far from the speaker as
  the bezel allows.
- **Speaker:** a compact micro speaker in its own sealed back volume, plugged
  into the XVF3800's on-board amplifier. Play audio through the XVF3800 over
  USB: XMOS takes the echo canceller's reference from the host's USB playback,
  so spoken replies do not trigger the recogniser. Point the speaker into the
  room from the bottom strip, the opposite edge from the mics. Pick its
  impedance and power to suit the amplifier at 5 V (check the amplifier's
  datasheet, not the 12 V figure).
- **Consequence of the presence gate:** the speaker is powered with the array,
  so the unit cannot make a sound while nobody is in front of it. A chime for
  an empty room would need a separate speaker path (for example the WM8962
  codec's output), and that path gives the echo canceller no reference.
- **The XVF3800's own mute button and red LED** are firmware features of a
  board the host can talk to, so they do not meet R4; the hardware gate does.
- **Fog (R6):** each acoustic port gets a hydrophobic acoustic membrane vent
  (the membrane passes sound, not water). The XVF3800's mics are not rated for
  humidity; the membranes protect them. If a foggy bathroom later proves too
  much, the fallback is Infineon IM73D122 sealed MEMS mics (IP57) on the SoC's
  PDM input, which needs the J18 pin question settled and software DSP.
- **Band:** 16 kHz capture for speech recognition; the XVF3800's 48 kHz
  2-channel firmware for a wider band. Pick per use; test both.

## PIR

The owner has an Adafruit 189 PIR module (product page and Adafruit's PIR
guide, read 2026-10-11):

- BISS0001 controller with a 3.3 V regulator. Supply 5–12 V ("5V is ideal");
  output high is 3.3 V, low when idle. That suits a 3.3 V SoC pad, once the
  pad bank's voltage is measured (ARRIVAL-08).
- Range up to about 6–7 m, a 110° × 70° pattern. 30–60 s to settle after
  power-up.
- Retrigger jumper: `H` keeps the output high while motion continues; `L`
  does not. The `TIME` pot sets the hold, from about 2.5 s to about 250 s
  (Adafruit's formula for this board). In `H` with a few minutes' hold, the
  module is the presence gate's timer. The same hold then delays Linux's view
  of absence by that long; if that matters, keep a separate timer on the gate
  board.
- 32.34 × 24.03 mm, 24.66 mm tall with the lens; M2 holes 28 mm apart; a
  30 cm cable with a 3-pin socket.
- Adafruit warns that PIRs can false-trigger near a Raspberry Pi 3 (its Wi-Fi
  is the usual suspect). Keep it away from the FRDM board's Wi-Fi module.

The lens dome is wider than the strips around the glass, so it needs its own
area outside the glass: a bottom "chin" (with the speaker) is the first
candidate.

## Physical stack (front to back)

1. Bezel with a gasket against the panel's cover glass; mic ports and
   membranes in the top strip; speaker grille in the bottom strip; PIR window;
   LED window.
2. Waveshare panel, held by the bezel and the mid-frame; touch side free.
3. Mid-frame carrying the XVF3800 core board, the speaker in its back
   volume, the small privacy/presence-gate board and the harness hub.
4. FRDM board on the removable rear plate, turned so the 12 cm DSI FPC
   reaches J15 without a fold (decide in the Step 4 mockup); heat path to the
   rear plate and vents.
5. Rear plate hooks onto the mirror-bonded mounting plate (enclosure plan).

Dimensions from the research report are entered in the table below; CAD still
waits for measuring the real stack.

| Part | Size | Depth from the glass front | Notes |
|---|---|---|---|
| Panel glass | 192.96 × 110.76 mm | glass front = 0 | toughened, 6H; active area 154.88 × 86.72 mm |
| Panel PCB / LCD module | 165.0 × 100.0 / 164.0 × 97.6 mm | PCB back at 8.0 mm; back parts to 10.5; 2×3 header tips at 17.0 | M3 case standoffs on a 126.3 × 65.5 mm pattern; M2.5 Pi standoffs on 58.0 × 49.0 mm |
| FRDM-IMX8MPLUS | 130 × 120 mm, 1.6 mm PCB | behind the panel | hole positions and tallest part not public (NXP design files need an account); UM12340 Figure 2 shows a heatsink on the SoC and a 2-pin fan connector |
| Flex Linear-4 mic strip | 110 × 11 × 2.9 mm | in the bezel strip | M3 holes 62.5 mm apart; check the FPC position (the DXF and STEP disagree) |
| Flex core board | 70.5 × 52.2 mm, parts about 7.7 mm | mid-frame | M3 on a 62.5 × 44.2 mm pattern |

Sources: Waveshare's outline drawing and STEP model; UM12340 Rev 1.0; Seeed's
files above. The panel's STEP model reuses an 800×480 touch-panel part, so
check it against the real panel.

First estimate, to be replaced by measurement: an outline of about
200 × 160 mm (the glass plus side walls; the glass, the mic strip above it
and a chin of about 30 mm for the PIR and speaker below it set the height,
which also clears the FRDM board's 120 mm) and a depth of roughly 35–45 mm (the panel's
17 mm, the FRDM board with its heatsink, and air gaps). The panel is listed at
0.324 kg (Waveshare does not say whether net or shipping); the FRDM board's
weight is unpublished. Weigh the parts in Step 4; the bonded area follows from
the enclosure plan's 3× rule.

Service access at the bottom edge: USB-C power in (J2), the debug console
(J19), the privacy switch, and a removable cover over the boot switch and
microSD. Ethernet is for the bench only.

## Harness

| From | To | Cable | Notes |
|---|---|---|---|
| J15 (22-pin) | panel 15-pin | 12 cm DSI FPC from the kit | contacts toward the springs at both ends |
| J27-9 / -7 / -6 / -2 | panel SDA1 / SCL1 / GND / 5V | 4 single wires, then a keyed JST harness | B1 pin numbers; work on this B3 board (2026-10-18, silkscreen "9" and "2" read) |
| USB-A host | XVF3800 core board USB-C | short USB cable with its 5 V line broken out through the gate board | gate board: privacy switch, load switch, hold timer, LED |
| XVF3800 core board | mic strip | 24-pin 0.5 mm FPC (20 cm included) | fold-free route to the top strip |
| XVF3800 speaker connector | micro speaker | 2 wires (JST) | |
| J18 5 V, GND, native GPIO | PIR (Adafruit 189: VCC, GND, OUT at 3.3 V); OUT also to the gate board's enable input | keyed JST | PIR pin per FRDM-GATE-PIR; nothing on J18 pins 3/5 until the B3 schematic |
| J2 | wall PD supply | USB-C cable down the mirror edge, drip loop | GFCI outlet |

## Steps

- [ ] **Step 1: Bring the parts up on the bench, unpackaged.**
  - Display (J27 wiring), touch and backlight gates; FRDM-GATE-POWER with a
    working PD supply and the inline meter.
  - XVF3800: enumerate on the vendor image (read-only), capture at 16 and
    48 kHz, play through it, record from where you stand at the mirror with
    the fan and water running; hold the mic strip where the bezel will put it.
  - Verify: gate records for DISPLAY/TOUCH/BACKLIGHT/POWER; a recording set
    under `hardware_tests/results/`.
- [ ] **Step 2: Prove suspend with every part attached.**
  - Suspend/resume with panel, XVF3800, speaker and PIR attached; PIR wake.
  - Measure wall power in each state; decide on the panel load switch.
  - Verify: attributed PIR wakes per the bake-off contract; a power table.
- [ ] **Step 3: Build the gate board.**
  - Privacy switch, load switch, PIR hold timer, mic-rail LED; on perfboard
    first.
  - Verify: with Linux running and the switch on, the array is powered only
    while the PIR holds; with the switch off, never; the LED follows the rail;
    the time from presence to "ready for a command" is recorded.
- [ ] **Step 4: Measure the real stack and mock it up.**
  - 2026-10-20: the owner asked for the printed case first; the
    [fit-check enclosure plan](2026-10-20-mirror-unit-fit-check-enclosure.md)
    designs it from vendor data, flagged, and its Step 5 brings back the
    calipers.
  - Calipers on every part; cardboard/foam mockup; cable routing; weight.
  - Verify: `enclosure/params.toml` holds measured values with photos.
- [ ] **Step 5: First printed enclosure, on the bench.**
  - Thermal soak closed (SoC temperature under load), acoustic test through
    the ports and membranes, speaker echo test.
  - Verify: no thermal throttling in the closed box over a set soak; speech
    recognition from the mirror position no worse than the open-board result.
- [ ] **Step 6: Mount on the mirror.**
  - Mounting plate bonded per the enclosure plan (72 h cure), unit hung,
    shower steam test for PIR false wakes and condensation.
  - Verify: a week of normal use with the suspend/wake and voice logs.

## Open questions

- The PIR's J18 pin and that pad bank's voltage (FRDM-GATE-PIR); its output
  must drive both a GPIO and the gate board's enable input, and its current
  draw is not published.
- J18 pins 3/5 on B3 (schematic). J27 works as wired (2026-10-18).
- XVF3800 power-up-to-ready time and current draw (neither published).
- FRDM-IMX8MPLUS hole positions, tallest part and heatsink (NXP design files,
  or calipers); thermal behaviour when closed in a bathroom.
- Whether the panel needs a load switch for suspend power.
- A PD wall supply that boots the board.

## Later: the custom product

ADR 0008 expects an i.MX 8M Plus SOM on a purpose-built carrier. The
[power-domain architecture](../../docs/architecture/power-domain-architecture.md)
already puts an always-on MCU in charge of the application domain's power.
On that carrier the presence gate and privacy switch become carrier
circuits, the MCU (not Linux) owns the microphone rail, and the XVF3800 can
become its chip-down equivalent or sealed PDM mics. That stage gets its own
plan once this one's measurements exist.
