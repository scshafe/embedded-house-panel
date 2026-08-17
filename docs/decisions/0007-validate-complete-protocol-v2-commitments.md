<!-- @canonical complete-protocol-v2-commitment-validation -->

# ADR 0007: Validate Complete Protocol v2 Commitments

**Status:** accepted

**Date:** 2026-08-16

## Context

ADR 0004 deliberately reduced the first protocol-v2 validation matrix: the
release identity was checked once, while session and event sequence were bound
only through the snapshot digest. The later master roadmap Step 2 and Lexinet
Step 7a require the exact latest committed tuple to gate nonzero brightness.

Implementation and adversarial review exposed a trust-boundary problem with the
reduced set. The Unix adapter must remain inside the privileged power-path
trusted computing base, but the final power owner still needs an immutable,
independently checked reference for the publication it is authorizing. A
handshake-only release check or diagnostic-only echoed field makes that final
boundary depend on mutable adapter history and creates unnecessary replay and
cross-publication ambiguity.

The extra comparisons are deterministic, local, and already present in both
typed peers. They add no network capability and do not weaken the rule that
SHA-256 is a content binding rather than authentication.

## Decision

Protocol version 2 validates the complete presentation commitment on every
acknowledgement. The supervisor independently requires exact equality for:

- the built-in `appliance_release_id`;
- the current unpredictable `supervisor_session_id`;
- the current per-connection `connection_challenge`;
- the one latest pending `event_sequence`;
- the canonical `snapshot_id`;
- the requested `display_mode`; and
- the explicit `presentation_result`, with only `committed` eligible for
  readiness.

The acknowledgement's `render_generation` must be a valid safe integer, newer
than the frozen publication floor, and strictly advance within the challenged
connection. The exact-UID `SO_PEERCRED` check remains mandatory before any
application frame is accepted.

`publish` freezes the expected tuple before control enters the acknowledgement
wait. The wait receives that expectation as immutable; a late connection is
retained for the next publication and cannot rewrite or satisfy the current
one. The final `panel_supervisor` power owner revalidates the immutable tuple
and generation continuity even though the canonical Unix adapter has already
validated the wire frame. Production constructs only that documented
privileged adapter.

Both peers revalidate the built-in release on every typed exchange. There is no
separate hello, negotiated capability, version-1 shim, alternate digest, or
fallback comparison. A mismatch closes the connection, emits only a
content-free structured error, and keeps brightness zero.

This decision supersedes only ADR 0004's sections
"`appliance_release_id` identifies the application bundle" and "Protocol v2
uses the reduced validation set." ADR 0004's presentation-outcome,
acknowledgement-only, Lexinet-writer, and presence-grace decisions remain
accepted. ADR 0006's atomic protocol-v2 cutover remains unchanged.

## Consequences

- The digest and independent comparisons intentionally overlap: the digest
  binds typed content, while exact field checks make the final authorization
  boundary explicit and auditable.
- Every acknowledgement test matrix includes well-formed mismatches for
  release, session, challenge, sequence, snapshot, mode, result, and stale
  render generation.
- A compromised exact-UID UI may still lie about its own physical page flip;
  the display-commit backend and on-target correlation evidence remain separate
  gates, so target services stay disabled.
- Changing the typed commitment or its canonical digest requires a new
  protocol version rather than weakening validation at runtime.

## References

- [ADR 0004: Freeze Review-Gate Integration Contracts](0004-freeze-review-gate-integration-contracts.md)
- [ADR 0006: Accept Product Semantics and Atomic Protocol v2 Cutover](0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md)
- [End-to-end development roadmap](../../.claude/plans/2026-08-05-end-to-end-development-roadmap.md)
- [Lexinet study integration plan](../../.claude/plans/2026-08-05-lexinet-study-integration.md)
- [Panel mode protocol](../interfaces/panel-mode-protocol.md)
- [Threat model](../security/threat-model.md)
