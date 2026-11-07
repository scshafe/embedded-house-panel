# Mirror Unit: Higher-Quality Speaker Options

<!-- @canonical mirror-unit-speaker-options -->

**Status (2026-11-06):** a comparison, asked for by the owner. Amended
2026-11-06: the owner picked option 3(a), the Dayton DMA45-8 with the PIR
moved up 5 mm, and a host-side low-cut filter (option 2). Both are now in
[mirror-unit-speaker-and-gate.md](mirror-unit-speaker-and-gate.md) and the
enclosure model, and the PUI AS04008CO-2-LW100-R is the fallback. The rest
of this page is the comparison as written. When written, nothing here was
bought and the enclosure model was unchanged. Every option that would move a part, grow the enclosure or change
the power path is written as a recommendation for the owner
([Decisions for the owner](#decisions-for-the-owner)).

Values are labelled by where they come from:
- **datasheet:** the maker's own sheet, downloaded and hashed;
- **listing:** a distributor's page, which is weaker evidence;
- **estimate:** computed here with the [method](#method) below. These are
  small-signal models of a sealed box, not measurements.

## Summary

- **The chin is about 85 mm tall, not 30 mm.** The plan's first guess was
  30 mm. The layout hangs the 120 mm FRDM board below the glass, so the front
  plate under the glass is 84.6 mm tall. The speaker only uses a 40 × 20 mm
  slot of it with a 5.35 cm³ chamber.
- **What limits quality today is the driver, then its back volume:**
  - A 40 × 20 mm micro speaker resonates at about 550 Hz. Its usable output
    ends at about 700 Hz in today's chamber (estimate), so it plays no voice
    fundamentals and little of the warmth band (100–500 Hz).
  - Its cone barely moves: the published 40 × 20 mm-class figure is 0.1 mm of
    linear travel. Low frequencies make it distort before the amplifier runs
    out.
  - A 1.5–2 inch full-range driver in a 25–75 cm³ box reaches 190–280 Hz
    (estimate), with 9–12 dB of excursion headroom at 200 Hz.
- **The 5 V amplifier and the grille do not limit speech.**
  - The amplifier gives about 78–86 dB at 1 m in the midrange, depending on
    the driver's sensitivity. That is enough for speech across a room.
  - The present slotted grille sets a front resonance near 8–9 kHz
    (estimate), above the speech band.
- **A bigger driver and box is the better upgrade, not the fallback
  amplifier.** Any amplifier on the same 5 V rail has the same voltage swing.
  The fallback only matters if the TPA3139D2 turns out to be off at 5 V
  (step 3).
- **Best value, if the owner wants better sound:** the Dayton Audio DMA45-8.
  - It needs the PIR moved about 5 mm up and its cable rerouted, both inside
    today's outline. It needs no new power path.
  - Its estimated low end is about 190 Hz, against about 700 Hz now, at the
    same loudness.
  - It is about $9, but Parts Express has it back-ordered to 2026-12-18.

## The space in the chin

From `enclosure/params.toml`, with clearances of 1 mm to parts and cables:
- **Height.** The glass ends at y −55.4 mm and the shell's outer bottom is at
  −140 mm, so the chin is 84.6 mm tall. Inside the 2.4 mm wall, the floor is
  at −137.6 mm.
- **Today's speaker** uses x 12.5–52.5 mm and y −131 to −111 mm. Its chamber
  is 10 mm deep and holds 5.35 cm³ of air.
- **The free pocket on the right, below the PIR.** It is 44.6 mm tall (from
  the floor to the PIR board at −92 mm) and about 58 mm wide. That runs from
  about x 39, past the core board and the FRDM board's USB-C jacks, to the
  right wall.
- **The pocket's depth.** It is 26 mm deep from the front plate's inner face,
  down to the PIR cable's route at z −28. With that cable rerouted, the
  pocket is 39–44 mm deep (rerouting is a layout change).
- **The left side of the chin** has more area, but the FRDM board sits
  16–17 mm behind the front plate there. Every candidate below except the
  40 × 20 mm class is deeper than that.

So the pocket takes a box up to about 44 × 58 × 26 mm without moving
anything except the speaker itself. Drivers larger than about 40 mm need the
PIR to move up or the chin to grow.

## The amplifier at 5 V

The reSpeaker Flex's amplifier is a TI TPA3139D2. It is covered in the
[baseline doc](mirror-unit-speaker-and-gate.md#what-the-amplifier-gives-at-5-v),
and its figures are from TI's SLOS810A. At 5 V, one bridge:

| Load | Power at 1 % THD+N | Voltage | 5 V current at that power (estimate) | At 10 % THD+N (clipping) |
|---|---|---|---|---|
| 8 Ω | about 1.4 W | 3.35 V rms | about 0.34 A | 1.7 W, about 0.41 A |
| 4 Ω | about 2.3 W | 3.03 V rms | about 0.55 A | 2.9 W, about 0.69 A |

- **The estimates** assume 87 % efficiency plus TI's 22 mA idle, on top of
  the array board's own draw, which is not published.
- **4 Ω does not make a 4 Ω driver louder.** The voltage is the same at 5 V,
  so a 4 Ω driver is louder only by its higher sensitivity per volt. The
  current is what doubles.
- **8 Ω keeps the most margin on the FRDM's USB port,** whose limit is not
  published. Speech averages about 10 dB below its peaks, so average current
  is far lower, but the gate's limit (R1) and the port see the peaks.
- **Both loads stay below the gate's 1.58 A minimum limit.**

## Candidates

The 40 × 20 mm micro class (the PUI and Same Sky lines) was checked first.
Within it, nothing reaches lower:
- Dayton's CE40-20F-8 is the same size and publishes full parameters
  (below), but it has a weaker motor and is rated 1 W.
- PUI's premium AS04004MO-SP40 has low distortion but is 4 Ω and quieter.
  Its sheet gives 75 dB at 1 W and 0.5 m, against the baseline's 84 dBA.

So the upgrades are full-range drivers of 1–2 inches:

| Driver | Ω | Frame, depth (mm) | Fs (Hz) | Qts | Vas (cm³) | Sd (cm²) | Xmax (mm) | Sensitivity, 2.83 V / 1 m | Rated | Price and stock, 2026-11-06 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Baseline:** PUI AS04008CO-2-LW100-R | 8 | 40 × 20, 8 | 550 | n/p | n/p | n/p | n/p | about 78 dB (84 dBA at 1 W / 0.5 m) | 1.5 W | $3.59, Digi-Key, 20 in stock |
| *40 × 20 class proxy:* Dayton CE40-20F-8 | 8 | 40 × 20, 6.1 | 695 | 1.46 | 8 | 3.5 | 0.1 | 76 dB | 1 W | not checked |
| HiVi B1S | 8 | Ø35.8, 13.2 | 220 | 0.56 | 28 (listing, unconfirmed) | n/p | n/p | 79 dB | 1 W | $8.98, Parts Express, 2,152 in stock (listing); €8.95, Sound Imports, 10+ |
| Dayton Audio DMA45-8 | 8 | 46 × 46, 25.9 | 150.7 | 0.59 | 55 | 8.6 | 2.0 | 76.9 dB | 10 W | $8.98, Parts Express, back-ordered to 2026-12-18 |
| Tang Band W1-2121S | 4 | Ø46, 27.2 | 120 | 0.35 | 108 (listing) | 8.9 (listing) | 2.3 | 81.2 dB (78.2 dB at 1 W) | 6 W | about $49, Parts Express (listing, not confirmed) |
| Dayton Audio DMA58-8 | 8 | 56 × 56, 31.75 | 166.2 | 0.54 | 150 | 14.5 | 2.0 | 82.5 dB | 15 W | $18.99 MSRP; €19.95, Sound Imports, 10+ (listing) |
| Peerless PLS-P830983 | 4 | Ø55.1, 35.1 | 175.6 | 0.71 | 160 | 15.2 | 1.7 | 85.0 dB half-space (82 dB at 1 W) | 25 W | $15.75, Parts Express, 103 in stock |

n/p: not published.

**Sources:**
- **PUI:** drawing, sha256 `2112ccaa…71b5`. 84 dBA at 0.5 m is about 78 dB at
  1 m.
- **Dayton:**
  - [CE40-20F-8](https://daytonaudio.com/images/resources/285-137-dayton-audio-ce40-20f-8-spec-sheet.pdf),
    `1117b6f9…ca45`;
  - [DMA45-8](https://daytonaudio.com/images/resources/295-581--dayton-audio-dma45-8-specification-sheet.pdf),
    `27e8072f…b168`;
  - [DMA58-8](https://daytonaudio.com/images/resources/295-583--dayton-audio-dma58-8-specification-sheet.pdf),
    `427facf9…cada`.
  - The DMA45-8's frame and depth are from the
    [Parts Express page](https://parts-express.com/Dayton-Audio-DMA45-8-1-1-2-Dual-Magnet-Aluminum-Cone-Full-Range-Driver-8-Ohm-295-581).
    The DMA58-8's are from the Audiophonics and Sound Imports listings.
- **Tymphany:** [PLS-P830983](https://www.parts-express.com/pedocs/specs/264-1042--tymphany-pls-p830983-spec-sheet.pdf),
  `a5b637bf…b712`. Its frame, price and stock are from the Parts Express page.
- **HiVi B1S:** HiVi publishes no sheet that could be found. Fs, Qts,
  sensitivity, rating and size are from
  [Sound Imports](https://www.soundimports.eu/en/hivi-b1s.html). Vas is from a
  Parts Express listing seen through a search, because the page refused an
  automated fetch.
- **Tang Band W1-2121S:** Tang Band's site could not be reached from here.
  Fs, Qts, Xmax and sensitivity are from the
  [bits.00aud.io](https://bits.00aud.io/) database. Vas, Sd, size and price
  are from distributor listings seen through a search. Treat this row as the
  weakest.

Parts Express refused automated fetches for most product pages. Stock
figures marked "listing" need checking before an order.

## In a realistic sealed box

Each driver is shown in the box it could have: today's chamber, the right
pocket (26 mm deep), or a box needing a layout or size change (see
[Fit](#fit-what-each-driver-needs)). The boxes are net air (estimate).

| Driver | Box (net) | Fc | Qtc | −3 dB | Max SPL at 1 m, midrange | At 200 Hz: amplifier-limited / excursion-limited |
|---|---|---|---|---|---|---|
| 40 × 20 class (proxy) | today's 5.35 cm³ | 1,100 Hz | 2.3 | about 730 Hz | about 78 dB | 48 / 51 dB |
| 40 × 20 class (proxy) | right pocket, 18 cm³ | 835 Hz | 1.75 | about 570 Hz | about 78 dB | 53 / 51 dB |
| HiVi B1S | right pocket, 25 cm³ | 320 Hz | 0.82 | about 280 Hz | about 80 dB (above its 1 W rating) | 72 / n/p |
| DMA45-8 | 45 cm³ | 225 Hz | 0.88 | about 190 Hz | about 78 dB | 76 / 85 dB |
| W1-2121S | 35–45 cm³ | 220–245 Hz | 0.65–0.71 | about 245 Hz | about 82 dB | 77 / 87 dB |
| DMA58-8 | 75 cm³ | 290 Hz | 0.94 | about 235 Hz | about 84 dB | 78 / 90 dB |
| PLS-P830983 | 75 cm³ | 310 Hz | 1.26 | about 225 Hz, with a +3 dB hump | about 86 dB | 80 / 89 dB |

How to read it:
- **The 40 × 20 class.** The PUI publishes no Thiele–Small parameters, so
  the CE40-20F-8 stands in for its class. The PUI's own figures are 550 Hz
  free-air resonance and about 78 dB.
  - A high Qtc means a peak near Fc, which is how micro speakers get their
    loudness. Below it the output falls at 12 dB per octave.
  - Moving the baseline into the right pocket gains about a third of an
    octave. Low frequencies would still exceed its linear excursion, so it
    needs a high-pass filter either way (see
    [Decisions](#decisions-for-the-owner)).
- **Low Q, low −3 dB.** The DMA45-8, DMA58-8 and W1-2121S all sit near Qtc
  0.7–0.95 in these boxes, so they have no boom.
  - The PLS-P830983's Qts of 0.71 gives Qtc 1.1–1.6 in any box that fits, so
    it has a hump.
  - The DMA45-8 would need 126 cm³ and the DMA58-8 210 cm³ for Qtc 0.707,
    which is more than the chin has. Smaller boxes trade a little Q for size.
- **Loudness.**
  - The midrange maximum is the sensitivity plus the amplifier's swing:
    +1.5 dB at 8 Ω, +0.6 dB at 4 Ω.
  - The DMA45-8 is no louder than the baseline; its gain is the low end and
    the clean excursion.
  - The DMA58-8 and the Peerless are 6–8 dB louder.
- **Excursion headroom.** For every full-range driver, the excursion limit
  at 200 Hz is 9–12 dB above what 5 V can drive, so they stay clean down
  there. The 40 × 20 class is already at its limit at 200 Hz.

### Distortion

- **Only the micro speakers publish distortion:**
  - the PUI: 5 % maximum, with no test condition given;
  - Same Sky's CMS-402008-18SP: 5 % at 1 kHz and 0.1 W;
  - PUI's AS04004MO-SP40: at most 5 % at 1 kHz and 2 W.
- **The full-range sheets give none.** They give Xmax instead, so the
  excursion headroom above is the comparison used here.
- **The DMA and PLS drivers** have copper caps (their datasheets), and all
  three full-range makes give 1.7–2.3 mm of linear travel, which is what keeps distortion low at
  speech's lower frequencies. Measure it before relying on it (see
  [Decisions](#decisions-for-the-owner)).

## Fit: what each driver needs

Today the speaker uses x 12.5–52.5 mm. For each driver:
- **Front area** is the driver plus 1 mm of gap and a 2 mm chamber wall. A
  chamber built into the shell's corner uses the shell's own floor and right
  wall on two sides.
- **Depth** is the driver plus the 1.6 mm chamber back and 2 mm of air
  behind the magnet.
- **Mass** is the driver's weight, which adds to the hanging mass. Today's is
  762 g, needing about 130 cm² of VHB.

| Driver | Front area needed | Depth | Where it fits | Mass |
|---|---|---|---|---|
| 40 × 20 class | 44 × 24 mm | 12 mm | Today's slot (5.35 cm³), or the right pocket (about 18 cm³) | 7.8 g |
| HiVi B1S | about 40 × 40 mm | 17 mm, 26 for the full pocket | The right pocket, about 25 cm³; no other part moves | not published |
| DMA45-8 or W1-2121S | about 49 × 49 mm | 30 mm | The right pocket, if the PIR moves up 5 mm (its window then ends 7.6 mm below the glass) and its cable is rerouted behind the chamber. About 45 cm³. | DMA45-8: Dayton lists 0.2 lb (about 90 g, possibly the shipping weight) |
| DMA58-8 or PLS-P830983 | about 59 × 59 mm | 34–37 mm | Needs the chin about 15 mm taller, to about 100 mm (the outline 216 → about 231 mm), or the PIR moved to the left chin. About 75 cm³. | PLS: 140 g (datasheet) |

Whether the PIR can move:
- **Moving it up** is limited by the glass, because its window must stay
  below it.
- **Moving it to the left chin** brings it within about 45 mm of the FRDM
  board's Wi-Fi antenna zone. The packaging plan warns that PIRs
  false-trigger near Wi-Fi.

Any heavier driver also needs a stiffer chamber and the frame gasket. That
keeps the cabinet from carrying vibration to the mic strip, about 180 mm
away.

## What limits quality here

Ranked, for speech from the mirror unit:

1. **The driver class.** The 40 × 20 mm micro speaker has little cone area
   (about 3.5 cm² of cone for the class), 0.1 mm of travel and a 550–700 Hz resonance. That
   fixes a thin voice whatever the box.
2. **Back volume.** For a full-range driver, the box sets the low end and the
   Q. Today's 5.35 cm³ suits only the micro speaker. The right pocket gives
   18–45 cm³ depending on what moves.
3. **Amplifier power at 5 V (not limiting for speech).**
   - Conversational speech is about 60 dB at 1 m. A voice assistant heard
     across a room wants about 70–75 dB at 1 m, and the drivers above reach
     78–86 dB.
   - More would need a higher rail. A boost converter or a separate supply
     is a power-path change, and either draws more from the USB port or adds
     a second supply that the privacy gate would also have to switch.
4. **The grille (not limiting, if it is scaled).**
   - Today's 9 slots, 1.6 × 8 mm in the 3 mm front plate, are 115 mm² open.
     With an assumed 0.6–0.8 cm³ front cavity, that gives a resonance of about
     8–9 kHz (estimate).
   - A larger driver needs the open area scaled to at least about 30 % of
     its cone, 260–450 mm², and a thin gasket so the front cavity stays
     small. That keeps the resonance above about 6 kHz (estimate).

## Bigger chin or the fallback amplifier

- **A bigger box and driver is the upgrade.** It moves the low end from about
  700 Hz to about 190–240 Hz, with clean excursion. The DMA58-8 also adds
  6 dB.
- **"Bigger chin" is mostly not needed.** The DMA45-8 class fits inside
  today's outline. Only the 2-inch class needs about 15 mm more chin.
- **The fallback amplifier is not a quality upgrade.** A mono class-D
  breakout on the gated 5 V has the same ≤ 3.5 V rms swing as the TPA3139D2.
  It exists for one case: the TPA3139D2 is silent at 5 V because `MODE_SEL`
  selects the 7.5 V lockout. It works with any 8 Ω driver above.

## Passive radiator

- **The part:** Dayton's DMA45-PR, the DMA45-8's partner.
  - Datasheet `f53c4006…934c`: Fs 60 Hz, Mms 2.8 g, Sd 8.6 cm², Xmax 5 mm,
    Vas 0.26 L.
  - Size: 46 × 46 mm and 34 mm deep (Dayton's page). It costs $8.99 MSRP.
- **Its tuning without added mass:** in 45 cm³ it would tune to about
  60 × √(1 + 260/45) ≈ 155 Hz (estimate). That is below the DMA45-8's sealed
  −3 dB of about 190 Hz, so it could extend the low end by perhaps half an
  octave.
- **It needs a second 46 mm aperture** on the front, next to the driver. The
  chin has no room for one even with the PIR moved, so it is only worth it
  with the 2-inch-class chin.
- **Not recommended at this size.** At this power it adds less than the
  step from the micro speaker to any full-range driver.

## Decisions for the owner

Amended 2026-11-06: the owner chose 2 and 3(a). The rest stays as written.

1. **Keep the baseline for now** (no change). Step 3 still answers first
   whether the TPA3139D2 plays at 5 V. Every option here is 8 Ω or 4 Ω on
   the same amplifier.
2. **A high-pass filter in the playback chain** (about 400–500 Hz for the
   baseline, about 150 Hz for a full-range driver). It is software on the
   host side, before the array, so the echo canceller's reference stays
   consistent. It stops the micro speaker being driven past its excursion.
   *Recommended in any case;* it is a software change, not part of this
   design.
3. **Better sound inside today's outline:**
   - **(a) Recommended:** the Dayton DMA45-8 in the right pocket. Move the
     PIR up 5 mm and reroute its cable; it is still 8 Ω.
   - **(b) Without moving the PIR:** the HiVi B1S in the right pocket. Its
     data is thin and it is rated 1 W, so cap the volume about 1.5 dB below
     full.
   - **(c) The minimum:** keep the PUI and give it the right pocket's 18 cm³.
4. **The most quality: a chin about 15 mm taller** with the DMA58-8 (8 Ω).
   This changes the enclosure's size, adds mass and VHB area, and needs a
   new fit check.

**Not recommended:**
- the Peerless PLS-P830983 (4 Ω current, a hump in any box that fits, 140 g);
- the Tang Band W1-2121S (4 Ω, about $49, no gain over the DMA45-8);
- the passive radiator;
- a higher-voltage amplifier rail.

If the owner picks 3(a), 3(b), 3(c) or 4:
- the speaker and chamber in `enclosure/params.toml` and `parts.py` follow
  the same way as in #65;
- a listening and measurement check goes into step 5 of the packaging plan
  (the real grille);
- the baseline doc's Decisions change.

## Method

- **Sealed box:**
  - Fc = Fs·√(1 + Vas/Vb) and Qtc = Qts·√(1 + Vas/Vb);
  - −3 dB from the second-order high-pass with that Fc and Qtc;
  - box losses, the driver's own displacement and the leak are ignored.
- **Net box volumes** subtract an estimate of the driver's volume.
- **Excursion-limited SPL** treats the cone as a point source in half-space
  at 1 m: p = ρω²·Sd·Xmax / (2πr), as rms.
- **The amplifier's swing** is √(P·R) at TI's 1 % THD+N power for 5 V.
- **Grille resonance** is a Helmholtz resonator: the slots in parallel, each
  with an end correction of 0.85 times the equivalent radius per side, and
  the front cavity behind them.
- **The chin's free space** comes from the layout model's solids and cable
  routes, with 1 mm clearances.

These are design estimates for choosing a direction. The fit check (step 4)
and the listening test (step 5) are where they get checked.
