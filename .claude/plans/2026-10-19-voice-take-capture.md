# Plan: Real Audio Capture with a `voice_take` Purpose

<!-- @canonical voice-take-capture-plan -->

**Status (2026-10-19):** In progress (issue #22, phase 2). The decisions below
are the agent's, taken where the issue, the ADRs and voice-lab's D-056 leave a
choice; the owner may change them before the step that depends on them
merges. Steps 1–6 need no tailnet. Step 7 needs the recorder surface from the
[voice-lab recorder plan](2026-10-19-voice-lab-recorder-groundwork.md),
Step 6.

**Goal:** the panel records a take from the XVF3800 array through one audio
owner, the media broker, under a touch-started `voice_take` lease that closes
on stop, on the maximum duration, on lifecycle closure and before suspend. The
take is the audio exactly as ALSA delivered it plus continuity evidence (the
hardware parameters, the xrun count, and one hardware timestamp per period).
Host tests run the same broker on a deterministic fake source.

**Fixed by the owner (issue #22):**
- The microphone is the Seeed reSpeaker Flex XVF3800 Linear-4 on USB (UAC
  2.0). The WM8962 headset jack is for bring-up only.
- One audio owner: `voice_take` is a broker purpose beside
  `command_recognition` and `wish_recording`, never a second device owner
  ([application capability ownership](../../docs/architecture/application-capability-ownership.md)).
- Prefer the array's raw or least-processed channels and record which
  processing ran.
- Recording and upload run outside `bhp-panel-ui`.
- R4 (the microphone is off when deactivated) stays a hardware property: the
  5 V switch and the presence gate. Nothing here relies on software for it.

**Out of scope:**
- the upload, the take package voice-lab admits, and sign-in: issue #23 and
  its [groundwork plan](2026-10-19-voice-lab-recorder-groundwork.md); the
  wire format waits for voice-lab's `contracts/corpus-client.v1/` bundle;
- `command_recognition` and `wish_recording` on real audio: the recognizer
  choice is still open, and both stay on the host fake;
- the 5 V load switch, the privacy switch and the mic-rail LED (mirror-unit
  plan R4);
- the production Buildroot image's kernel audio options and device
  permissions, which are recorded as a risk below.

## Starting point

- **The array is attached to the board.** Read over ssh on 2026-10-19: ALSA
  card 6, `reSpeaker Flex XVF3800 L16K6Ch`, USB `2886:0022` at
  `usb-xhci-hcd.1.auto-1` (high speed). Capture has one mode: `S16_LE`,
  6 channels, 16000 Hz, channel map `FL FR FC LFE RL RR`; playback is 2
  channels at 16000 Hz. The vendor image has `arecord`, `amixer` and
  `libasound.so.2`. An earlier non-gate note
  (`hardware_tests/results/frdm/panel-j27-20261018T050000Z/notes.txt`) adds
  that it did not enumerate on a USB-C port.
- **The broker is a host fake.** `media_broker/fake_media_broker.py` speaks
  JSON Lines over the simulator's private pipes, opens no device, and refuses
  every purpose except `command_recognition`. The production image has no
  Python, so a real broker is C++.
- **The lease design exists on paper.** ADR 0005 and
  [process interconnectivity](../../docs/architecture/process-interconnectivity.md)
  define the topology: the supervisor publishes a lifecycle window to the
  broker; the UI asks the broker for a purpose-scoped lease with a touch
  observation ID; the broker claims that observation from the supervisor
  (`TouchObservationAuthority::redeem_for_media_broker`) under its exact UID.
  The supervisor's touch observer and authority exist; the broker-facing
  socket does not.
- **Capture closure before suspend exists.**
  `CaptureClosureGatedSuspend` reads every `/proc/asound/card*/pcm*c/sub*/status`
  and every process's open `/dev/snd/pcmC*D*c` before an enabled suspend, and
  can terminate only the exact broker UID
  (`BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID`). It covers a USB card unchanged.
- **FRDM-GATE-AUDIO has not run.** `frdm_mic_capture.sh` captures the WM8962
  headset path at 48 kHz stereo; the board profile leaves
  `BHP_BOARD_AUDIO_CAPTURE_DEVICE` unmeasured.
- **What voice-lab has built** (voice-lab main `d89d530`, read 2026-10-19).
  Its `alsa-continuity/v1` model and the `xvf3800-linear-4` capture profile
  exist in code (`src/cole_voice_lab/alsa_evidence.py`,
  `capture_profiles.py`), but the pinnable bundle is its last step, and
  voice-lab asked the panel not to build its wire format before then. Their
  rule recomputes per-period deviation from integer
  `[position_frames, timestamp_ns]` pairs at `1_000_000_000 // rate` ns per
  frame, needs zero xruns and `buffer_frames >= 2 * period_frames`, and
  accepts at most 660 s at 16 kHz. The profile records the firmware
  (`VERSION`, `BLD_MSG`, `BLD_REPO_HASH`) and nine processing settings read
  at the start of the take.

## Decisions

1. **The broker's own take record (taken).** The broker writes
   `original.wav` (a plain 44-byte PCM header, then the samples exactly as
   ALSA delivered them) and `capture.json`, a panel record
   (`bhp.media.voice-take/v1`). The record carries the integers voice-lab's
   rule needs under the same names where they exist (`period_frames`,
   `buffer_frames`, `xruns`, `periods`, `tstamp_type`, `frames`), plus the
   device, firmware and settings read at the start. The voice-lab files
   (`continuity.json`, `manifest.json`, `qc.json`, `READY`) are written by
   issue #23's packager from the pinned bundle. Alternative: write voice-lab's
   `continuity.json` now from its as-built code; rejected because the bundle
   is the contract voice-lab asked the panel to wait for.
2. **All six channels, untouched (taken).** The broker opens `hw:` (no `plug`
   layer), refuses any mode other than the one it asked for, and never
   resamples, extracts channels or applies gain. Which channel is raw is
   recorded from the firmware's routing read, not assumed.
3. **Takes end at 600 s (taken).** A take stops on the lease holder's stop
   (`requested`), at 600 s (`maximum-duration`), or as a failure on lifecycle
   closure, UI disconnect, an xrun or a device error. voice-lab accepts at
   most 660 s; 600 s matches issue #22.
4. **Takes stay in the broker's directory (taken for #22).** Completed takes
   go in a bounded, owner-only directory (a count and a byte ceiling; full
   refuses a new lease with a structured error). How the issue #23 uploader
   reads them is decided in that plan.

## Steps

- [ ] **Step 1: Run FRDM-GATE-AUDIO on the array.**
  - `frdm_mic_capture.sh --source array` finds the USB array by vendor
    `2886` in `/proc/asound/card*/usbid`, records `stream0`, `usbid` and the
    mixer as artifacts, and checks:
    - `alsa_card`: a USB card with vendor `2886` and `XVF3800` in its name;
    - `capture_mode`: `stream0` offers the capture mode the record names
      (`S16_LE`, 6 channels, 16000 Hz);
    - `capture_non_silent`: per-channel peak and RMS, with the existing
      thresholds on the loudest channel.
  - The default `--source headset` keeps today's WM8962 behaviour.
  - Run it on the board (a 5 s capture into `/tmp`, nothing installed), then
    copy the `BHP_BOARD_AUDIO_CAPTURE_DEVICE` candidate into
    `config/boards/frdm-imx8mp.env` and set `BHP_BOARD_AUDIO_MIC_SOURCE` to the
    array.
  - Update the gate rows in `docs/operations/frdm-bringup.md`, ADR 0008's
    gate table (as an amendment) and `hardware_tests/README.md`.
  - Verify:
    - `python3 -m unittest hardware_tests.tests.test_frdm_evidence_scripts` exit 0;
    - an FRDM-GATE-AUDIO record from the board passes `alsa_card`, `capture_mode` and `capture_non_silent` with `--source array`.
- [x] **Step 2: The capture core and a deterministic fake source.** (2026-10-19)
  - A C++ library in `media_broker/` (`bhp_media_capture`):
    - `CaptureSource` (open with an exact format, read one period with its
      hardware position and timestamp, close);
    - `FakeCaptureSource`: deterministic samples and timestamps, with
      injectable xruns, timestamp jumps and device loss;
    - `ContinuityLog`: the `[position_frames, timestamp_ns]` pairs, the xrun
      count and the maximum absolute deviation, by voice-lab's as-built rule;
    - `TakeWriter`: streams `original.wav` to a temporary directory, fixes the
      header at the end, writes `capture.json` (sorted keys, compact,
      trailing newline), fsyncs, and renames the directory into place;
    - `TakeStore`: the bounded directory, crash leftovers removed at start.
  - Tests cover the WAV header and size rule, each stop reason, every
    injected failure, the deviation rule against voice-lab's worked numbers,
    and the store bounds.
  - Done: `media_broker/include/media_broker/{capture_source,continuity_log,take_store}.h`
    (`bhp_media_capture`), tested by `media_broker/tests/media_capture_test.cpp`.
    Only a clean take commits; a failed or abandoned take leaves no files.
  - Verify:
    - `ctest --test-dir .build/host-dev -R media_capture` exit 0;
    - `make verify` exit 0.
- [x] **Step 3: ALSA capture and the array's identity on the board.** (2026-10-19)
  - `AlsaCaptureSource` (libasound): `hw:` device, interleaved read access,
    exact format, rate and channels, period and buffer sizes it asked for and
    records, `SND_PCM_TSTAMP_ENABLE` with `MONOTONIC_RAW` timestamps,
    one `snd_pcm_htimestamp` pair per period, and xruns counted, never
    recovered silently.
  - `Xvf3800Control`: read-only vendor control reads over usbdevfs (`VERSION`,
    `BLD_MSG`, `BLD_REPO_HASH`, the nine settings voice-lab's profile names,
    and the output routing), following the XMOS read protocol (vendor IN
    request 0, `wValue = 0x80 | cmdid`, `wIndex = resid`, a status byte
    first). No write command is implemented.
  - `bhp-media-capture-check --seconds N --take-root DIR`: one take through
    the Step 2 writer, printing a JSON summary.
  - Add `libasound2` and its development package to
    `scripts/build/frdm_vendor_sysroot.sh` (pinned by SHA-256) and to CI's
    apt step, and the tool to the FRDM demo bundle (`libasound.so.2` in
    `ALLOWED_NEEDED`).
  - On the board, run a 30 s and a 600 s take into `/tmp`, and read the
    capture-closure probe (`capture_closure_check`) during and after one.
  - Done: `AlsaCaptureSource`, `Xvf3800Control` and `bhp-media-capture-check`
    in `media_broker/`. On the board, a 30 s and a 600 s take had zero xruns
    and a maximum deviation of 0.32 ms and 0.36 ms; the firmware read `1.0.0`
    (`ua-io16-6ch-lin`). The probe saw the capture open during the take and
    closed after it. Measured facts:
    [XVF3800 array on the FRDM board](../../docs/hardware/xvf3800-array-on-frdm.md).
  - Verify:
    - `ctest --test-dir .build/host-dev -R media_capture` exit 0;
    - a board record under `hardware_tests/results/frdm/voice-take-<UTC>/` shows a 30 s and a 600 s take with zero xruns, the measured maximum deviation, the firmware strings and the routing;
    - the capture-closure probe reports the array's capture open during the take and closed after it.
- [ ] **Step 4: The supervisor's broker socket.**
  - A Unix socket in `panel_supervisord`, accepted only from the exact broker
    UID (`SO_PEERCRED`; the broker checks the supervisor's UID and the
    socket's root ownership):
    - the lifecycle window: `open` while the panel is day-active with
      presence asserted, `closed` on absence, night or suspend preparation,
      each message bound to the supervisor session and a sequence;
    - the claim: one touch observation ID and an operation digest in, a
      `TouchProvenanceEnvelope` or a structured refusal out
      (`redeem_for_media_broker`).
  - Suspend preparation closes the window before the capture-closure gate
    runs, so a well-behaved broker stops first and the gate stays the
    backstop.
  - Tests: wrong UID, a replay, a gap, a restart, an expired or consumed
    observation, a closed window, and the suspend ordering.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'media_window|panel_supervisor'` exit 0;
    - `make verify` exit 0.
- [ ] **Step 5: The broker daemon with `voice_take` leases.**
  - `bhp-media-broker`, C++, one process and the only audio owner:
    - a UI socket accepted only from the exact UI UID;
    - `arm` (purpose `voice_take`, touch observation ID) claims the
      observation from the supervisor and returns an opaque lease ID;
    - `start` (lease ID only, within 1 s) opens the device;
    - `stop`, 600 s, window closure, UI disconnect, an xrun or a device error
      end the take; a closed lease is terminal;
    - while active, a level event (peak dBFS per channel) about every 100 ms;
    - the end reply names the take ID, the stop reason and the summary;
    - `--fake-source` for hosts and the simulator.
  - Error codes reuse the fake's `MEDIA_BROKER.*` names where they mean the
    same thing.
  - Tests run the daemon against a fake supervisor and the fake source.
  - Verify:
    - `ctest --test-dir .build/host-dev -R media_broker` exit 0;
    - `make verify` exit 0.
- [ ] **Step 6: The UI's lease client and the simulator.**
  - `MediaLeaseClient` in `panel_ui` (connect, arm, start, stop, level and end
    events), used by the recorder surface in the groundwork plan.
  - The simulator runs `bhp-media-broker --fake-source` with a fake
    supervisor window and touch claims, so a recorder scenario records a
    deterministic take.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'media_lease|panel_simulation'` exit 0;
    - `make verify` exit 0.
- [ ] **Step 7: A touch-started take on the board.**
  - Needs the recorder surface (groundwork plan Step 6) and the owner's
    go-ahead for anything outside `/tmp`.
  - The FRDM demo bundle runs the broker under its own UID next to the
    supervisor and the UI; a tap on the recorder records a 30 s take, and a
    presence lapse during a second take ends it as a failure.
  - Verify:
    - a board record under `hardware_tests/results/frdm/voice-take-<UTC>/` shows the touch-started take, its capture record, and the window-closure failure;
    - the owner's confirmation that the take sounds right on headphones.

## Risks

- **The production image.** The Buildroot kernel fragment never names
  `SND_USB_AUDIO`, and `/dev/snd` is `root:root 0600` there; the broker needs
  the card and its usbdevfs node. Both are image changes for the display and
  audio cutover, not this plan.
- **Firmware reads.** The control protocol comes from Seeed's public host
  code for the XVF3800 USB arrays. If the Flex firmware answers differently,
  the record says `unread`, never a guessed value.
- **A 600 s take on a busy board.** At 16 kHz × 6 × 2 bytes a take is about
  115 MB; on the vendor image `/tmp` is RAM. The broker checks free space
  before `start` and refuses with a structured error.
- **USB-C.** The array did not enumerate on a USB-C port; it stays on USB-A.
