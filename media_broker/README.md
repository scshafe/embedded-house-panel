# Fake Media Broker

**Amended 2026-10-19 (issue #22):** `media_broker/` now also holds the C++
capture core for the real broker's `voice_take` purpose
([voice-take capture plan](../.claude/plans/2026-10-19-voice-take-capture.md),
Steps 2, 3 and 5):
- `CaptureSource` is the one place audio enters. `FakeCaptureSource` is the
  deterministic host and simulator source, with injectable xruns, timestamp
  steps and device loss.
- `ContinuityLog` keeps one `[position_frames, timestamp_ns]` pair per period
  and the maximum deviation by the rule voice-lab built for its ALSA
  continuity evidence. voice-lab's pinned contract, when it lands, is
  authoritative.
- `TakeWriter` streams `original.wav` (a 44-byte PCM header, then the samples
  untouched) and commits `capture.json` (`bhp.media.voice-take/v1`) with the
  WAV in one rename. Only a clean take commits: no xrun, the deviation within
  tolerance, and the last position within the buffer. Everything else is
  erased.
- `TakeStore` is the bounded owner-only directory of takes.

- `AlsaCaptureSource` (`BHP_BUILD_MEDIA_ALSA=ON`, libasound) opens a `hw:`
  PCM with no resampling and the exact format, rate and channels; an xrun is
  `MEDIA_CAPTURE.XRUN`, never recovered. Each period's pair comes from
  `snd_pcm_htimestamp` with `MONOTONIC_RAW` timestamps.
- `Xvf3800Control` reads the array's firmware identity, nine processing
  settings and output routing through usbdevfs vendor IN requests. It has no
  write path.
- `bhp-media-capture-check --pcm NAME --seconds N --take-root DIR` records one
  take through all of the above and prints a JSON summary. It is a bring-up
  and evidence tool, not the broker: it takes no lease.

- `MediaBroker` and `bhp-media-broker` (Step 5) are the one audio owner.
  - It listens for the UI on `--ui-socket` (exact `--ui-uid`) and speaks
    `shared/media_lease_protocol`.
  - It follows the supervisor's window on `--window-socket` (exact
    `--supervisor-uid`) through `shared/media_window_protocol`.
  - It serves `voice_take` only; other purposes are
    `MEDIA_BROKER.PURPOSE_DISABLED`.
  - A lease runs `arm`, then the broker claims the touch observation for a
    digest of the request, then `armed`, then `start` within 1 s, then
    `started`, with a `level` about every 100 ms, then `ended`.
  - A take ends on `stop`, at 600 s, or as a failure on window closure, UI
    disconnect, an xrun, a device error or daemon stop. A failure leaves no
    files.
  - `discard` deletes a committed take the reviewer rejected, while no lease
    is active.
  - `--fake-source` records the paced fake source.

Tests: `ctest --test-dir .build/host-dev -R media_` (the tool runs on the
paced fake source, and against a missing device). The Python fake below is
unchanged and still serves the simulator's `command_recognition`.

**Amended 2026-09-26:** the supervisor now verifies kernel-visible capture
closure before every enabled suspend write and can terminate only the exact
broker UID (`BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID`; see
`panel_supervisor/README.md`). This broker is still the fake below: it opens no
device, and its closure replies remain telemetry.

**Amended 2026-09-25:** host integration implements only a deterministic fake
`command_recognition` broker. It opens no ALSA or other audio device and has no
network/provider capability. `wish_recording` always returns
`MEDIA_BROKER.PURPOSE_DISABLED`. No recognizer, microphone, pronunciation,
policy, or acoustic accuracy has production qualification.

Run from any working directory:

```sh
python3 /absolute/repo/media_broker/fake_media_broker.py --root /absolute/repo
```

The simulator creates private stdin/stdout pipes. These are an explicitly
scripted fixture boundary, not the production exact-UID lifecycle/lease socket
or independent microphone-closure evidence. Each request and response is one
JSON line, at most 8192 input bytes. Unknown fields, duplicate keys, malformed
messages, and extra start payloads fail closed without echoing content.

`context` accepts exactly `display_mode`, `presence`, `surface`, `idle`,
`muted`, `consumer_connected`, `active_context_nonce`, `render_generation`,
`back_revealed`, and `history_available`. Only `day_active`, asserted presence,
stable study surface, unmuted and connected state admits the command purpose.
A valid context response supplies the exact C++ `VoiceCatalogBinding` field
names. `utterance` accepts a complete synthetic `phrase` and strictly increasing
`utterance_sequence`; it returns one canonical semantic `InteractionIntentEnvelope`
including the complete artifact/policy chain, or a redacted structured error.
The process never returns phrases, transcripts, scores, audio, or partial
recognition results. Simulator phrase injection is explicitly synthetic input;
the production audio transport is not implemented.

Every surface, render/context, lifecycle, mute, disconnect, fault, deadline,
and stop transition closes the active lease and overwrites the bounded mutable
test buffer before releasing it. Deadline checks run while stdin is idle.
Arming creates an immutable binding and opaque ID; `start` carries only that
ID on the same private process connection. Start authorization is consumed
once. Unknown, repeated, closed, expired, and mismatched starts have distinct
error codes. The future touch-gated wish purpose is disabled, so no caller can
substitute a UI-authored provenance object. Stop/closure replies are telemetry.
The broker cannot prove a physical microphone rail is off.

The deterministic recognizer matches whole normalized phrases from the one
generated active set. It applies replay rejection, cooldown/debounce, silence,
and lease deadlines; back-side rating forms are absent on the front. The
canonical study reducer independently enforces item/revision/render binding,
provisional observations, durable correction capacity, visible acknowledgement,
and touch Undo. Static fixture admission does not bypass those guards. Ratings
can affect an already materialized successor cycle before touch correction;
correction changes the next unmaterialized cycle.

Tests run with `python3 -m unittest discover -s media_broker/tests -p 'test_*.py'`.
They cover all closure gates, buffer erasure, one-shot leases, catalog/policy
identity tampering, exact collision rejection, complete phrases, all ratings,
replay/cooldown, subprocess redaction, bounded malformed input, and two-phase
effect admission. Synthetic byte payloads occur only inside broker test memory.
PIR, never speech, wakes Linux. Real capture enablement still needs distinct
service identities, direct authenticated supervisor lifecycle delivery,
independent kernel-visible capture closure, reviewed provenance claim IPC,
sandbox packaging, and acoustic/hardware evidence.
