# A Full-Range Microphone for Recording at 0.6 m

<!-- @canonical full-range-microphone-options -->

**Status (2026-11-14): proposed, an exploration asked for by the owner.** It
is research and a recommendation only. Nothing was bought, nothing on the
board changed, and nothing here commits the project to a new microphone.
Every step that would buy a part, change the enclosure, the gate board, the
board or voice-lab's contract is written as a question for the owner
([Decisions for the owner](#decisions-for-the-owner)).

**Amended 2026-11-14:**
- **Distance.** The owner corrected the speaking distance to about 0.6 m.
  0.6 m is now the design distance, and 1.2 m is kept as the worst case.
  The take-distance decision is answered and removed.
- **Two microphones.** Added a section on the two-microphone ideas the
  owner asked about.

The question: the owner may make the panel the main place to record the
voice corpus for voice-lab. voice-lab fine-tunes F5-TTS at 24 kHz, so useful
content reaches about 12 kHz. The take is spoken from about 0.6 m (1.2 m
at worst), straight in front of the panel at head height, in a small tiled
bathroom with glass and the mirror behind the panel. The microphone must mount on or in the panel.

Two options are already out:
- **The XVF3800 array** captures at 16 kHz, so nothing above 8 kHz
  ([xvf3800-array-on-frdm.md](xvf3800-array-on-frdm.md)). Seeed's 48 kHz
  firmware does not change that (below).
- **The miniDSP UMIK-1** was rejected by the owner: too bulky and oddly
  shaped for the panel, and very quiet beyond about 15 cm in past tests.

Values are labelled by where they come from:
- **datasheet:** the maker's own sheet or manual;
- **listing:** a retailer's or distributor's page. Several retailer pages
  refused automated reads on 2026-11-14, so some prices come from search
  results of those pages and were not opened. Check before buying;
- **estimate:** computed here with the [method](#method). Nothing in this
  page was measured in the bathroom.

## Summary

- **At 0.6 m the room still sets the quality, but it can be managed.**
  - Speech is 12 dB quieter at 0.6 m than at 15 cm, at about 67 dB SPL.
  - In a small tiled room, an ordinary omni microphone then hears about
    10 dB more reverberant sound than direct sound (estimate).
  - F5-TTS learns the room along with the voice, so reverberation is the
    first problem and noise the second.
- **With the session treatment, a supercardioid at 0.6 m is about balanced**
  (direct ≈ reverberant, −0.3 dB):
  - The treatment is soft surfaces for the session: door open, towels on
    the glass, a bath mat. It is worth about 3.7 dB.
  - A supercardioid gains about 5.7 dB over an omni.
  - A flush omni in the panel face is about 3 dB behind the supercardioid
    at high frequencies and about 6 dB behind below 1.5 kHz.
- **At 1.2 m, the worst case,** every option is 6 dB more reverberant
  still. That is why the 0.6 m design distance matters more than any
  microphone.
- **Noise is no longer the hard part.**
  - 40 dB of speech-to-noise at 0.6 m allows about 27 dBA of total noise.
  - A 15 dBA microphone and the best MEMS parts (73 dB SNR, 21 dBA) pass
    with room to spare.
  - A 69 dB SNR MEMS (25 dBA) passes alone, but it leaves the room only
    about 22 dBA.
  - A single 65 dB SNR MEMS (29 dBA) still fails. Two of them summed are
    marginal.
- **A second microphone cannot subtract reverberation.** Several
  microphones can still help, through beamforming and multichannel
  dereverberation such as WPE, applied offline to raw channels
  ([Two microphones](#5-two-microphones)).
- **Recommendation: try a Rode VideoMic GO II first** (about $100, USB audio
  at 24-bit/48 kHz, supercardioid, 15 dBA, 33 g). The kernel the board runs
  today already has USB audio. It is bus-powered, so the existing privacy
  gate cuts its only power. Mount it on a removable shoe on top of the
  enclosure, aimed at the talker.
- **The built-in route is more workable at 0.6 m,** because its MEMS now
  pass on noise. Its DRR gap to a supercardioid is the same at any
  distance. It uses two to four Infineon IM73D122 PDM microphones on the SoC's PDM input (J18), flush
  behind acoustic vents in the front face.
  - The device tree and drivers are already there, and it handles steam
    better.
  - Its raw channels, sampled on one clock, allow beamforming and
    multichannel dereverberation that a single USB microphone cannot.
  - It needs a small board and a gate change.
- **Both routes need voice-lab's agreement.** voice-lab's pinned contract
  accepts only takes from the XVF3800 at 16 kHz.
- **Measure before buying.** The XVF3800 already on the board can measure
  the room's reverberation time and noise below 8 kHz, which decides how
  much the microphone choice matters ([What to measure first](#what-to-measure-first)).

## 1. The physics

### Level: inverse square

Free-field speech level falls 6 dB per doubling of distance:

| From 15 cm to | 30 cm | 60 cm | 90 cm | 1.0 m | 1.2 m |
|---|---|---|---|---|---|
| Level change | −6.0 dB | −12.0 dB | −15.6 dB | −16.5 dB | −18.1 dB |

ANSI S3.5 gives normal vocal effort as about 62 dB SPL at 1 m. That puts a
take at about 79 dB SPL at 15 cm, 67 dB SPL at 0.6 m (the design distance)
and 61 dB SPL at 1.2 m (the worst case) (estimate). The
direct sound falls with distance, but the room's reverberant sound stays
roughly constant, which is why the next number matters more.

### Reverberation: critical distance and direct-to-reverberant ratio

The critical distance is where the direct and reverberant sound are equally
loud:

r_c = 0.057 × √(Q × V / T60) m

- Q is the microphone's directivity factor (1 for omni);
- V is the room volume in m³;
- T60 is the reverberation time in seconds.

The ratio at distance r is DRR = 20 log10(r_c / r).

**The bathroom here is an assumption, not a measurement:** 2.2 × 1.8 × 2.4 m
(9.5 m³). Tiled bathrooms typically measure T60 = 0.5–1.2 s in the speech
band, so 0.8 s is the middle case.

| T60 | Pattern (Q) | r_c | DRR at 15 cm | at 0.6 m | at 0.9 m | at 1.2 m |
|---|---|---|---|---|---|---|
| 0.8 s | omni (1) | 0.20 m | +2.3 dB | −9.7 dB | −13.2 dB | −15.7 dB |
| 0.8 s | omni flush in the face (~2, above ~1.5 kHz) | 0.28 m | +5.4 | −6.7 | −10.2 | −12.7 |
| 0.8 s | cardioid (3) | 0.34 m | +7.1 | −4.9 | −8.4 | −10.9 |
| 0.8 s | supercardioid (3.7) | 0.38 m | +8.0 | −4.0 | −7.5 | −10.0 |
| 0.8 s | hypercardioid (4) | 0.39 m | +8.4 | −3.7 | −7.2 | −9.7 |
| 0.8 s | short shotgun, high frequencies (~8) | 0.56 m | +11.4 | −0.7 | −4.2 | −6.7 |
| 0.5 s | omni / supercardioid | 0.25 / 0.48 m | +4.4 / +10.1 | −7.7 / −2.0 | −11.2 / −5.5 | −13.7 / −8.0 |
| 1.2 s | omni / supercardioid | 0.16 / 0.31 m | +0.6 / +6.3 | −11.5 / −5.8 | −15.0 / −9.3 | −17.5 / −11.8 |

All estimates.

**At the design distance, 0.6 m, with and without the session treatment**
(T60 0.8 s untreated, about 0.34 s treated; estimates):

| Pattern (Q) | Untreated | Treated | 1.2 m, treated |
|---|---|---|---|
| omni (1) | −9.7 dB | −6.0 dB | −12.0 dB |
| omni flush in the face (~2, above ~1.5 kHz) | −6.7 | −3.0 | −9.0 |
| cardioid (3) | −4.9 | −1.2 | −7.2 |
| supercardioid (3.7) | −4.0 | −0.3 | −6.3 |
| short shotgun, high frequencies (~8) | −0.7 | +3.0 | −3.0 |

What they say:
- **The UMIK-1's past tests explain each other.** Its drop beyond 15 cm is
  the inverse-square loss: 12 dB by 0.6 m. On top of that, an omni at 0.6 m
  hears about 10 dB more reverberant sound than direct, while at 15 cm it
  sits inside the critical distance and the direct sound wins.
- **At 0.6 m, a supercardioid with the session treatment is about balanced.**
  Without the treatment it is 4 dB reverberant, and a bare omni 10 dB.
- **A flush omni in the face is in between.** The face's baffle gives it
  about Q = 2 above roughly 1.5 kHz, so treated it sits 3 dB behind the
  supercardioid there. Below that it is a plain omni (−6 dB treated).
- **1.2 m costs 6 dB with any microphone.** The worst case is reverberant
  even with a short shotgun.
- **Soft surfaces help.** The room here has about 1.9 m² of absorption
  (Sabine, at T60 = 0.8 s). An open door (about 1.6 m², a perfect absorber)
  plus two towels and a bath mat (about 1 m² above 1 kHz) bring T60 to about
  0.34 s. That raises r_c and the DRR by about 3.7 dB (estimate).
- **Below about 600 Hz the room is modal.** The Schroeder frequency is
  2000 × √(T60 / V) ≈ 580 Hz, so low frequencies come as a few strong modes,
  not a diffuse field, and these averages are rougher there.

### How much each pattern helps

The directivity index is DI = 10 log10(Q). It is the gain against diffuse
sound: reverberation and noise from all directions.

| Pattern | Q | DI | Notes |
|---|---|---|---|
| Omni | 1 | 0 dB | Every MEMS capsule and boundary microphone on its own |
| Omni flush in a baffle | ~2 above ~1.5 kHz | ~3 dB | The panel face rejects the half-space behind it at short wavelengths. Pressure doubling also adds 6 dB of direct level, with no comb filtering, if the port is within a few mm of the surface |
| Cardioid | 3 | 4.8 dB | Desk USB microphones |
| Supercardioid | 3.7 | 5.7 dB | VideoMic GO II and NTG. Rear lobe about −10 to −12 dB |
| Hypercardioid | 4 | 6.0 dB | Larger rear lobe |
| Short shotgun (12–20 cm tube) | ~4 at low frequencies, ~8–10 above a few kHz | 6–10 dB | The tube adds directivity only where the wavelength is shorter than about twice its length. Below that it is a supercardioid |
| MEMS array, fixed beam (UMA-8 class, 9 cm) | ~1–2 below 1 kHz, ~4–6 above 3 kHz | 0–8 dB | Directivity needs aperture: a 9 cm array has almost none at voice fundamentals. Superdirective designs trade this for low-frequency noise gain |

All estimates from first-order patterns and array aperture; none is a
product measurement.

**Comb filtering.** A capsule standing off a reflector by d meters sees its
first notch at c / (4d):
- 5 mm off the face puts the notch at 17 kHz, which is harmless;
- 1 cm puts it at 8.6 kHz, which is in the band;
- 3 cm puts it at 2.9 kHz.

So a built-in microphone should be flush with the front face, not recessed
in a tube or a deep port. A tube microphone on top of the enclosure has the
mirror about 5–10 cm behind its capsule. The supercardioid's rear lobe
leaves a ripple of roughly ±2 dB there (estimate), worth checking in the
first test.

### Noise: what self-noise and sensitivity are needed

- **Working target:** a speech-to-noise ratio of at least 40 dB in the take.
  That is a common bar for TTS training data, not a standard. At 67 dB SPL
  of speech (0.6 m), total noise must stay at or below about 27 dBA. At the
  1.2 m worst case (61 dB SPL) it is about 21 dBA.
- **The room:** a quiet bathroom with the fan off is often 25–35 dBA. That
  is a guess to be measured. A bath fan rated 0.3–3 sones adds roughly
  25–55 phon.
- **Directivity helps against noise too.** A directional microphone hears
  diffuse room noise lower by its DI, about 5.7 dB for a supercardioid. So
  the room is often still the limit, and the fan has to be off.
- **The microphone:** its own noise must leave room for the room's. Noise
  adds as power, so a microphone at the full 27 dBA budget leaves nothing
  for the room.

| Microphone | Self-noise | Margin at 0.6 m (67 dB SPL) | Room noise left at 0.6 m (as an omni hears it) | Verdict at 0.6 m | Margin at 1.2 m |
|---|---|---|---|---|---|
| Rode VideoMic GO II / NTG | 15 dBA (datasheet) | ~52 dB | ~26.5 dBA as an omni would hear it; its 5.7 dB DI lets the room itself be about 32 dBA | passes easily | ~46 dB |
| Infineon IM73D122 (73 dB SNR) | 21 dBA (94 − SNR) | ~46 dB | ~25.5 dBA | passes | ~40 dB |
| Two IM73D122 summed | ~18 dBA | ~49 dB | ~26 dBA | passes | ~43 dB |
| 69 dB SNR MEMS (IM69D130 class) | 25 dBA | ~42 dB | ~22 dBA | passes alone, little room margin | ~36 dB, fails |
| 68 dB SNR (TDK T5848 / T5838) | 26 dBA | ~41 dB | ~19 dBA | marginal; the room must be very quiet | ~35 dB, fails |
| 65 dB SNR MEMS (ICS-43434, SPH1668, UMA-8 capsules) | 29 dBA | ~38 dB | none | **fails alone** | ~32 dB |
| Two 65 dB MEMS summed | ~26 dBA | ~41 dB | ~19 dBA | marginal | ~35 dB |
| Seven 65 dB MEMS summed (UMA-8 v2, RAW) | ~20.5 dBA | ~46 dB | ~25.5 dBA | passes | ~40 dB |

Summing N capsules on the same signal lowers their uncorrelated self-noise
by 10 log10(N) relative to the speech.

- **The 8–12 kHz band** carries roughly 25–35 dB less energy than the
  overall speech level (long-term average speech spectrum, estimate). That
  is where hiss and fan noise show first, so measure that band's SNR on its
  own, not just the A-weighted total.
- **Sensitivity and bit depth.** A −26 dBFS digital MEMS puts speech at
  0.6 m near −53 dBFS on average, and −59 dBFS at 1.2 m. A 16-bit capture's
  floor (about −98 dBFS) then comes within a few dB of the microphone's own
  noise. Capture and store 24-bit, and normalise later. Today's recorder
  stores `pcm_s16le`.

## 2. Candidates

Prices on 2026-11-14. Retailer pages that refused automated reads were
priced from search results.

### USB microphones with a narrow pattern

| Product | Pattern | Range | USB format | Noise / sensitivity | Processing, and how it is turned off | Size, mass, mount | Power | Price |
|---|---|---|---|---|---|---|---|---|
| [Rode VideoMic GO II](https://edge.rode.com/pdf/products/849/VideoMic%20GO%20II_ds_V2.pdf) | supercardioid | 20 Hz–20 kHz | 24-bit / 48 kHz | 15 dBA; −31 dBV/Pa (datasheet) | High-pass, high-frequency boost and pad exist only in RØDE Central (Windows/macOS) in USB mode. Off unless set there; no Linux control | 120 × 21.6 mm, 33 g; cold-shoe mount | USB bus only | $99.99 at [Rode's store](https://brandstore.rode.com/products/video-mic-go-ii) (listing) |
| [Rode VideoMic NTG](https://edge.rode.com//pdf/products/633/VMNTG%20DS%2003.pdf) | supercardioid | 20 Hz–20 kHz (35 Hz–18 kHz ±3 dB) | 24-bit / 48 kHz | 15 dBA; −31 dBV/Pa (datasheet) | Pad, safety channel, high-frequency boost, high-pass (75/150 Hz) and gain on **buttons on the microphone**; each has an off setting | 171 × 21.6 mm, 94 g; cold-shoe mount | internal Li-ion, USB charging | $223–259 ([Trew Audio](https://www.trewaudio.com/product/rode-videomic-ntg/), listing) |
| [Shure MV88+](https://www.bax-shop.nl/downloads/products/9000-0070-5330/mv88+_specsheet_letter_en.pdf) | mid-side; mono cardioid or figure-8 in the app | 20 Hz–20 kHz | 16/24-bit, 44.1/48 kHz, UAC1 | self-noise not published; −37 dBFS/Pa at minimum gain | Presets, EQ, compressor and limiter set in the MOTIV app (not Linux); a flat mode exists | 27 × 27 × 77 mm, 79 g | USB bus | $249 (shure.com, listing) |
| [Audio-Technica AT2020USB-X](https://docs.audio-technica.com/all/AT2020USB-X-UM-EN.pdf) | cardioid | 20 Hz–20 kHz | 16/24-bit, up to 96 kHz | not in the manual | none (headphone mix and mute only) | 142 × 52 mm, 373 g | USB bus | about $130 (listing) |
| [Rode NT-USB Mini](https://edge.rode.com//pdf/products/96/NT-USB_Mini_ds_V06.pdf) | cardioid | 20 Hz–20 kHz | 24-bit / 48 kHz | not published | none listed | 142 × 55 mm, 585 g with stand | USB bus | about $102 (listing) |

Notes:
- **No product here publishes a humidity rating,** and none says whether it
  is UAC1 or UAC2, except the MV88+ (UAC1). The board's `snd-usb-audio`
  handles either.
- **Left out:**
  - The Sennheiser MKE 400 (2nd gen) and MKE 200 have analog output only.
  - The Shure MV6 and MV7+ and the Rode PodMic USB are close-talk dynamic
    microphones, and the MV6 has auto-level and a denoiser.
  - Boundary USB microphones (Audio-Technica ATR4697-USB, 50 Hz–15 kHz; the
    MXL AC-404, discontinued) are omni. They are the worst case in a tiled
    room.

### USB MEMS arrays

| Product | Capsules | USB format | Processing, and how it is turned off | Size | Price |
|---|---|---|---|---|---|
| [miniDSP UMA-8 v2](https://www.minidsp.com/images/documents/UMA-8%20v2%20User%20manual.pdf) | 7 × Knowles SPH1668LM4H, omni, 65 dB SNR (datasheet) | RAW firmware: 8 channels, 24-bit, up to 48 kHz. DSP firmware: 2 channels at 48 kHz (datasheet) | Beamforming, AEC, noise suppression, de-reverberation and AGC in the DSP firmware, which processes at 16 kHz (XVF3000). **Off only by flashing the RAW firmware;** then the host must form the beam | 90 mm disc, 14 mm (20 mm with the LED ring) | about $105 street; €188.95 at Sound Imports (listing) |
| [miniDSP UMA-16 v2](https://www.minidsp.com/images/documents/UMA-16%20v2%20User%20Manual.pdf) | 16 × SPH1668LM4H on a 42 mm grid | 16 channels, 24-bit, up to 48 kHz, UAC2 (datasheet) | none; the beam is formed on the host | 132 × 202 × 18 mm | €369.95 at Sound Imports (listing) |
| [Seeed reSpeaker XVF3800](https://github.com/respeaker/reSpeaker_XVF3800_USB_4MIC_ARRAY/tree/master/xmos_firmwares/usb) (today's array) | 4 MEMS | firmware v2.1.1 has a 2-channel 48 kHz image | XMOS documents the [pipeline](https://xmos.com/documentation/XM-014888-PC/html/modules/fwk_xvf/doc/datasheet/03_audio_pipeline.html) at 16 kHz with a sample-rate converter for a 48 kHz interface, and a range of 80 Hz–8 kHz. **The 48 kHz image is 16 kHz audio resampled.** Raw microphones come only in the 16 kHz image | — | already owned |

### Digital MEMS wired to the SoC (I2S or PDM)

All MEMS capsules are omni. Bottom port unless noted.

| Part | Interface | SNR / self-noise | AOP | Response | Ingress | Status | Evaluation board |
|---|---|---|---|---|---|---|---|
| **Infineon IM73D122** | PDM | 73 dB(A) at 3.072 MHz clock / 21 dBA (datasheet) | 122 dB SPL | low-frequency roll-off 20 Hz; +3 dB at 19 kHz (datasheet) | **IP57**, sealed dual membrane (datasheet) | active | KIT_IM73D122V01_FLEX, €74–92 at DigiKey (listing; marked discontinued at DigiKey Canada) |
| Infineon IM72D128 | PDM | 72 dB / 22 dBA | 128 | 20 Hz roll-off | sealed family; per-part rating not published | active | — |
| TDK T5848 | I2S, 24-bit | 68 dBA / 26 dBA (datasheet) | 133 | low-frequency cutoff 28 Hz | not published | in production | EV_T5848-FX2, $45.71 at DigiKey (listing) |
| TDK T5838 | PDM | 68 dBA | 133 | low-frequency cutoff 28 Hz | not published | in production | EV_T5838-FX2, €28.03 (listing) |
| TDK ICS-43434 | I2S, 24-bit | 65 dBA / 29 dBA (datasheet) | 120 | 60 Hz–20 kHz | not published | **end of life** | Adafruit 6049, about $6.45 (listing) |
| Knowles SPH0645LM4H-B | I2S | 65 dB(A) | 120 | curve not published | not published | listed obsolete | Adafruit 3421, $6.95 (listing) |
| Infineon IM69D130 | PDM | 69 dB(A) | 130 | 28 Hz roll-off | no claim | not for new designs | — |

The Adafruit 3492 PDM breakout ($4.95) carries an ST MP34DT01-M (about
61 dB SNR). It can prove the PDM path on the bench, but it is not corpus
grade.

Datasheets:
- [IM73D122](https://static.chipdip.ru/lib/911/DOC028911535.pdf) (a mirror
  of Infineon's v1.00 sheet);
- [Infineon's selection guide](https://www.infineon.com/assets/row/public/documents/24/66/infineon-xensiv-mems-microphones-productselectionguide-en.pdf);
- [T5848](https://invensense.tdk.com/products/digital/t5848/);
- [ICS-43434](https://invensense.tdk.com/products/ics-43434/).

**Ready-made board.** The [NXP 8MIC-RPI-MX8](https://www.digikey.com/en/products/detail/nxp-usa-inc/8MIC-RPI-MX8/11624630)
is 8 PDM microphones on a 40-pin header board: $272.33 at DigiKey, 4 in
stock (listing). NXP's kernel has a device tree for it on the FRDM
(`imx8mp-frdm-8mic-revE.dts`). Its microphone part is not published, so its
noise is unknown. It is the quickest way to prove the PDM path, not a
corpus microphone.

### Analog capsules with a small ADC

| Part | Type | SNR | Notes |
|---|---|---|---|
| Infineon [IM73A135](https://www.farnell.com/datasheets/3629245.pdf) | analog MEMS, differential | 73 dB(A) (datasheet) | IP57; AOP 135 dB; VDD at most 3.0 V; KIT_IM73A135V01_FLEX £55.17 (listing) |
| Primo [EM272](https://cdn.shopify.com/s/files/1/1998/4481/files/EM272.pdf) | electret, 10 mm | 80 dB (datasheet) | about 14 dBA. Needs bias and 20–30 dB of gain. No ingress rating, and electrets dislike condensation |

| ADC or codec | Spec | Front end | Linux driver | In the board kernel |
|---|---|---|---|---|
| TI TLV320ADC3140 | 106 dB, 4 analog or 8 PDM channels | 0–42 dB gain, programmable bias | `tlv320adcx140` | no (needs a config line) |
| TI PCM1863 | 110 dB differential | −12 to +32 dB gain | `pcm186x` | no |
| Wolfson WM8960 | ADC 94 dB(A) | microphone gain and bias | `wm8960` | yes, as a module |

The analog route can be the quietest on paper (an EM272 at about 14 dBA),
but it needs careful layout, a codec board and a SAI. It is the most work
and the most moisture-sensitive capsule. It is listed for completeness, not
recommended.

## 3. On the FRDM board

Facts below come from this repository's captures (raw records under the
ignored `hardware_tests/results/frdm/`) and from NXP's kernel tree at the
pinned tag `lf-6.12.49-2.2.0`, read on 2026-11-14 (`imx_v8_defconfig`,
sha256 `7f74e244…42241`). Nothing was installed or changed on the board.

- **USB host ports.**
  - There is one USB-A host port (J4) and one USB-C OTG port (J3)
    (UM12340).
  - In the mirror unit, J4 carries the gate board, which feeds the XVF3800
    core board. An earlier note found the XVF3800 does not enumerate on
    USB-C, so J3 as a host is unproven.
  - So a USB microphone in the mirror unit needs either a gate board with a
    small hub (two downstream ports behind the same gated 5 V), or to take
    the XVF3800's place.
  - On the bench, the owner can swap the XVF3800 for the test microphone on
    J4.
- **USB audio in the kernels.**
  - The vendor image (kernel 6.18.20) loads `snd_usb_audio`; the XVF3800
    runs on it today (`music-output-inventory-20261106`).
  - NXP's `imx_v8_defconfig`, which the Buildroot image uses, has
    `CONFIG_SND_USB_AUDIO=m`.
  - That driver handles USB Audio Class 1, 2 and 3. A class-compliant
    microphone needs no added software on either image.
- **PDM input (MICFIL).**
  - The SoC takes up to 8 PDM microphones on 4 data lines.
  - Mainline `fsl_micfil` offers 8–48 kHz for the 8M Plus, and 48 kHz is
    enough for 24 kHz training.
  - On the FRDM the pads are SAI5: `SAI5_RXC` is the PDM clock and
    `SAI5_RXD0`–`RXD3` are data lines 0–3, all on J18. The bank
    (NVCC_SAI1_SAI5) is 3.3 V ([J18 inventory](frdm-j18-pir-candidates.json)).
  - The vendor and panel device trees enable `micfil` with a `sound-micfil`
    card ("micfil-audio"), and the running board lists it as card 3.
  - `CONFIG_SND_SOC_FSL_MICFIL`, `CONFIG_SND_SOC_IMX_CARD` and
    `CONFIG_SND_SOC_DMIC` are modules in `imx_v8_defconfig`.
  - **So PDM microphones need no kernel or device-tree change,** only
    wiring.
- **J18 pins.**
  - The repository records `SAI5_RXD3` on J18-35 and `SAI5_RXFS` on J18-32.
    The pins for the PDM clock and data lines 0–2 are not recorded here, so
    read them from the B1 schematic before wiring.
  - The quick-start drawing names J18 pin 3 "GPIO 2 (RXC)", and pins 3 and 5
    are still disputed ([frdm-bringup.md](../operations/frdm-bringup.md)):
    nothing goes on them until that settles.
  - The PIR's first-choice pad (J18-12, `GPIO1_IO01`) does not collide with
    the PDM pads.
- **I2S instead of PDM.**
  - SAI2 and SAI3 serve Bluetooth and the WM8962 codec, and SAI1's J18 links
    are not fitted.
  - An I2S microphone or ADC on J18 would therefore use the SAI5 pads in
    I2S mode, in place of MICFIL. That needs a device-tree overlay.
  - It also needs a kernel config line: `CONFIG_SND_SOC_ICS43432`,
    `TLV320ADCX140` and `PCM186X` are not in `imx_v8_defconfig`.
  - PDM is the shorter path.
- **The privacy gate (packaging plan R4)** applies to any new microphone:
  its power must depend on the switch and the presence gate, not on Linux.
  - **A bus-powered USB microphone** behind the gated 5 V meets it as the
    XVF3800 does.
  - **The VideoMic NTG does not:** its internal battery keeps it powered
    when VBUS is cut. Its USB link drops, but R4 as written says "off".
  - **PDM or I2S microphones** need a gated 3.3 V. The SoC also drives the
    PDM clock, which would back-power an unpowered microphone through its
    clock pin's protection diodes. So the clock must pass a buffer powered
    from the gated rail with an Ioff input (74LVC1G125 class). That is a
    gate-board change.

### What each option needs in the project

| Option | Board / image | media_broker and recorder | voice-lab |
|---|---|---|---|
| USB microphone | nothing on the vendor image. On Buildroot, check that `snd-usb-audio` loads on hot-plug as the XVF3800 already needs. A gate board with a hub for the mirror unit | A capture profile for a second card: one channel, 48 kHz, S24 (`CaptureSource` defaults to 16 kHz, 6 channels) | a new capture profile (below) |
| PDM MEMS on MICFIL | wiring to J18, a gated 3.3 V and a clock buffer; no kernel or device-tree change | the same, on the `micfil-audio` card; the beam (if any) formed on the host, raw channels kept | the same |
| I2S MEMS or ADC on SAI5 | a device-tree overlay, a kernel config line, and the gated rail | the same | the same |

**voice-lab's contract is the real gate.**
- The pinned `corpus-client.v1` refuses a take at 48 kHz ("takes 16 kHz
  audio"), from another product ("product 0x0022"), or of any profile but
  `xvf3800-linear-4`. See the fixtures under
  `voice_lab_recorder/tests/fixtures/corpus_client_v1/examples/`.
- Its continuity rule is in integer nanoseconds: 62,500 ns a frame at
  16 kHz. At 48 kHz a frame is 20,833.3 ns, so the rule needs a form that
  does not assume a whole number.
- Recording full-band takes needs voice-lab to add a profile. That is
  voice-lab's change, asked for through a request once the owner picks a
  microphone.

## 4. The bathroom

- **Placement.**
  - Aim the microphone at the mouth, not at the chest.
  - Keep to the 0.6 m design distance. Drifting back to 1.2 m costs 6 dB,
    more than any microphone upgrade. A floor mark helps.
  - Keep the microphone away from the speaker and any vent
    ([enclosure plan](enclosure-plan.md)).
  - A tube microphone should clear the enclosure edge, so its side slots
    are open.
- **Treatment for a session**, all removable, no installation:
  - door open;
  - towels over the shower glass;
  - a bath mat on the floor;
  - the shower curtain closed, if there is one.

  These bring T60 from about 0.8 s to about 0.34 s in the assumed room
  (estimate). Most of the gain is above 1 kHz, where tile is most
  reflective.
- **Noise.**
  - Turn the fan off during takes.
  - Wait for the cistern to refill and for water in the pipes to stop.
  - Record the noise floor before each session ([below](#what-to-measure-first)).
- **Steam.**
  - Never record during or after a shower.
  - Condensation on a condenser or electret capsule causes crackle until it
    dries.
  - The IM73D122 and IM73A135 are IP57 at component level. An acoustic vent
    such as the Gore GAW334 (IP67/68, under 2 dB loss at 1 kHz for a 1.6 mm
    port; [datasheet](https://www.gore.com/sites/g/files/ypyipe116/files/2017-01/GORE-Acoustic-Vent-GAW334-Datasheet-en.pdf))
    would cover the front-face port. Its loss at 8–12 kHz is not published
    and has to be measured.
  - The USB microphones have no rating. A removable shoe lets the owner
    take one out of the room between sessions.

## 5. Two microphones

The owner asked whether a second microphone could help, for example as an
"echo reference" to subtract the reverberation.

### Why an echo reference cannot subtract reverberation

- **There is no separate echo source.** Reverberation is the talker's own
  voice, delayed and filtered by the room. A second microphone hears the
  same voice through a different room path: different arrival times,
  different reflections.
- **The two reverberant signals are decorrelated.** In a diffuse field, the
  coherence between two points falls as sin(kd)/(kd). It drops below 0.5
  above about 104 / d Hz: about 350 Hz for microphones 30 cm apart, and
  about 2 kHz for 5 cm. Above that, no filter can predict one microphone's
  reverberation from the other's. Below it, the microphones hear nearly the
  same sound, direct voice included, so there is nothing separate to
  subtract.
- **Any canceller would also cancel the voice.** An adaptive canceller
  removes whatever in the main channel correlates with the reference.
  Here the strongest common part is the voice itself, so the canceller
  would take out the voice and leave the reverberation.

### What does work

- **Cancelling a separate noise source with a reference microphone.** A
  microphone at the fan hears mostly the fan and little voice, and an
  adaptive filter can remove the fan's coherent part (hum, tones) from the
  main channel. Broadband turbulence is less coherent at a distance, so it
  cancels poorly. For corpus takes, turning the fan off is simpler and
  better.
- **Echo cancellation (AEC) of the panel's own speaker.** The speaker's
  signal is known exactly, so AEC works. That is what the XVF3800 does for
  commands. For corpus takes the panel should simply play nothing.
- **Beamforming.** Several microphones with known geometry, combined toward
  the talker, raise the DRR by the array's directivity index:
  - a two-microphone broadside pair gives up to about 3 dB, where its
    spacing is at least half a wavelength;
  - a close endfire pair, processed as a differential microphone, gives a
    cardioid to hypercardioid (up to about 6 dB). It costs low-frequency
    noise gain, so it needs quiet capsules.
- **Multichannel dereverberation (WPE).** Weighted prediction error
  (Nakatani et al., 2010) predicts the late reverberation of each channel
  from the delayed past of all channels, and subtracts it.
  - It keeps the direct sound and early reflections, works offline, and
    gets better with more channels. It is widely used in front of speech
    recognition (the open-source `nara_wpe`).
  - It also works on one channel, less well.
  - Neural dereverberation exists too, but it can change the timbre that
    F5-TTS is meant to learn, so it is the riskier choice.
- **These need channels sampled on one clock.** Beamforming and WPE assume
  the channels share a sample clock:
  - MICFIL's PDM channels do;
  - two separate USB devices (for example the GO II beside the XVF3800) do
    not. They drift against each other, and the XVF3800 stops at 8 kHz in
    any case.

  So multichannel processing favours the built-in MEMS route, or a USB
  array in RAW mode.

### Process offline, keep the raw channels

- Any processing (beamforming, WPE, noise cancellation) should run offline,
  in voice-lab, on stored raw channels, and never on the panel at record
  time.
- With versioned settings, training runs can then be compared with and
  without each step and redone when a method improves.
- Today's capture profile already works this way: it records the XVF3800's
  raw microphone channels with `processing: []` beside its processed
  channels. A full-band profile should keep that rule.

## 6. Shortlist

1. **Best overall, and best if we stay with USB: Rode VideoMic GO II**
   (about $100). Unchanged by the move to 0.6 m.
   - Supercardioid at 15 dBA, with USB audio at 24-bit/48 kHz. At 0.6 m with
     the session treatment it reaches about balanced direct and
     reverberant sound.
   - It is bus-powered, so the existing gate covers it, and it has no
     processing unless someone turns it on in RØDE Central.
   - The board's kernel already drives it, and it can be tested on the
     bench with no software change.
   - Costs:
     - a 120 mm tube on a shoe on top of the enclosure, about 70 mm proud
       of the front;
     - no humidity rating;
     - its processing settings are not visible from Linux, so check
       flatness by measurement;
     - one channel only, so dereverberation is single-channel.
2. **Best if we go I2S/PDM: two to four Infineon IM73D122 on MICFIL**, flush
   behind vents in the front face.
   - They are invisible, IP57, 73 dB SNR and flat to 19 kHz within 3 dB, on
     an input the kernel and device tree already support.
   - At 0.6 m the gap to the GO II is about 3 dB of DRR above 1.5 kHz and
     about 6 dB below.
   - Their raw channels on one clock allow beamforming and WPE offline,
     which can close part of that gap.
   - Costs:
     - a small board or the flex kit;
     - a gate-board change for the gated 3.3 V and the clock buffer;
     - an enclosure change for the ports.
3. **Best budget: still the GO II.**
   - At 0.6 m a 68–69 dB SNR MEMS passes on noise, but only with a very
     quiet room. As an omni it hears more reverberation, and it still needs
     the same board, gate and enclosure work as the IM73D122.
   - The $5–7 breakouts (65 dB SNR) still fail alone. They prove the I2S or
     PDM path, not corpus quality.
4. **Runners-up:**
   - **Rode VideoMic NTG** (buttons instead of an app, a slightly longer
     tube, but its battery breaks R4).
   - **miniDSP UMA-8 v2 in RAW mode.** A flush disc. At 0.6 m its seven
     capsules summed pass on noise, and its raw channels allow beamforming
     and WPE, but the beam has to be written.
   - **TDK T5848** (I2S, 68 dB) if PDM were ruled out. Marginal on noise
     alone.

**Recommendation:**
1. Measure the room first with what is already on the board.
2. If the owner approves, buy one GO II and record test takes at 15 cm,
   0.6 m and 1.2 m, beside the XVF3800, with and without the session
   treatment.
3. Decide on the built-in MEMS route only if a removable tube on the panel
   is unacceptable to look at, or if offline multichannel dereverberation
   turns out to be needed.

Either way, ask voice-lab for a full-band capture profile once the owner
picks.

## What to measure first

**Step 0: no purchase, the XVF3800 as it is.** Its raw channel is 16 kHz, so
these are valid below 8 kHz:
- **T60 per octave (250 Hz–4 kHz).** Use a hand clap or a balloon pop from
  the talker's position, or a sine sweep from a phone at head height, then
  T20 by Schroeder integration. Do it with and without the session
  treatment.
- **Noise floor.** 30 s of silence with the fan off and then on, as dBFS
  per octave and an A-weighted total.
- **Distance pair.** The same sentence read at 15 cm, 0.6 m and 1.2 m.
  Compare the level drop with the 18 dB the inverse-square law predicts:
  any shortfall is the room. Also estimate C50 from the clap response.

**Step 1: with a test microphone (after the owner's go-ahead to buy).**
Record on any computer first, then on the bench. On the vendor image the
USB microphone needs nothing installed; the capture writes only to `/tmp`.
- the same three distances, at 48 kHz, 24-bit;
- the noise floor, including the 8–12 kHz band's SNR;
- the spectrum at 0.6 m (and 1.2 m) against 15 cm above 4 kHz, for high-frequency loss
  and the mirror's ripple;
- with the GO II's RØDE Central settings confirmed off: a flat response
  between the 15 cm and 0.6 m takes, apart from the room;
- voice-lab's own take QC on a short read, and a listening check of a short
  fine-tune, before any full corpus session.

**Pass, as a starting target:**
- at least 40 dB speech-to-noise in the 0.6 m take;
- the 8–12 kHz band at least 20 dB above the floor;
- a DRR or C50 estimate at 0.6 m near 0 dB with the session treatment.

## Decisions for the owner

1. **Direction.** Choose one:
   - the removable USB microphone (recommended);
   - the built-in MEMS route;
   - both: USB now, MEMS in a later enclosure revision.
2. **Test purchase.** One Rode VideoMic GO II, about $100. Nothing is bought
   without this.
3. **Session treatment.** Door open, towels on the glass, fan off, for
   recording sessions only.
4. **Privacy (R4).** A new microphone must be behind the gate. That means
   no battery-powered microphone, or an amended R4. A USB microphone in the
   mirror unit needs a gate board with a hub.
5. **voice-lab.** Once a microphone is picked, the panel's agent files a
   request asking voice-lab for a full-band capture profile (48 kHz, 24-bit,
   the new device). That is voice-lab's decision.
6. **Measurements on the board.** Step 0 reads the array into `/tmp` only.
   Say whether the agent may run it, or whether the owner runs it.

## Method

- **Level:** free-field inverse square, 20 log10(r1 / r2). Speech level from
  ANSI S3.5's normal vocal effort (about 62 dB SPL at 1 m), so about 67 dB SPL
  at the 0.6 m design distance and 61 dB SPL at 1.2 m.
- **Reverberation:**
  - Sabine: A = 0.161 V / T60.
  - Critical distance r_c = 0.057 √(Q V / T60).
  - DRR = 20 log10(r_c / r).
  - The room is assumed, 2.2 × 1.8 × 2.4 m, T60 0.5–1.2 s; the real one
    needs measuring.
  - Treatment adds about 2.6 m² of absorption above 1 kHz: an open door of
    about 1.6 m², plus towels and a mat of about 1 m².
- **Directivity:** first-order patterns (Q = 3, 3.7, 4). Short shotgun and
  array figures are order-of-magnitude, from tube length and aperture
  against wavelength.
- **Noise:**
  - self-noise = 94 dB − SNR for MEMS parts;
  - N summed capsules gain 10 log10(N) over their uncorrelated noise;
  - room noise left = 10 log10(10^(27/10) − 10^(self-noise/10)) dBA at
    0.6 m.
- **Two microphones:** diffuse-field coherence sin(kd)/(kd), equal to 0.5
  at kd ≈ 1.9.
- **Comb filtering:** the first notch of a reflector at distance d is at
  c / (4d), with c = 343 m/s.
- **Kernel facts:**
  - the running vendor image's module list and sound cards (records under
    `hardware_tests/results/frdm/`);
  - NXP's `imx_v8_defconfig` and `imx8mp-frdm.dts` at `lf-6.12.49-2.2.0`.
