# Touch Observation Protocol

Owns the process-neutral normalized touch-observation record, its strict
validation, and the bounded non-blocking handoff between the supervisor-owned
raw-touch observer and an unprivileged consumer. It does not open an input
device, interpret coordinates, apply calibration, perform hit testing, issue
provenance, redeem an observation, or decide display and power policy.

`BoundedNormalizedTouchObservationStream` is the
`@canonical normalized_touch_observation_stream`. The supervisor side is the
only producer and owns every lifecycle transition. One UI-side consumer may
call `try_receive`. Both paths use `try_lock`; producer publication and
invalidation never wait for the consumer. Queue capacity is required explicit
configuration and has no production default. Overflow or producer-side lock
contention invalidates the stream with a stable structured error instead of
dropping one event and allowing a plausible partial contact sequence.

Every observation carries the touch protocol version and the complete committed
presentation binding: lifecycle protocol version, supervisor session,
lifecycle sequence and snapshot digest, UI connection challenge, and committed
render generation. Coordinates remain signed raw-axis evidence. No scale,
orientation, range, calibration, resolution, or gesture meaning is inferred.
The duration field is supervisor-to-UI evidence only. The provenance authority
derives it from its own retained contact-start time and a second injected-clock
read; neither this stream's consumer nor a provenance claimant can send a
duration back into that authority.

The validator accepts only explicit ASCII grammars shared with the lifecycle
protocol and touch authority. Error context contains field, operation, reason,
or bound names only. It never contains a session, challenge, snapshot,
observation ID, raw coordinate, or other supplied value.

## Public interface

- `validate_lifecycle_presentation_binding` validates one complete protocol-v2
  committed-render binding.
- `validate_normalized_touch_observation` validates one protocol-v1 raw-axis
  observation without adding hardware assumptions.
- `activate_presentation` installs a committed binding after an explicit
  lifecycle invalidation.
- `try_publish` either enqueues the complete observation immediately or returns
  a typed failure and leaves panel policy free to continue.
- `try_receive` returns the next FIFO observation, an explicit empty result, or
  a typed lifecycle/stream failure.
- `invalidate` synchronously closes delivery and attempts to clear retained
  records without waiting for the consumer. Its structured result distinguishes
  a complete physical clear from a contended clear; either result leaves stale
  records inaccessible.

The component depends only on the canonical `panel_mode_protocol` types for the
lifecycle version and safe-integer bound.
