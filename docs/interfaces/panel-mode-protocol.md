# Panel Mode Protocol

**Status:** protocol v2 host peers and defensive power-owner validation are
implemented and host-tested; target services remain disabled because no
physical display-commit backend or on-target evidence exists

**Transport:** local Unix-domain stream socket, newline-delimited UTF-8 JSON

**Current authority:** `panel_supervisor` publishes and validates protocol v2;
neutral `panel_ui` authenticates the supervisor and consumes v2. The current UI
entry point deliberately sends no committed acknowledgement because it has no
physical display-commit backend. `vocabulary_panel` is not a protocol peer.

**Version 2 boundary:** `panel_supervisor` publishes lifecycle state and accepts
only validated presentation acknowledgements; neutral `panel_ui` consumes the
snapshot and may acknowledge only an explicit display-commit result. Neither
message direction grants the UI power authority.

## Version 2 ~~migration~~ greenfield replacement: ~~field set pending human review~~ frozen Step 7 field set

Version 1 has no consumer or reverse message direction to migrate. Version 2 is
therefore a greenfield replacement of the publisher-only scaffold, not an
upgrade negotiated with an existing UI client.

ADR 0006 accepts an atomic appliance-release cutover, not a runtime
compatibility window:

- supervisor, `panel_ui`, and their Buildroot service/configuration changes
  deploy and roll back as one content-identified appliance release;
- ~~enabled runtime components accept version 2 only; no adapter translates a
  version 1 transport write into a presentation acknowledgement;~~ the
  replacement supervisor publisher and new `panel_ui` consumer speak version 2
  only, with no compatibility adapter for unconsumed version 1 snapshots;
- any absent, stale, unknown-version, mismatched, failed, disconnected, or
  timed-out acknowledgement leaves requested nonzero brightness unapplied;
- source changes may land incrementally while target services remain disabled,
  but no release enables only one side of the cutover; and
- rollback restores the complete prior release rather than mixing supervisor
  and UI artifacts from different protocol generations.

Version 2 makes presentation readiness a request-bound exchange. ~~It upgrades
a bare version 1 sequence acknowledgement.~~ Version 1 has no acknowledgement
to preserve or upgrade. ~~Each enabled release must independently validate all
of the following fields.~~ The accepted review-gate decisions freeze the fields
and validation roles below. ADR 0007 supersedes ADR 0004's reduced validation
set and freezes the complete per-acknowledgement commitment.

| ~~Field~~ | ~~Source~~ | ~~Binding rule~~ |
|---|---|---|
| ~~`appliance_release_id`~~ | ~~Immutable build manifest~~ | ~~Supervisor and UI must report the same content-identified appliance release~~ |
| ~~`supervisor_session_id`~~ | ~~Supervisor CSPRNG at every process start~~ | ~~At least 128 unpredictable bits; never persisted or reused~~ |
| ~~`connection_challenge`~~ | ~~Supervisor CSPRNG for every accepted UI connection~~ | ~~At least 128 unpredictable bits; invalidated on disconnect or replacement~~ |
| ~~`event_sequence`~~ | ~~Supervisor counter within that session~~ | ~~Strictly increasing; meaningful only with the session ID~~ |
| ~~`snapshot_id`~~ | ~~Supervisor digest of the complete requested lifecycle snapshot and the preceding identity fields~~ | ~~Prevents acknowledging different content with the same mode and sequence~~ |
| ~~`display_mode`~~ | ~~Requested snapshot~~ | ~~Acknowledgement must echo the exact requested mode~~ |
| ~~`render_generation`~~ | ~~UI monotonic counter within the challenged connection~~ | ~~Must advance for the committed frame being acknowledged~~ |
| ~~`presentation_result`~~ | ~~UI after the strongest available page-flip/commit observation~~ | ~~Only the explicit `committed` result can satisfy readiness~~ |

| Field or credential | Producer | Version 2 role | Supervisor validation |
|---|---|---|---|
| `SO_PEERCRED.uid` | Linux kernel for the accepted Unix-socket peer | Connection identity | Independently require exact equality with the configured dedicated `panel_ui` service UID; shared-group membership is insufficient |
| `appliance_release_id` | Top-level CMake derives one application-bundle identity from repository `VERSION` and Git commit; a dirty tree additionally carries the full Git-visible source-content SHA-256, so divergent dirty builds do not share a generic marker. It compiles the same value into `panel_supervisor` and `panel_ui`, and the Buildroot build inherits that value | Connection-handshake binding and `snapshot_id` digest input | Require exact equality in every acknowledgement; mismatch closes the connection and leaves the display dark |
| `supervisor_session_id` | Supervisor CSPRNG at every process start | `snapshot_id` digest input and acknowledgement binding | Independently require the exact current session; at least 128 unpredictable bits, never persisted or reused |
| `connection_challenge` | Supervisor CSPRNG for every accepted UI connection | Per-connection acknowledgement binding and `snapshot_id` digest input | Independently require the exact challenge issued on that connection; invalidate it on disconnect or replacement |
| `event_sequence` | Supervisor counter within `supervisor_session_id` | `snapshot_id` digest input and acknowledgement binding | Independently require the exact latest pending sequence and emit strictly increasing values meaningful only within the session |
| `snapshot_id` | Supervisor digest of the complete requested lifecycle snapshot plus `appliance_release_id`, `supervisor_session_id`, `connection_challenge`, and `event_sequence` | Content and identity binding | Independently require the exact identifier of the one latest pending snapshot |
| `display_mode` | Supervisor requested lifecycle snapshot | Explicit lifecycle binding | Independently require an exact echo of the latest requested mode |
| `render_generation` | UI monotonic counter within the challenged connection | Presented-frame binding | Independently require the generation for the frame attempted for the latest pending snapshot; only a newer committed generation may satisfy readiness |
| `presentation_result` | UI after its strongest available page-flip or commit observation | Positive or negative presentation acknowledgement | Independently require exactly `committed` or `failed`; only `committed` satisfies readiness, while `failed` completes as degraded and keeps brightness zero |

The first snapshot/presentation-acknowledgement exchange establishes the release
handshake: the supervisor publishes its built-in `appliance_release_id`, and the
UI reports its built-in value in the presentation acknowledgement. The UI sends
no separate hello, request, or negotiation message. Every later acknowledgement
must repeat the same exact release, session, challenge, sequence, snapshot, and
mode binding; the first success never waives a later field comparison.

The canonical Unix adapter is part of the privileged power-path TCB. `publish`
freezes the exact expected tuple and prior render-generation floor before the
power owner calls `await_presentation`; the await operation receives that value
as immutable. A connection arriving after publication is retained for the next
snapshot and cannot rewrite or satisfy the current expectation.

The `snapshot_id` digest binds the semantic field values above. The concrete
canonical byte encoding and digest algorithm are frozen below so two conforming
peers cannot produce different identifiers for the same snapshot.

### Canonical version 2 snapshot digest

JSON text is never hashed. Each peer first decodes and validates the complete
typed snapshot, then forms this exact byte sequence:

1. ASCII `bhp.panel_mode.snapshot.v2`, followed by one NUL byte;
2. protocol version as unsigned byte `0x02`;
3. `appliance_release_id` UTF-8 byte length as unsigned 16-bit big-endian,
   followed by its ASCII bytes;
4. `supervisor_session_id` decoded from exactly 32 lowercase hexadecimal
   characters into 16 raw bytes;
5. `connection_challenge` decoded the same way into 16 raw bytes;
6. `event_sequence` as unsigned 64-bit big-endian;
7. one unsigned byte for each enum in order: `display_mode` (`01` day active,
   `02` night red, `03` suspended, `04` diagnostic), `presence_state` (`00`
   unknown, `01` present, `02` grace period, `03` absent), `solar_state` (`00`
   unknown, `01` day, `02` night), and `system_state` (`00` starting, `01`
   active, `02` suspended, `03` recomputing after resume, `04` faulted);
8. `effective_brightness_percent` as one unsigned byte; and
9. `observed_at_unix_milliseconds` as signed two's-complement 64-bit
   big-endian.

`snapshot_id` is the literal prefix `sha256:` followed by 64 lowercase
hexadecimal characters encoding SHA-256 of that preimage. It is not included in
its own preimage. `render_generation` and `presentation_result` belong only to
the acknowledgement. JSON member order, whitespace, and diagnostic metadata
are not digest inputs. Version 2 metadata cannot influence rendering or
readiness; adding any presentation-affecting field requires protocol version 3
so the field can enter the typed preimage.

Conversion from a system-clock time point to wire milliseconds floors toward
negative infinity, including for sub-millisecond instants before the Unix
epoch. It never truncates a negative value toward zero.

The checked-in [schema](panel-mode-protocol-v2.schema.json) freezes the typed
JSON data model after lexical decoding; JSON Schema alone cannot distinguish an
integer token from `2.0`, detect duplicate names, or observe escape spelling.
The canonical strict codec, bounded incremental stream framer, and
[golden-vector harness](../testing/fixtures/panel-mode-protocol-v2-golden.json)
therefore jointly freeze the accepted wire language, complete snapshot and
acknowledgement frames, and two independently reproduced digest examples. Both
frames are flat JSON objects followed by one line feed. A complete frame is at
most 4,096 bytes;
carriage return and blank frames are forbidden. Release IDs are 1–128 ASCII
bytes matching `[A-Za-z0-9][A-Za-z0-9._+-]{0,127}`. Session and challenge IDs
are nonzero 16-byte values encoded as lowercase hexadecimal. Event sequence and
render generation are in `1..9007199254740991`; brightness is in `0..100`; and
observed time is from `1800-01-01T00:00:00Z` inclusive through
`2101-01-01T00:00:00Z` exclusive.

Decoders reject duplicate, unknown, or missing members, invalid UTF-8, string
escapes, floats, coercions, nulls, unknown enum values, unsafe integers, and
unknown protocol versions. No decoder supplies defaults or an alternate digest
path. Stable structured failures use the `PANEL_MODE_PROTOCOL.*` codes for
frame size/encoding, JSON, missing/duplicate/unknown/type/value fields,
unsupported version, digest computation, and snapshot mismatch. Error context
names only the field, bound, or operation; it never includes the raw frame,
release/session/challenge identifiers, or digest.

The supervisor accepts an acknowledgement exactly once for its one latest
pending snapshot, only on the connection to which it issued the challenge, and
only when every independently validated binding matches. The session and event
sequence remain digest inputs and are also exact mismatch classes at the
adapter and final power-owner boundaries. A new connection, process restart,
release mismatch, duplicate, timeout, or malformed value invalidates the
pending readiness claim and keeps the backlight at zero.

The UI-to-supervisor direction is acknowledgement-only. It carries positive or
negative presentation acknowledgements plus ordinary socket connect and
disconnect. It never carries requests, commands, leases, feature messages, or
negotiated capabilities. Adding any such traffic requires a superseding
architecture decision and a new protocol version; it cannot be added as another
version-2 message type
(`docs/decisions/0004-freeze-review-gate-integration-contracts.md`).

These bindings prevent stale or cross-connection replay. They do not let a
compromised process running as the authorized `panel_ui` UID prove that it
honestly committed the claimed pixels. Version 2 deliberately accepts that
limitation for the prototype and bounds it with process isolation, a single
display owner, fail-dark supervision, and on-target correlation against the
selected DRM/fbdev commit primitive. A stronger attestation requirement would
move page-flip observation into a more trusted display component and requires a
new architecture decision.

~~Version 1 cannot prove that a frame was committed, so a bounded compatibility
mode would either remain dark or weaken the safety property that motivates
version 2. Reconsider compatibility only if supervisor and UI acquire genuinely
independent deployment lifecycles.~~ Version 1 is unconsumed, so there is no
consumer compatibility window to preserve. Reconsider dual-version support
only if the replacement supervisor and `panel_ui` later acquire genuinely
independent deployment lifecycles. ADR 0006 accepts this greenfield replacement;
the host peers are implemented, while target service enablement remains gated
on a physical display-commit backend and on-target evidence.

## Legacy version 1 snapshot/event envelope

This historical publisher-only shape has no current consumer and is not
accepted by either version-2 peer. It remains documented solely to make the
atomic replacement boundary explicit.

```json
{
  "data": {
    "protocol_version": 1,
    "event_sequence": 42,
    "display_mode": "vocabulary",
    "presence_state": "present",
    "solar_state": "day",
    "system_state": "active",
    "effective_brightness": 100,
    "observed_at": "2026-08-05T19:00:00Z"
  },
  "error": null,
  "meta": {
    "degraded": false,
    "degraded_reason": null,
    "request_id": "local-42"
  }
}
```

~~Allowed `display_mode` values are `vocabulary`, `night_red`, `suspended`, and
`diagnostic`.~~ The version 1 publisher emits `vocabulary`, `night_red`,
`suspended`, or `diagnostic`. The complete version 2 lifecycle set is
`day_active`, `night_red`, `suspended`, and `diagnostic`, as required by roadmap
Step 2 (`.claude/plans/2026-08-05-end-to-end-development-roadmap.md`).
`day_active` is feature-neutral: `panel_ui`, not `panel_supervisor`, chooses the
daytime surface. Unknown values or protocol versions are hard errors; the UI
must blank rather than guess.

## Delivery behavior

- A newly connected UI receives the current snapshot before incremental events.
- In version 1, `event_sequence` increases monotonically for the supervisor
  process lifetime. Version 2 includes the session and sequence in
  `snapshot_id` and independently validates the exact session, sequence, and
  connection challenge on every acknowledgement.
- At most one UI client is retained. Extra or slow clients are isolated and closed; a reconnect receives the current snapshot rather than replaying stale events.
- With no UI connected, publication retains the snapshot and returns success;
  it is not a disconnection signal or proof of presentation.
- The socket directory is root-owned, setgid to the UI group, and not writable
  by the UI; the socket itself is group-connectable mode `0660`. Version 2 also
  validates the exact dedicated UI service UID from Unix peer credentials.
- ~~No command message from the UI changes power state. Future acknowledgements
  use a distinct message type and do not grant authority.~~ Version 2 accepts
  only presentation acknowledgements from the UI. An acknowledgement never
  grants power authority; no UI command, request, lease, feature message, or
  capability negotiation is legal on this channel.

## Render-readiness limitation

In version 1, a successful notifier call means only that a complete snapshot
was retained and, when a client exists, offered to the local socket transport.
It does **not** establish that the
UI parsed the snapshot, selected the requested view, drew a black/red/day
frame, or completed a DRM/fbdev page flip. The supervisor may notify before it
raises a nonzero backlight value, but that ordering alone cannot guarantee that
an old or bright frame will never be exposed.

The host implementation enforces the complete version 2 binding above: it
rejects stale or mismatched acknowledgements, retains brightness zero on timeout
or disconnect, and never lets an acknowledgement grant suspend or backlight
authority. Before the backlight/resume path is called product-safe, an
evidence-backed display backend and on-target tests must correlate the
acknowledgement with a completed page flip (or the strongest equivalent the
selected framebuffer path provides). Until then, the target services remain
disabled by default and the no-white-flash behavior is an experiment
requirement, not a proven invariant.

## Version 1 error direction

~~This section previously specified a version 1 UI-to-supervisor
unsupported-version response.~~ Version 1 has no UI-to-supervisor message
direction, so it defines no response envelope. Version 2 acknowledgement errors
belong to the replacement bidirectional contract and will not be inferred from
the version 1 publisher.

**Amended 2026-08-07:** corrected version 1 from a claimed supervisor/UI
integration to a published-but-unconsumed scaffold, recast version 2 as a
greenfield replacement that still cuts over atomically, and removed the invalid
UI-to-supervisor version-error envelope.

**Amended 2026-08-07:** ~~froze the reduced version-2 validation set from ADR
0004~~, defined the top-level-CMake application-bundle release identity and its
first-exchange handshake with per-acknowledgement revalidation, made the reverse
direction acknowledgement-only, and
enumerated the feature-neutral version-2 lifecycle modes. This amendment does
not approve roadmap Step 1 or any pending `LEXI-*`, `VOICE-*`, or `AGENDA-*`
decision.

**Amended 2026-08-16:** ADR 0006 accepted the atomic protocol-v2 appliance
cutover with no version-1 compatibility window. The host v2 codec, supervisor
server, neutral UI client, and final supervisor tuple validation are now
implemented and tested. Services remain disabled pending a physical
display-commit backend and target evidence; no display or brightness behavior is
claimed.

**Amended 2026-08-16:** ADR 0007 superseded ADR 0004's reduced validation set.
Every acknowledgement now carries and independently validates the complete
immutable release/session/challenge/sequence/snapshot/mode/result commitment at
the privileged adapter and final power-owner boundaries. This host contract
does not provide a physical display-commit backend or target evidence.
