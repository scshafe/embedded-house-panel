# Panel Mode Protocol

**Status:** ~~version 1 scaffold; supervisor-to-UI delivery is implemented, but
a render-ready acknowledgement is not~~ version 1 publisher scaffold; the
supervisor publishes snapshots, but no repository process consumes them and no
render-ready acknowledgement exists

**Transport:** local Unix-domain stream socket, newline-delimited UTF-8 JSON

**Current authority:** `panel_supervisor` publishes version 1;
~~`vocabulary_panel` consumes it in the scaffold.~~ Version 1 is published but
unconsumed. `vocabulary_panel` implements no Unix-socket protocol client.

**Version 2 target:** `panel_supervisor` publishes lifecycle state and accepts
presentation acknowledgements; neutral `panel_ui` consumes and acknowledges.
Neither message direction grants the UI power authority.

## Version 2 ~~migration~~ greenfield replacement decision pending human review

Version 1 has no consumer or reverse message direction to migrate. Version 2 is
therefore a greenfield replacement of the publisher-only scaffold, not an
upgrade negotiated with an existing UI client.

The proposed roadmap Step 1 decision is an atomic appliance-release cutover,
not a runtime compatibility window:

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
to preserve or upgrade. Each enabled release must use all of the following
fields:

| Field | Source | Binding rule |
|---|---|---|
| `appliance_release_id` | Immutable build manifest | Supervisor and UI must report the same content-identified appliance release |
| `supervisor_session_id` | Supervisor CSPRNG at every process start | At least 128 unpredictable bits; never persisted or reused |
| `connection_challenge` | Supervisor CSPRNG for every accepted UI connection | At least 128 unpredictable bits; invalidated on disconnect or replacement |
| `event_sequence` | Supervisor counter within that session | Strictly increasing; meaningful only with the session ID |
| `snapshot_id` | Supervisor digest of the complete requested lifecycle snapshot and the preceding identity fields | Prevents acknowledging different content with the same mode and sequence |
| `display_mode` | Requested snapshot | Acknowledgement must echo the exact requested mode |
| `render_generation` | UI monotonic counter within the challenged connection | Must advance for the committed frame being acknowledged |
| `presentation_result` | UI after the strongest available page-flip/commit observation | Only the explicit `committed` result can satisfy readiness |

The supervisor accepts an acknowledgement exactly once for its one latest
pending snapshot, only on the connection to which it issued the challenge, and
only when every echoed binding matches. It checks Linux Unix-socket peer
credentials (for example `SO_PEERCRED`) against the dedicated configured
`panel_ui` service UID; membership in a shared group is not sufficient sender
identity. A new connection, process restart, release mismatch, duplicate,
timeout, or malformed value invalidates the pending readiness claim and keeps
the backlight at zero.

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
independent deployment lifecycles. This greenfield-replacement choice remains
proposed until the roadmap Step 1 human-review gate passes.

## Snapshot/event envelope

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

Allowed `display_mode` values are `vocabulary`, `night_red`, `suspended`, and `diagnostic`. Unknown values or protocol versions are hard errors; the UI must blank rather than guess.

## Delivery behavior

- A newly connected UI receives the current snapshot before incremental events.
- In version 1, `event_sequence` increases monotonically for the supervisor
  process lifetime. Version 2 additionally binds it to the unpredictable
  `supervisor_session_id` and connection challenge defined above.
- At most one UI client is retained. Extra or slow clients are isolated and closed; a reconnect receives the current snapshot rather than replaying stale events.
- With no UI connected, publication retains the snapshot and returns success;
  it is not a disconnection signal or proof of presentation.
- The socket directory is root-owned, setgid to the UI group, and not writable
  by the UI; the socket itself is group-connectable mode `0660`. Version 2 also
  validates the exact dedicated UI service UID from Unix peer credentials.
- No command message from the UI changes power state. Future acknowledgements use a distinct message type and do not grant authority.

## Render-readiness limitation

In version 1, a successful notifier call means only that a complete snapshot
was retained and, when a client exists, offered to the local socket transport.
It does **not** establish that the
UI parsed the snapshot, selected the requested view, drew a black/red/day
frame, or completed a DRM/fbdev page flip. The supervisor may notify before it
raises a nonzero backlight value, but that ordering alone cannot guarantee that
an old or bright frame will never be exposed.

Before the backlight/resume path is called product-safe, implement the complete
version 2 binding above. The supervisor must reject stale or mismatched
acknowledgements, retain brightness zero on timeout or disconnect, and must not
let an acknowledgement grant suspend or backlight authority. On-target tests
must correlate the acknowledgement with a completed page flip (or the strongest
equivalent the selected framebuffer path provides). Until then, the target
services remain disabled by default and the no-white-flash behavior is an
experiment requirement, not a proven invariant.

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
