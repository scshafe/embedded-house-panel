# Fake Media Broker

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
