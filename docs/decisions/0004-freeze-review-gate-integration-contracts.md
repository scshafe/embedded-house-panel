<!-- @canonical review-gate-integration-decisions -->

# ADR 0004: Freeze Review-Gate Integration Contracts

**Status:** accepted

**Date:** 2026-08-07

## Context

The 2026-08-06 adversarial architecture review identified six choices that
must be settled before its amendments can be implemented without guessing.
They span presentation failure handling, the UI-to-supervisor trust boundary,
release identity, protocol-v2 validation, Lexinet event-submission cardinality,
and presence-grace behavior.

This record captures those choices together because they are the shared input
to amendment-plan Steps 5, 7, 8, and 9 and to a later Lexinet plan amendment.
It does not approve any pending `LEXI-*`, `VOICE-*`, or `AGENDA-*` product
decision, and it does not approve roadmap Step 1. The trust-boundary ADR
required by amendment-plan Step 8 remains a separate record.

## Decision

### Presentation outcomes are degraded and non-fatal

A presentation acknowledgement that is timed out, disconnected, stale,
duplicate, mismatched, or explicitly failed is a normal degraded outcome, not
a cycle fault. Only an exact `committed` acknowledgement may gate nonzero
brightness. For every other presentation outcome, brightness stays zero, the
cycle completes, and the suspend request is still made.

The supervisor emits a structured event for every degraded presentation
outcome so persistent failures remain visible in logs and diagnostics, then
continues running. Daemon-ending faults remain reserved for internal invariant
failures: a backlight write failure, structured-log write failure, or rejected
`/sys/power/state` write. Those faults retain blank-before-exit behavior, with
bounded restart supervision as the recovery mechanism.

### UI-to-supervisor traffic is acknowledgement-only

The UI-to-supervisor direction carries presentation acknowledgements only,
including negative `presentation_result` values, plus ordinary socket connect
and disconnect. It never carries requests, commands, leases, or feature
messages.

Consequently, `panel_ui` requests capture leases directly from the media
broker. The supervisor publishes lifecycle state to the broker in the
supervisor-to-broker direction so absence, night, and suspend preparation arm
or close capture windows. The Step 8 trust-boundary design must deliver touch
provenance to the broker without trusting `panel_ui` and without weakening this
acknowledgement-only invariant.

### `appliance_release_id` identifies the application bundle

`appliance_release_id` is one build-stamped application-bundle identity:
repository `VERSION` plus Git commit, with a dirty-tree marker when applicable.
The top-level CMake configure/build step produces the identity and compiles the
same value into `panel_supervisor` and `panel_ui`. The Buildroot image build
inherits that value, so application-bundle and image identity coincide in a
production image.

The peers compare `appliance_release_id` once during the connection handshake.
A mismatch refuses the connection and leaves the display dark. The value also
remains an input to the `snapshot_id` digest. This makes the atomic-cutover rule
self-enforcing even when per-binary deployment tooling could otherwise install
only one side of the protocol migration.

### Protocol v2 uses the reduced validation set

The independently validated bindings are `connection_challenge`, `snapshot_id`,
`display_mode`, `render_generation`, and `presentation_result`, plus an
exact-UID `SO_PEERCRED` check. `supervisor_session_id` and `event_sequence` are
inputs to the `snapshot_id` digest and are echoed for diagnostics only rather
than validated as independent mismatch classes. `appliance_release_id` is
checked once at the handshake as described above.

Session and sequence therefore remain bound through the digest while the
validator and its test matrix carry fewer redundant mismatch classes.

### The panel is the only routine Lexinet study-event submitter

In production, the panel device principal identified by its Tailnet tag is the
sole routine submitter of study events. The bearer-authenticated operator CLI
retains loopback access and may submit corrective or backfill events as a rare
manual administrative action against the same learner profile. It is an
operator tool, not a second study client.

No phone, web, or second-panel study client is planned. Introducing one reopens
this decision. A later Lexinet plan amendment may use the resulting
single-concurrent-writer-per-profile model when separately evaluating the
review's simplification candidates; this record does not adopt those
candidates.

### Presence grace is dim for 120 seconds

During presence grace, the panel retains its active day or night mode at the
calibrated grace brightness for 120 seconds, then blanks and follows the
suspend path. The duration remains configurable. PIR deassertion does not prove
departure, so the dim state gives a still-present occupant a visible cue and a
chance to reassert presence instead of making the interface disappear.

Illustrative values of 40 percent by day and 5 percent by night are examples,
not production defaults; final brightness values require on-target calibration.
The duration and behavior are revisited against measured H3/H6 battery evidence.
`docs/behavior/panel-policy.md` is the single source of truth. The `dark_idle`
row in the power-domain architecture describes a future MCU-era power state and
must defer to panel policy for current grace behavior.

## Consequences

- Presentation transport failures remain visible without preventing blanking,
  cycle completion, or suspend.
- The supervisor socket remains a narrow lifecycle-output and presentation-ack
  boundary; feature capabilities do not accumulate there.
- Partial application deployment fails dark at handshake rather than creating
  a mixed-release protocol session.
- Protocol-v2 validation binds the same security-relevant state with fewer
  independently checked fields.
- Current Lexinet planning may assume one routine study-event writer per learner
  profile while preserving a separately authenticated administrative path.
- Grace brightness remains hardware-evidence-gated even though grace behavior
  and duration are now decided.

## References

- [Review amendment plan](../../.claude/plans/2026-08-06-review-amendments.md)
- [End-to-end development roadmap](../../.claude/plans/2026-08-05-end-to-end-development-roadmap.md)
- [Panel mode protocol](../interfaces/panel-mode-protocol.md)
- [Panel policy](../behavior/panel-policy.md)
- [Application capability ownership](../architecture/application-capability-ownership.md)
- [Lexinet study integration](../architecture/lexinet-study-integration.md)
