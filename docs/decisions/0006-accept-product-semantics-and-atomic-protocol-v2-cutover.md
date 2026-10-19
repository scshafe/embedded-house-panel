<!-- @canonical product-semantics-and-protocol-v2-cutover -->

# ADR 0006: Accept Product Semantics and Atomic Protocol v2 Cutover

**Status:** accepted

**Date:** 2026-08-16

## Context

Roadmap Step 1 requires human decisions on the Lexinet study semantics, the
shared spoken-command capability, the personal-agenda product and safety
semantics, shared capability ownership, and the lifecycle protocol migration.
ADR 0004 froze several prerequisite integration contracts, but explicitly did
not approve Roadmap Step 1 or any `LEXI-*`, `VOICE-*`, or `AGENDA-*` decision.

The implementation plans already assign the first neutral application shell,
shared HTTP runtime, shared media broker, and Buildroot application migration
to the Lexinet work stream because it reaches those capabilities first. That
delivery order must not turn panel-wide capabilities into vocabulary-specific
ones or permit later agenda work to create peer implementations.

The current lifecycle protocol version 1 is a publisher-only scaffold with no
consumer or acknowledgement path. The planned version 2 adds the neutral
`panel_ui` consumer and fail-dark presentation acknowledgement. A migration
choice is still necessary because mixed supervisor/UI versions must never
produce mixed-version brightness behavior.

## Decision

### Accept the reviewed product semantics

The following product decisions are accepted exactly as recorded in their
canonical architecture documents:

| Decision set | Disposition |
|---|---|
| `LEXI-001` through `LEXI-007` | Accepted |
| `VOICE-001` | Accepted |
| `AGENDA-001` through `AGENDA-010` | Accepted |
| `AGENDA-011` | Pending and deliberately deferred to Roadmap Step 6 after feasibility evidence and its ADR |

Acceptance of `AGENDA-005` makes `Lexi, finish recording` the preferred stop
phrase. `Lexi, end birthday wish` is not an active alias; admitting it still
requires the recorded context-separated exception and measured acoustic
evidence. Acceptance of an evidence-gated decision preserves its stated gate:
it does not manufacture microphone, playback, Messages, scheduler, Tailnet, or
hardware evidence.

### Freeze shared capability ownership

The application capability registry is accepted. In particular:

- the Lexinet plan is the first delivery authority for the neutral `panel_ui`
  shell, canonical `shared/http_runtime/`, shared `media_broker/`, and the
  Buildroot migration to the neutral shell/broker service layout;
- vocabulary remains a subordinate feature beneath the neutral shell;
- a later agenda production plan must extend those exact capabilities and
  packaging boundaries rather than create agenda-owned peers; and
- first-delivery authority is sequencing, not a transfer of lasting runtime
  authority to the vocabulary feature.

Every capability retains the implementation and evidence status recorded in
the registry. Accepting an owner does not claim that an absent component exists
or that a planned control has been tested.

### Select an atomic protocol-v2 cutover

Lifecycle protocol version 2 replaces version 1 atomically as one
content-identified appliance release. The replacement supervisor publisher,
new `panel_ui` consumer, and their Buildroot service/configuration changes are
enabled and rolled back together. Enabled components speak version 2 only;
there is no version-1 compatibility adapter or runtime negotiation window.

Source changes may land while the affected target services remain disabled,
but no deployable release may enable only one side. The release-identity and
fail-dark handshake frozen by ADR 0004 enforce the boundary. ADR 0007
supersedes that record's reduced validation set and requires the complete
immutable commitment to be checked on every acknowledgement. Missing,
unknown-version, mismatched, failed, disconnected, stale, or timed-out
acknowledgement cannot permit nonzero brightness.

### Keep the Lexinet checkout an explicit input

`BHP_LEXINET_SOURCE_DIR` is the sole host-side input that selects the external
Lexinet source checkout for cross-repository work. Scripts and tests must not
infer a sibling checkout or encode a developer-specific absolute path. Runtime
Lexinet endpoint and credential configuration remain separate from this
development-source input.

**Amended 2026-10-18 (issue #20):** when `BHP_LEXINET_SOURCE_DIR` is unset, the
host tools fetch the exact commit pinned in the panel's contract provenance from
`https://github.com/scshafe/lexinet` into an ignored cache. That is a fixed,
content-checked pin, not an inferred checkout, so the rule above still holds.

### Preserve later review and evidence gates

This record accepts decisions and ownership only. It neither implements nor
claims completion of protocol version 2, `study.v1`, a voice engine, agenda
providers, recording, transcription, Messages submission, Buildroot service
migration, or any hardware configuration.

The personal-agenda feasibility plan's Step 1 remains incomplete. Its
experiment contract, test-recipient choice, maximum external side effects,
redaction rules, and stop points still require their own recorded human review.
No live Messages test, recipient, or external side effect is authorized by this
ADR. `DraftOnlyTransport` remains the default, and execution must stop again at
every approval boundary recorded by that plan.

## Consequences

- Implementers may rely on the accepted `LEXI-*`, `VOICE-001`, and
  `AGENDA-001` through `AGENDA-010` semantics without reopening Roadmap Step 1.
- `AGENDA-011` remains the single unresolved household civil-time decision and
  cannot be inferred from this record.
- There is one neutral shell, HTTP runtime, media broker, and Buildroot
  migration path for later features to extend.
- Protocol-v2 deployment is simpler and fail-dark, at the cost of forbidding
  independent mixed-version supervisor/UI rollout.
- Cross-repository verification remains portable because the Lexinet checkout
  is explicit configuration rather than repository-layout convention.
- Every implementation, feasibility, privacy, and on-target hardware gate
  remains in force.

## References

- [End-to-end development roadmap](../../.claude/plans/2026-08-05-end-to-end-development-roadmap.md)
- [Lexinet study integration](../architecture/lexinet-study-integration.md)
- [Spoken command distinctiveness](../architecture/spoken-command-distinctiveness.md)
- [Personal agenda and birthday wishes](../architecture/personal-agenda-and-birthday-wishes.md)
- [Application capability ownership](../architecture/application-capability-ownership.md)
- [Panel mode protocol](../interfaces/panel-mode-protocol.md)
- [Review-gate integration contracts](0004-freeze-review-gate-integration-contracts.md)
- [Complete protocol-v2 commitment validation](0007-validate-complete-protocol-v2-commitments.md)
