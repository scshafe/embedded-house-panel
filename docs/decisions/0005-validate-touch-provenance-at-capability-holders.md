<!-- @canonical touch-provenance-trust-boundary -->

# ADR 0005: Validate Touch Provenance at Capability Holders

**Status:** accepted

**Date:** 2026-08-07

## Context

The spoken-command catalog correctly forbids voice from starting capture,
submitting a personal message, or controlling power. Catalog validation and a
second check inside `panel_ui` do not enforce that boundary when `panel_ui` is
the process asking a more privileged component to act. A compromised UI can
label its own request as touch-originated.

The UI-to-supervisor direction is already frozen as presentation
acknowledgements only, plus ordinary connect and disconnect. It may not acquire
feature requests, commands, leases, or capability negotiation. At the same
time, `panel_ui` must request a media lease directly from the media broker, and
the supervisor must close media windows on absence, night, and suspend
preparation.

This record defines the proposed trust boundary. It does not implement the
components, approve roadmap Step 1, approve a pending product decision, or make
a hardware claim.

## Decision

### Observe touch outside `panel_ui`

`panel_supervisor` will be the actual physical-touch observer. It will open one
explicitly configured, exactly identified touch evdev device and will not infer
a path, identity, calibration, gesture, or coordinate mapping. On-target
evidence must prove that the selected input path and forwarding design work
before either becomes production configuration.

The supervisor will forward normalized touch events to `panel_ui` on a
dedicated supervisor-to-UI observation stream. `panel_ui` remains the sole
owner of application hit testing, surface routing, and touch semantics, but it
will not open the touch device or author security-relevant touch provenance.
Normalization means validated protocol framing of the observed contact and raw
axis values; only the UI applies an explicitly configured coordinate
calibration and hit map.
The observation stream is separate from the lifecycle/presentation protocol,
and its UI-to-supervisor direction permits connect and disconnect only.
The production cutover must revoke the current `bhp-touch` raw-device group
membership and direct evdev access from the vocabulary/UI service; leaving both
the forwarded stream and raw node available would preserve an untracked peer
touch path. This record requires that later packaging change but does not make
it.

After a complete physical contact on the latest committed presentation, the
supervisor creates an unpredictable `touch_observation_id`. The observation is
bound to the supervisor boot/session, lifecycle `snapshot_id`, UI connection
challenge, committed `render_generation`, a monotonic observation time, and a
supervisor-measured `contact_duration_ms`, plus a monotonic expiry no later than
five seconds after contact completion. The supervisor may additionally attest
that the duration meets a holder-supplied qualifying-hold threshold bound into
the canonical operation; the UI cannot supply either duration or qualification.
The observation is
invalidated by expiry, a new lifecycle snapshot or committed render generation,
UI disconnect, absence, night, suspend preparation, or supervisor restart. Raw
coordinates do not enter the provenance token or logs.

Touch forwarding and provenance service are subordinate to lifecycle safety.
They use bounded non-blocking queues; a slow, disconnected, or failed consumer
is dropped and receives no provenance. Such failure cannot fault the supervisor,
delay blanking, extend a media window, or block suspend preparation.

### Bind one observation to one capability operation

`panel_ui` may include the opaque `touch_observation_id` when it asks a
capability holder to act. It cannot redeem that observation itself. A separate
supervisor provenance endpoint admits only the exact configured media-broker
and personal-context-client UIDs, verified from kernel peer credentials. Those
are two distinct role-specific service identities; a shared UID, shared group,
or membership-only check is insufficient. The later implementation plan must
allocate their concrete UIDs, socket paths, ownership, and modes before either
service is enabled. Each holder reciprocally requires the exact configured
supervisor UID and a root-owned, non-holder-writable socket path before trusting
an observation, claim response, or lifecycle publication.

After validating the semantic request, the capability holder canonicalizes the
complete operation and asks the supervisor to bind the observation to:

- the authenticated capability-holder audience;
- a digest of the canonical operation, including its required stable
  `intent_id`, capability endpoint, and endpoint-derived effect class;
- the supervisor/session, lifecycle snapshot, UI connection, and committed
  render generation already recorded with the observation; and
- the observer-measured contact duration, any required qualifying-hold
  threshold/result, and the original monotonic expiry.

The supervisor atomically consumes the observation and returns the canonical
touch-provenance envelope. An observation can authorize exactly one operation
across all capability holders. The UI has no permission to connect to this
claim endpoint. The supervisor treats the operation digest as opaque and does
not learn a birthday, recipient, draft, phrase, route, or feature workflow.
Observation consumption happens at successful claim. For a media lease, it is
separate from the broker's later one-shot consumption of start authorization.

Missing, expired, already consumed, wrong-audience, stale-presentation, or
operation-digest-mismatched evidence returns a structured rejection. No caller
may substitute a recent touch, retry with a new operation, or downgrade the
failure to voice or unknown provenance.

### Enforce at the narrow effect boundaries

The media broker owns media leases. `panel_ui` sends lease requests directly to
the broker. The broker validates the requested purpose and lifecycle window,
claims the referenced touch observation, and opens a `wish_recording` lease
only when the returned envelope is bound to that exact lease request and the
touch-only `agenda.start_wish_recording` intent.

After that successful claim consumes the observation, the broker creates an
in-memory `armed` lease with a fresh unpredictable `lease_id`. Its immutable
binding contains the exact authenticated UI connection/peer, UI request ID,
`agenda.start_wish_recording`, `wish_recording` purpose, canonical operation
digest, supervisor touch-provenance envelope, lifecycle session/snapshot and
sequence, `start_expires_at`, and purpose/lifecycle `capture_expires_at`. The UI
receives only the opaque `lease_id`. It never receives or echoes the provenance
envelope, lifecycle fields, operation digest, deadlines, or any other stored
binding; no field can be rebound or renewed.

The lease state machine is one-shot:

- The first `capture_start` carries only the exact `lease_id` over the same
  still-authenticated UI connection/peer that opened the lease. The broker
  resolves that ID to its stored immutable binding, verifies the stored peer,
  provenance, operation, purpose, lifecycle, and deadlines against current
  broker state, then atomically consumes start authorization and transitions
  `armed -> active` before the microphone opens.
- A repeated start while `active` returns a structured
  `MEDIA_BROKER.LEASE_START_ALREADY_CONSUMED` rejection. A start against
  `closed`, an unknown ID, an expired start deadline, or any binding mismatch
  returns its distinct structured rejection and never opens the microphone.
- Explicit stop transitions `active -> closed`. A stop outside `active` returns
  a structured no-active-capture outcome and cannot resurrect the lease.
- Armed-start expiry, active-capture expiry, lifecycle closure, UI disconnect,
  mute, or broker restart transitions `armed|active -> closed`. `closed` is
  terminal.

A voice intent, UI-authored `touch` flag, lifecycle state alone, or catalog
admission result can never satisfy either consumption. Observation consumption
proves that one operation was claimed; lease-start consumption proves that the
one bound capture actually began.

The personal-context client validates the prepared immutable dispatch revision,
claims the referenced touch observation, and creates a short-lived single-use
confirmation token containing the supervisor-authored `touch_provenance`
envelope bound to the touch-only `agenda.submit_wish` intent. Hold-to-submit
also requires observer-measured contact duration to
meet the reviewed minimum bound into that dispatch operation. It refuses
dispatch unless the token, operation digest, draft,
recipient route, sender/transport revision, expiry, and current birthday
occurrence all match. Its existing atomic dispatch barrier consumes the token
before external handoff.

Once this client has successfully claimed the observation and created the
confirmation token, a later lifecycle change does not retroactively revoke that
token. The client has no lifecycle subscription. The token remains governed by
its exact prepared-revision and operation binding, supervisor-authored touch
provenance and qualifying hold, monotonic expiry, client-process lifetime, and
atomic dispatch consumption. Client restart invalidates every unconsumed token.

The supervisor publishes lifecycle state directly to the broker. The broker
uses that one-way input to arm or close capture windows; `panel_ui` cannot keep
a window open. The UI-to-supervisor lifecycle socket remains
acknowledgement-only and never carries a media request, lease, feature message,
or provenance claim.

Lifecycle, touch-observation, and provenance messages carry an explicit
protocol version, unpredictable supervisor boot/session identity, and strictly
increasing sequence within that session. For the broker, a duplicate, gap that
prevents reconciliation, stale session, supervisor disconnect/restart, or
peer-identity mismatch closes the lifecycle window and every `armed` or
`active` lease. Supervisor disconnect/restart also invalidates unclaimed touch
observations and in-flight claim exchanges for either holder. It does not
invalidate a confirmation token that the personal-context client already
created. Reconnection begins media-closed from a complete current lifecycle
snapshot; no receiver replays an earlier open state.

### Verify the reachable effect, not the catalog declaration

The build acceptance gate is deterministic and has two phases:

1. **Phase A — source-independent endpoint effects.** For every registered
   `intent_id`, regardless of whether touch, voice, or a fixture can originate
   it, traverse the registered handler graph to all reachable capability
   endpoints. Record every endpoint-to-effect pair and its deduplicated effect
   set from endpoint-owned classes, never from catalog or UI labels. Phase A
   does not rank separate effect categories. The study-effect dominance order
   remains only where the canonical admission matrix defines it. An unresolved
   dynamic edge reaches every compatible registered target and contributes
   every corresponding endpoint-to-effect pair. An unenumerable edge fails
   Phase A.
2. **Phase B — voice projection admission.** Derive the voice-emittable ID set
   from each validated active spoken-form projection. For every projected ID,
   apply the unchanged voice-admission rule to every reachable effect in its
   Phase-A set under that projection's context and safeguards. Any one
   forbidden effect fails the projection. A projected ID missing from Phase A
   fails. `agenda.start_wish_recording`, `agenda.submit_wish`, and every other
   touch-only ID must be absent from every voice projection, but still receive
   a Phase-A result.

A voice projection reaching `capture_start`, external submission, or
power/illumination therefore fails even when the catalog, active command set,
or `panel_ui` labels the intent as a lower effect. Capability-holder runtime
validation remains mandatory; the two-phase static gate does not replace it.

## Required structured outcomes

- The supervisor distinguishes missing, expired, consumed, wrong-audience,
  stale-presentation, peer-identity, session/sequence, and operation-binding
  failures.
- The media broker returns a machine-readable provenance rejection without
  opening a lease or microphone device.
- The media broker accepts only `lease_id` in a `capture_start` payload and
  distinguishes unknown, connection/peer-mismatched, stored-binding-invalid,
  start-expired, already-started, closed, and no-active-capture lease outcomes.
  It exposes only the legal `armed -> active -> closed` path.
- The personal-context client returns a machine-readable provenance rejection
  before signing or beginning dispatch.
- A hold-duration failure is distinct from missing provenance; no UI-authored
  duration or `qualifying_hold` flag is accepted.
- The call-graph gate reports every Phase-A `intent_id`, reachable
  endpoint-to-effect pair, and deduplicated effect set, then every Phase-B
  projection/ID/effect admission result and violated rule.
- Logs contain opaque observation and request correlation identifiers only;
  they exclude raw coordinates, personal content, recipient data, audio, and
  token material.

## Consequences

- Compromising `panel_ui` no longer lets it invent the physical-touch evidence
  required for capture start or message dispatch.
- One touch cannot authorize both a media lease and a later message submission.
- The supervisor gains a narrow observation and attestation responsibility but
  no feature semantics, media ownership, message authority, or new UI request
  path.
- A compromised UI can still misrepresent pixels or consume a genuine touch on
  the bound frame for a different visible affordance. Eliminating that risk
  requires a trusted renderer or independently verified hit map and is outside
  this decision.
- Root compromise, a false kernel input event, or physical input injection
  below evdev remains outside this prototype boundary.
- Exact touch contact ABI capabilities, event forwarding under concurrent
  consumers, device identity, and hold timing remain hardware-unproven. The
  services stay disabled until on-target procedures capture that evidence.

## References

- [ADR 0004: Freeze Review-Gate Integration Contracts](0004-freeze-review-gate-integration-contracts.md)
- [Review amendment plan](../../.claude/plans/2026-08-06-review-amendments.md)
- [Panel mode protocol](../interfaces/panel-mode-protocol.md)
- [Application capability ownership](../architecture/application-capability-ownership.md)
- [Spoken command distinctiveness](../architecture/spoken-command-distinctiveness.md)
- [Personal agenda and birthday wishes](../architecture/personal-agenda-and-birthday-wishes.md)
- [Security and trust map](../security/threat-model.md)
