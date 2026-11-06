# Mirror Unit: Speaker and Gate Board

<!-- @canonical mirror-unit-speaker-and-gate -->

**Status (2026-11-06):** designed, not bought or built. This document chooses the
speaker (requirement R5) and designs the privacy/presence gate board (step 3) of
the [mirror-unit packaging plan](../../.claude/plans/2026-10-11-mirror-unit-packaging.md).
Every value comes from a vendor datasheet or is marked as a design value, an
inference, or **provisional**. Provisional values wait for:
- the PIR meter reading (ARRIVAL-08 in the [bring-up](../operations/frdm-bringup.md));
- the step 3 bench measurements below.

The owner places the orders. Nothing here installs software, flashes the board
or writes to it.

## Decisions

- **Speaker:** PUI Audio AS04008CO-2-LW100-R (8 Ω, 1.5 W rated, 40 × 20 × 8 mm),
  $3.59 at Digi-Key. Runner-up: Same Sky CMS-402008-18SP. Higher-quality
  drivers are compared in
  [mirror-unit-speaker-options.md](mirror-unit-speaker-options.md). This stays
  the baseline unless the owner picks another.
- **Gate:** the privacy switch and the presence gate are wired in series on the
  array's USB 5 V:
  - a mechanical slide switch;
  - then a current-limited load switch, a Diodes AP22653, enabled only by the
    PIR's output;
  - a red LED powered from the switched output.
- **Linux cannot raise the enable line.** The SoC's wake GPIO reads the PIR
  through a diode, so driving that pin cannot reach the gate.
- **No hold timer.** The PIR's own retrigger hold is the gate's timer. A timer
  is added only if step 3 shows the hold is too short.
- **Build form:**
  - Step 3, on the bench: perfboard with pigtail leads, about $12 of parts.
  - The enclosure: a small fabbed PCB, laid out once step 3's measurements fix
    the values, about $5 for three boards plus about $1 more in parts.

## Speaker

### What the amplifier gives at 5 V

- **The amplifier IC.** The reSpeaker Flex XVF3800 core board's speaker amplifier
  is a TI TPA3139D2, a 2-channel class-D part. Seeed names it in
  [the Flex introduction](https://wiki.seeedstudio.com/respeaker_flex_introduction/)
  and in the
  [Flex datasheet](https://files.seeedstudio.com/Bazaar/product_pdf/100099135.pdf),
  p. 5 (sha256 `9e90544c…a522`).
- **Its power inputs.** The same datasheet (p. 6) lists two inputs:
  - "5V Input: Via USB Type-C or 5V JST Connector";
  - "12V Input: Via 2-Pin Terminal".
- **What is not published.** Seeed publishes no schematic. Which rail feeds the
  amplifier, its gain and how its output is wired are all unknown. Seeed's
  "up to 10 W" figure is for 12 V.

From TI's [TPA3139D2 datasheet](https://www.ti.com/lit/ds/symlink/tpa3139d2.pdf)
(SLOS810A, sha256 `7232ca25…1a67`):

| Supply, load, output wiring | 1 % THD+N | 10 % THD+N | Where |
|---|---|---|---|
| 5 V, 8 Ω, one bridge (BTL) | about 1.4 W | about 1.7 W | read from Figure 6-18 |
| 5 V, 4 Ω, one bridge (BTL) | about 2.3 W | about 2.9 W | read from Figure 6-19 |
| 5 V, 4 Ω, two bridges in parallel (PBTL) | 3.0 W | 3.5 W | §6.5 table |
| 7.4 V, 8 Ω, BTL | 3.0 W | 3.8 W | §6.5 table |

- **Supply range:** 3.5–14.4 V, with a minimum load of 3.2 Ω for BTL below 10 V
  (§6.3).
- **Supply current:** the idle current at 5 V is about 22 mA (Figure 6-23). At
  85–90 % efficiency, 1.7 W into 8 Ω takes about 0.4 A from the 5 V rail. 2.9 W
  into 4 Ω takes about 0.65 A. Both of these are inferences, on top of the
  board's own draw, which is not published.

**Risk: the amplifier may be silent on USB power.** The `MODE_SEL` pin chooses
the undervoltage threshold (pin table, p. 4):
- low gives the BD mode, which shuts the amplifier off below 7.5 V;
- high or floating gives the 1SPW mode, which shuts it off below 3.4 V.

Seeed does not say how the pin is set. The first bench test answers it: play a
tone through the array on USB 5 V with the speaker attached (step 3 below). If
the amplifier is silent, the speaker still fits. The fallback is a small mono
class-D amplifier breakout on the gate's switched 5 V, fed from the array's
3.5 mm output.
- It keeps the echo canceller's reference, because playback still goes through
  the array.
- It keeps the privacy property, because it is powered only when the array is.
- It is not chosen or priced here.

Feeding the array 12 V instead is not an option. Seeed says the 12 V terminal
"supplies 12V to the whole system", which would power the microphones around
the gate.

### The choice: PUI Audio AS04008CO-2-LW100-R

From [PUI's drawing](https://puiaudio.com/file/specs-AS04008CO-2-LW100-R.pdf)
(sha256 `2112ccaa…71b5`):

| | |
|---|---|
| Size | 40 × 20 mm frame, 8 mm deep (magnet Ø14.8 mm, frame 2.8 mm); four Ø2.2 mm holes on 36 × 16 mm |
| Impedance | 8 Ω ± 15 % |
| Power | 1.5 W rated, 3 W maximum |
| Output | 84 ± 3 dBA at 1 W / 0.5 m |
| Resonance | 550 Hz ± 20 %; range 200 Hz–20 kHz |
| Leads | 100 mm UL1007 24 AWG, stripped |
| Mass | 7.8 g |
| Price | $3.59 at qty 1, [Digi-Key](https://www.digikey.com/en/products/detail/pui-audio-inc/AS04008CO-2-LW100-R/11618410), 2026-11-06 (20 in stock, 27-week lead time after that) |

Why this one:
- **Impedance.** 8 Ω roughly halves the current drawn from the FRDM's USB port
  compared with 4 Ω. The port's current limit is not published, so this is the
  margin worth keeping.
- **Power.** The 1.5 W rating covers the amplifier's 1.4 W at 1 % THD+N into
  8 Ω at 5 V. The 3 W maximum covers clipping. No software volume cap is
  needed.
- **Fit.** The 40 × 20 mm frame fits the chin, which is about 30 mm tall, in
  place of the layout's 35.9 × 18.5 mm placeholder.
- **Resonance.** It resonates at about 550 Hz, against about 1 kHz for the
  placeholder (Same Sky CES-361810-18PM), which is 1 W rated and comes in its
  own fixed box.
- **Back volume.** The frame is open, so its back volume is the one the
  enclosure gives it.

**Runner-up: Same Sky CMS-402008-18SP.**
- Specification: 8 Ω, 1.5 W rated, 3 W maximum, 550 Hz (440–660), the same
  40 × 20 × 8 mm frame and 7.6 g. Source:
  [Same Sky datasheet](https://www.sameskydevices.com/product/resource/cms-402008-18sp.pdf),
  sha256 `44f3dac3…7adf07769`.
- $3.72 with 68 in stock (Digi-Key 2223-CMS-402008-18SP-ND, 2026-11-06).
- It has solder terminals instead of leads. Choose it if the PUI part is out of
  stock.

### Mounting and wiring

- **Back volume (design value).** The front shell gets a sealed back chamber
  behind the speaker, at least 5 cm³ (40 × 20 mm footprint by about 7 mm
  behind the magnet). It has a foam gasket on the frame's front face and no
  leaks around the leads (seal them with a dab of silicone). Neither maker
  publishes a back volume for these drivers. PUI rates a similar 40 mm part,
  the AS04004MO-SP40, in a 5 cm³ box. A small sealed box raises the resonance, which
  costs little for speech. Step 5 of the packaging plan listens through the
  real grille.
- **Wiring.** The amplifier's connector is J9, a 2-pin JST PH, 2.0 mm pitch,
  according to Seeed's STEP model; matching it to the "SPEAKER" silkscreen is
  inferred from the board photo.
  - Cut a pre-crimped PH lead (`ASPHSPH24K152`) in half and solder each half to
    a speaker lead under heat-shrink.
  - Push both into a `PHR-2` housing, + to the pin marked + on the board.
  - Crimping the speaker's own 24 AWG leads would put their insulation at the
    upper limit of the `SPH-002T-P0.5S` crimp.

## Gate board

### The guarantee

The microphones have no power when the privacy switch is off, or when nobody is
in front of the mirror. This holds whatever Linux does, including a compromised
Linux with root (packaging plan, R4).

The array's microphones are powered from its core board. The gate cuts the core
board's only power input, which is the 5 V reaching its 4-pin PH USB header (J10).

### Schematic

```text
FRDM J4 (USB-A host)                                       XVF3800 core J10 (PH-4)
P1 USB-A plug                    U1 AP22653W6-7 (SOT-26)
 1 VBUS ── SW1 ──┬── VIN ── 1 IN              OUT 6 ──┬────┬──── 5V_MIC ── J10 5V
          slide  │                                    │    │
          switch C1 1 µF           EN 3 ──┬── PIR_OUT C2   R5 1.6 kΩ
          (series)│                       │           1 µF │
                 GND             ILIM 5 ─ R1 15.0 kΩ ─ GND │   D2 LED, red (bezel window)
                                 FAULT 4 ─ test pad   │    │
                                 GND 2 ── GND       R2 100 kΩ   GND
                                                      │
                                                     GND
 2 D-  ─────────────────────────────────────────────────────────── J10 D-
 3 D+  ─────────────────────────────────────────────────────────── J10 D+
 4 GND ─────────────────────────────────────────────────────────── J10 GND
 shell ── GND

FRDM J18                                 PIR (Adafruit 189), its 3-pin lead
 5 V ────────────────────────────────── +5V
 GND ────────────────────────────────── GND
 wake GPIO ─┬─ R4 4.7 kΩ ── D1 BAT85 ── OUT = PIR_OUT
            │           (cathode)   (anode)
          R3 100 kΩ
            │
           GND
```

Netlist:

| Net | Connects |
|---|---|
| `VBUS_IN` | P1-1, SW1 common |
| `VIN` | SW1 throw, U1-1 IN, C1 |
| `5V_MIC` | U1-6 OUT, C2, R5, J10 5 V |
| `PIR_OUT` | the PIR's OUT, U1-3 EN, R2, D1 anode |
| `WAKE` | D1 cathode through R4; R3; J18 wake GPIO |
| `ILIM` | U1-5, R1 |
| `FAULT` | U1-4, test pad only (open drain, not connected) |
| `D-`, `D+` | P1-2 to J10 D-; P1-3 to J10 D+ (short, twisted) |
| `GND` | P1-4, P1 shell, U1-2, C1, C2, R1, R2, R3, D2 cathode, J10 GND, the PIR's GND, J18 GND |
| `PIR_5V` | J18 5 V, the PIR's +5V |

- **J10's pin order.** It is 5V, D-, D+, GND from left to right, read from the
  silkscreen in Seeed's
  [board photo](https://files.seeedstudio.com/wiki/reSpeaker_flex/main_noxiao.jpg)
  and
  [pinout image](https://files.seeedstudio.com/wiki/reSpeaker_flex/header_pinout.jpg).
  Check it against the real board before first power.
- **The PIR's pin order** follows its own silkscreen.

### Parts and values

| Ref | Part | Value and why | Source |
|---|---|---|---|
| SW1 | C&K L102021ML04Q, right-angle SPDT slide | Used as a single pole: common plus one throw, other throw unconnected. Rated 4 A at 28 V DC, 8.51 mm actuator (Digi-Key listing). The actuator sits in the bottom wall's `privacy_switch` slot, which the layout draws 8 × 3 mm. **Provisional:** caliper the body and actuator and resize the slot to suit; C&K's datasheet did not load. | [Digi-Key CKC5102-ND](https://www.digikey.com/en/products/detail/c-k/L102021ML04Q/181454) |
| U1 | Diodes AP22653W6-7, SOT-26 | Active-high EN, VIH 1.5 V / VIL 0.4 V, so the PIR's 3.3 V output drives it with margin. Adjustable current limit; 0.5 ms soft start; reverse-current blocking; thermal shutdown at 145 °C; 600 Ω output discharge whenever disabled or below UVLO. 6.0 V abs-max against USB's 5.25 V. Pins: 1 IN, 2 GND, 3 EN, 4 FAULT, 5 ILIM, 6 OUT. | [datasheet](https://diodes.com/datasheet/download/AP22652_AP22653_AP22652A_AP22653A.pdf), pp. 2–6, sha256 `972b6c1b…7b40` |
| R1 | 15.0 kΩ 1 % | Current limit 1.58 / 1.74 / 1.90 A (minimum / typical / maximum), from the datasheet's fit `I = 30321 / R^1.055` and its minimum and maximum forms. **Provisional:** step 3 measures the array's peak current. If the FRDM port turns out to limit lower, 20.0 kΩ gives 1.16 / 1.29 / 1.41 A. | as U1 |
| R2 | 100 kΩ | Holds EN low when the PIR is unplugged or its output floats: fail-off. Loads the PIR's output by 33 µA. | design value |
| C1, C2 | 1 µF X7R, 10 V or more | The datasheet asks for at least 0.1 µF on IN and on OUT. C1 stays well under USB 2.0's 10 µF attach limit for a device's VBUS. | as U1 |
| D1 | Vishay BAT85S-TAP, DO-35 Schottky | Lets the PIR drive the wake GPIO but blocks the GPIO from driving `PIR_OUT`. 2 µA maximum reverse leakage at 25 V, per the distributor listing; Vishay's datasheet was not retrieved. Even 2 µA through R2 is 0.2 V, far below U1's VIL of 0.4 V. | [Digi-Key BAT85S-TAPCT-ND](https://www.digikey.com/en/products/result?keywords=BAT85S-TAP) |
| R3 | 100 kΩ | Pulls the wake GPIO low when the PIR is low. External, so the wake does not depend on the SoC's own pull-down. | design value |
| R4 | 4.7 kΩ | Limits the current if the GPIO is driven low against a high PIR output (about 0.6 mA). The GPIO then reads about 2.9 V high: 3.3 V less about 0.25 V across D1 and 0.14 V across R4. | design value |
| R5 | 1.6 kΩ | 2.0 mA at 5.0 V and 2.2 mA at 5.25 V. | Kingbright VF below |
| D2 | Kingbright WP710A10LSURDK, 3 mm red, diffused | 30 mcd minimum, 80 mcd typical at 2 mA; VF 1.75 V typical. It sits in the bezel's `led_window` on a 2-wire lead. | [datasheet](https://www.kingbrightusa.com/images/catalog/SPEC/WP710A10LSURDK.pdf), sha256 `1040163a…c09b` |
| P1 | Adam Tech USB-AP-S-RA (PCB build only) | Through-hole USB-A plug lying flat on the board: 18.80 × 12.00 × 4.50 mm, 1.5 A, pins on a 2.0 mm pitch. | [datasheet](https://app.adam-tech.com/products/download/data_sheet/195860/usb-ap-s-ra-data-sheet.pdf), sha256 `57e4f7d9…1d714` |

- **The wake GPIO is provisional (ARRIVAL-08).** R4's 2.9 V high suits a pad
  bank at 3.3 V. If ARRIVAL-08 measures the wake pad's bank
  at 1.8 V, 2.9 V would overdrive it. In that case replace R4 with a divider:
  4.7 kΩ from D1, then 10 kΩ to ground at the pad, giving about 2.0 V. Or use a
  one-gate buffer powered from the bank's own supply. The gate side does not
  change.

### Why Linux cannot defeat it

| Situation | 5 V at the array | Why |
|---|---|---|
| SW1 off | none | the contact is open before U1, with no silicon in the path; U1's discharge also runs below UVLO |
| SW1 on, nobody present (PIR low) | none | EN is low, so U1 is off and its 600 Ω discharge drains the array's capacitors |
| SW1 on, PIR unplugged or broken open | none | R2 holds EN low |
| SW1 on, PIR high (someone present) | on, LED lit | by design; the hold keeps it on for the PIR's set time after the last motion |
| Linux drives the wake GPIO high | no change | D1 is reverse-biased: no SoC pin connects to `PIR_OUT` or EN |
| Linux drives the wake GPIO low while the PIR is high | on, or off | R4 limits the current. A weak PIR output could sag and turn the array off, never on. |
| Linux turns off J4's VBUS (vendor `USB_VBUS` regulator) | none | it can only remove power |
| U1 fails shorted | depends on SW1 | the manual switch still breaks the path |

**An assembly rule, without which the table does not hold:**
- Nothing is ever plugged into the core board's USB-C port or its 12 V terminal.
  Either one feeds the board directly. Seeed does not say whether these inputs
  are diode-ORed with J10's 5 V.
- The enclosure has no opening to either, so the rule holds once the unit is
  closed.

**The LED is lit exactly when `5V_MIC` is above about 2 V,** because it is
powered from that rail. Software has no path to it.

**What the gate does not cover:**
- A person in front of the unit is, by design, in range of a powered microphone.
- The speaker shares the array's power, so the unit is silent when nobody is
  present (packaging plan, audio layout).

### The hold timer

- **Mode and hold.** The Adafruit 189 holds its output high while motion
  continues when its jumper is in `H`. Its `TIME` pot sets the hold from about
  2.5 s to about 250 s (packaging plan, "PIR"). Set `H` and about 3 minutes
  (**provisional**).
- **Why no timer on the board.** The gate board has no timer of its own, so a
  person returning within the hold finds the array already powered.
- **When to add one.** Only if step 3 measures a presence-to-ready time
  (power-up, USB enumeration, firmware start) that needs a longer hold than the
  pot reaches. The timer would then be a diode, an RC network and a Schmitt
  buffer between `PIR_OUT` and EN.
- **The cost of a longer hold.** It also delays Linux's view of absence, because
  the wake GPIO reads the same output.

### Bill of materials

Prices are Digi-Key at qty 1, read on 2026-11-06. "Not checked" means no price
was looked up; a standard part costs cents.

| Ref | Part | Maker PN | Digi-Key PN | Qty | Unit |
|---|---|---|---|---|---|
| U1 | load switch | AP22653W6-7 | 31-AP22653W6-7CT-ND | 1 | $0.41 |
| SW1 | slide switch | L102021ML04Q | CKC5102-ND | 1 | $4.16 |
| D1 | Schottky | BAT85S-TAP | BAT85S-TAPCT-ND | 1 | $0.50 |
| D2 | LED | WP710A10LSURDK | 754-WP710A10LSURDK-ND | 1 | $0.28 |
| R5 | 1.6 kΩ ¼ W | CF14JT1K60 | (Stackpole) | 1 | $0.10 |
| R1 | 15.0 kΩ 1 % ¼ W | any | — | 1 | not checked |
| R2, R3 | 100 kΩ ¼ W | any | — | 2 | not checked |
| R4 | 4.7 kΩ ¼ W | any | — | 1 | not checked |
| C1, C2 | 1 µF X7R ceramic | any | — | 2 | not checked |
| — | PH 4-pin housing (to J10) | PHR-4 | — | 1 | $0.10 |
| — | PH 2-pin housing (speaker) | PHR-2 | 455-1165-ND | 1 | $0.10 |
| — | pre-crimped PH leads, 6 in, 24 AWG | ASPHSPH24K152 | 455-3082-ND | 3 | $0.40 |
| — | speaker | AS04008CO-2-LW100-R | (see above) | 1 | $3.59 |

**Perfboard build (step 3)**, in addition to the parts above:
- a SparkFun BOB-00717 SOT-23-to-DIP adapter for U1, $1.25;
- a small FR-4 perfboard, such as Adafruit 1608 at $3.95 (out of stock on
  2026-11-06; any similar board will do);
- a short USB-A male cable to cut, in place of P1;
- three 2.54 mm female jumper wires to cut, for J18.

The JST PH headers do not fit a 2.54 mm grid. On perfboard the leads are
soldered to the board and only their far ends carry PH housings. Parts total
about $12 (the unchecked parts at cents each).

**Fabbed PCB build (the enclosure)**, in addition to the shared parts:
- P1, Adam Tech USB-AP-S-RA, $0.42;
- B4B-PH-K-S $0.20, B3B-PH-K-S ×2 $0.32, B2B-PH-K-S $0.12, top-entry headers;
- three boards from OSH Park, $4.84 (about 25 × 25 mm at $5 per square inch,
  1.6 mm, free shipping, 9–12 days;
  [OSH Park services](https://docs.oshpark.com/services/)).

JLCPCB advertises boards from $2. Its US shipping and duty are shown only at
checkout and were not checked.

### Build form

**Recommendation: perfboard for step 3, then a fabbed PCB for the enclosure.**
- **Step 3 is a measurement step, and perfboard suits it.**
  - The packaging plan already calls for perfboard first.
  - Every part used then moves to the PCB except the adapter.
  - The measurements set the PCB's values: the array's peak current fixes R1,
    the hold, and the wake interface (diode or divider, from ARRIVAL-08).
- **The PCB is what fits the enclosure.**
  - The layout places the gate board on J4 itself (USB-A plug on the board),
    18.8 × 22 mm.
  - Perfboard cannot carry the 2.0 mm-pitch plug or the PH headers.
- **Laying the PCB out needs KiCad.** It is not installed on the development
  PC. The layout and its gerbers are a follow-up.
  - Amended 2026-11-06: the owner approved KiCad for when the layout is
    actually needed, after the step 3 bench build.
  - The agent has no sudo, so at that point it tells the owner, who installs
    it on the PC.

The two forms differ by about $5 and the PCB's two-week wait. This choice
changes no privacy property, because the circuit is the same.

### Step 3 bench checks

These follow the plan's step 3 verify line. Run them with Linux running and the
array on the gate.
1. **Polarity first.** With SW1 off and the PIR covered, check that J10's pins
   match the order above, and that `5V_MIC` reads 0 V.
2. **The privacy cases.** For each row of the table above, record `5V_MIC` and
   the LED, including the wake GPIO driven high and low. On the vendor image,
   only a `/tmp` script may drive the pin, and only with the owner's go-ahead
   if it needs a device-tree change.
3. **The currents.** Measure the array's current at idle, while capturing, and
   at full speaker volume (tone at 1 kHz). Measure the inrush when EN rises.
   These decide R1.
4. **The amplifier on 5 V.** Play a tone through the array's playback device.
   Silence means the BD mode: use the fallback amplifier above.
5. **Presence to ready.** Measure the time from the PIR going high to the array
   enumerating, and to a capture opening. Also time how fast `5V_MIC` falls
   below 0.5 V after EN drops. The array's bulk capacitance is not published:
   the STEP model shows 2 × 470 µF, which at 600 Ω would take about a second.

## Follow-ups

- **ARRIVAL-08 and ARRIVAL-10 (owner).** Take the meter reading on the PIR pad
  bank, then wire the PIR. These fix the wake interface (R4 or a divider).
- **`enclosure/params.toml`.** Done for the speaker (2026-11-06): its vendor
  outline sits 1 mm lower than the placeholder, clear of the core board's
  audio jack, and the CAD's `speaker_chamber` holds 5.35 cm³ of air behind it.
  The gate board's outline stays a placeholder until the PCB is laid out.
- **The PCB layout and gerbers,** after step 3.
  - KiCad was approved by the owner on 2026-11-06 for this step.
  - When step 3's measurements are in, the agent asks the owner to install
    it; the agent has no sudo.
- **Order list for the owner** (nothing is bought by the agent): the BOM above
  with the perfboard extras. Add CMS-402008-18SP if the PUI speaker is out of
  stock.
