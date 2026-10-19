# XVF3800 Array on the FRDM Board

<!-- @canonical xvf3800-array-on-frdm -->

**Status (2026-10-19):** measured on the vendor image over ssh, as part of the
[voice-take capture plan](../../.claude/plans/2026-10-19-voice-take-capture.md),
Steps 1 and 3. Raw records are in the ignored
`hardware_tests/results/frdm/gate-audio-array-20261019T202043Z/` and
`hardware_tests/results/frdm/voice-take-20261019T203600Z/`. Nothing here is a
production default until a reviewed change copies it into the board profile.

## What the board sees

- **USB:** vendor `0x2886`, product `0x0022`, "reSpeaker Flex XVF3800
  L16K6Ch", on `usb-xhci-hcd.1.auto-1` at high speed (a USB-A port; an earlier
  note found it does not enumerate on USB-C).
- **ALSA:** card `L16K6Ch` (card 6 on the vendor image), device 0. Capture
  offers one mode: `S16_LE`, 6 channels, 16000 Hz, channel map
  `FL FR FC LFE RL RR`. Playback is 2 channels at 16000 Hz. The USB mixer
  exposes playback and capture volume and switch controls; nothing here
  changed them.
- **Firmware** (read-only XMOS control reads, `bhp-media-capture-check`):

  | Parameter | Value |
  |---|---|
  | `VERSION` | `1.0.0` |
  | `BLD_MSG` | `ua-io16-6ch-lin` |
  | `BLD_REPO_HASH` | `57dbcf3e6e46b024040a04cb0331fd6ab93de3c6` |

- **Processing settings at the start of a take:**

  | Parameter | Value |
  |---|---|
  | `SHF_BYPASS` | `0` |
  | `AEC_HPFONOFF` | `2` |
  | `PP_AGCONOFF` | `1` |
  | `PP_AGCGAIN` | `32.0` |
  | `PP_LIMITONOFF` | `1` |
  | `PP_MIN_NS` | `0.15` (float32) |
  | `PP_MIN_NN` | `0.51` (float32) |
  | `PP_ECHOONOFF` | `1` |
  | `PP_NLATTENONOFF` | `1` |

- **Output routing:** `AUDIO_MGR_OP_L` = `[8, 0]`, `AUDIO_MGR_OP_R` =
  `[7, 3]` (category, source). What these categories mean, and so which of the
  six channels carry raw microphones, is **not measured**. voice-lab's D-056
  expects channels 3–6 to be the raw microphones. In a quiet room the first
  channel was the loudest (peak -54.3 dBFS) and channels 3–6 sat together
  near -85 dBFS RMS. That fits the expectation but does not prove it.

## Capture continuity

`bhp-media-capture-check` opened `hw:CARD=L16K6Ch,DEV=0` with 1024-frame
periods and a 4096-frame buffer (both granted as asked), `MONOTONIC_RAW`
timestamps, and read one `snd_pcm_htimestamp` pair per period.

| Take | Frames | Xruns | Max deviation | Median | 99th percentile |
|---|---|---|---|---|---|
| 30 s | 479,232 | 0 | 321,375 ns | 30,750 ns | 319,500 ns |
| 600 s | 9,600,000 | 0 | 362,375 ns | 28,625 ns | 319,625 ns |

Deviation is voice-lab's as-built rule, `(ts[i] - ts[i-1]) - (pos[i] -
pos[i-1]) * 62500`. Every pair had zero frames waiting, so each read finished
right at a period boundary. The 600 s WAV was 115,200,044 bytes, exactly
`44 + frames * 12`.

## Capture closure

The supervisor's check-only probe (`capture_closure_check`, no broker UID, so
it could only refuse) reported one holder and one open substream on the
array during the take, and refused the suspend with
`PANEL_SUPERVISOR.CAPTURE_TERMINATION_UNAVAILABLE`. After the take it read the
substream `closed` and verified closure. The probe needed no change for a USB
card. No suspend was written.

## Not measured yet

- **FRDM-GATE-AUDIO:** the gate run on 2026-10-19 passed `alsa_card`,
  `capture_mode` and `capture_opened`. It failed `capture_non_silent` because
  the room was quiet. The gate needs a run with someone speaking.
- Which channels are raw, and what the routing categories mean.
- Behaviour across a USB re-enumeration (the mirror unit's switched 5 V rail)
  and across suspend.
- The production Buildroot kernel's USB audio support and device permissions.
