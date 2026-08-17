# Lexinet Consumer Contract Boundary

**Status:** transition document. ADR 0006 accepts the `study.v1` product
semantics; the current panel adapter is a scaffold and the successor API does
not yet exist in Lexinet.

The product semantics, event model, scheduler provenance, and executable work
are defined in the
[Lexinet study architecture](../architecture/lexinet-study-integration.md) and
its linked implementation plan. The canonical machine-readable API contract
will live in Lexinet. This repository will consume generated or
provenance-stamped fixtures rather than maintain a second schema.

## Planned `study.v1` boundary

The planned panel integration needs three idempotent capabilities:

```text
POST /api/v1/study/sessions
GET  /api/v1/study/sessions/{session_id}
POST /api/v1/study/sessions/{session_id}/sync
```

- Atomically create or resume the one nonterminal immutable finite coverage cycle
  for an explicitly configured learner/profile, normalized corpus/card kind,
  regardless of scheduler configuration; distinct concurrent creation UUIDs
  converge on it and a policy change applies only to its successor unless an
  audited migration terminates it.
- Return bounded complete card projections plus scheduler name/version,
  configuration digest, server-assigned per-profile ingestion high-water ordinal,
  seed, and per-item selection reason. The panel never reproduces server
  selection.
- Accept ordered, client-generated, idempotent `presented`, `revealed`, `rated`,
  and `rating_voided` events under a durable client-stream UUID and contiguous
  sequence. A separate terminal receipt ledger consumes accepted,
  semantic-duplicate, and stable rejected sequences without treating rejection
  as scheduler evidence. A rating uses exactly one of `again`, `hard`,
  `good`, or `easy`; a void names the exact rating UUID it corrects. A causal
  reveal/rating target may already be stored or precede its dependent event in
  the same ordered atomic batch.
- Return an authoritative cursor and byte-equivalent pages for an idempotent
  replay. Reuse of an idempotency key with changed normalized input is an
  explicit conflict.
- Before a successor cycle is materialized, require a predecessor sync/flush
  receipt and record an exact profile ingestion high-water at or after it. In
  one active-scope transaction, require a canonical `presented` fact for every
  predecessor item, mark that predecessor completed, and link exactly one
  successor; local prefetch exhaustion is insufficient, concurrent creation
  UUIDs converge, and replays return the linked successor. The panel must have
  no earlier locally pending predecessor event at that barrier.
- Represent an unchanged empty corpus with one `waiting_for_corpus` sentinel;
  repeated create calls return it until a changed eligibility snapshot can
  atomically supersede it with one active cycle.
- Continue accepting valid retained `rated`/`rating_voided` evidence against a
  completed predecessor; a correction after successor materialization affects
  only the next unmaterialized cycle.
- Distinguish an empty corpus, exhausted prefetch, stale state, invalid schema,
  authorization denial, and transient service failure with stable typed codes.

The accepted product semantics constrain the exact request/response fields,
bounds, error envelope, and compatibility rules that Lexinet must freeze during
implementation. Required semantic changes then require a new protocol version
and a coordinated consumer update.

## Panel validation boundary

- Require certificate-validated HTTPS even on the Tailnet. Tailnet reachability
  does not replace narrow Lexinet study authorization.
- Prefer an authorized tagged node identity; if a scoped token fallback is
  required, read it from a root-provisioned owner-only file, never an argument,
  URL, image overlay, log, or checked-in environment value.
- Validate protocol version, size/count bounds, UTF-8, every required JSON type,
  identities, positions, event ordering, scheduler provenance, and complete
  structured errors before producing a domain object.
- Treat HTTP success with malformed data as
  `LEXINET.RESPONSE_SCHEMA_INVALID`, never as an empty corpus.
- Retry only declared transient failures with bounded delay, cancellation, and
  the original idempotency identity.
- Make an event durable in the bounded local outbox before displaying it as
  queued. An acknowledgement, not a successful socket write, removes it.
  Rating admission reserves capacity for its append-only Undo correction.
- Do not assign a client-stream sequence or outbox entry to a voice-only first
  `presented` observation or a dependent first reveal. Keep it bounded and
  session-local, label it provisional, and discard it at session termination or
  restart. A study touch or accepted rating matching the active session,
  session item, immutable content revision, and render generation may atomically
  promote the prerequisite observations before any dependent canonical event;
  callers cannot request promotion directly. Restart recovery must enter a
  non-card resume gate rather than automatically render an issued item lacking
  canonical `presented` evidence; touch resume creates the fact before display,
  while voice resume recreates provisional state.
- Persist the complete constituent set of a grouped rating transition. A local
  commit failure leaves prior provisional/render state unchanged. A stable
  terminal batch rejection marks every promotion, rating, and included
  touch-successor fact rejected together, preserves already-visible navigation
  state, releases Undo without a void, and forbids those facts from satisfying
  exhaustion or being reissued under new identities. A voice-successor marker
  outside the batch remains provisional.
- ~~Expose queued, syncing, saved, rejected, offline, and tracking-paused states;
  never imply that an unsynchronized event was saved.~~ Expose queued, syncing,
  saved, rejected, offline, and tracking-paused states;
  expose provisional voice evidence separately and never imply that an
  unsynchronized or provisional event was saved.

## Current scaffold limitation

The implemented `fetch_next_card` / single-review proposal predates the Lexinet
source audit and is not an implementation contract. It has no session identity,
immutable issued order, correction event, scheduler provenance, or safe offline
reconciliation. Keep production services disabled until the approved
`study.v1` contract, generated fixtures, resilient client, and cross-repository
tests replace it together.

## Deployment inputs still required

- Explicit `BHP_LEXINET_SOURCE_DIR` for host contract work; no inferred checkout
  path.
- Actual MagicDNS/base endpoint and validated TLS arrangement.
- Dedicated Tailnet tag/grants and Lexinet study-capability mapping.
- Explicit learner-profile reference, topic set, and bounded cache/outbox values.
- Credential provisioning/revocation procedure for any fallback token.

**Amended 2026-08-08:** made voice-only first presentation and dependent reveal
bounded session-local provisional evidence until a matching study touch or
accepted rating promotes them, with a non-card restart gate and explicit grouped
terminal-rejection disposition. No `LEXI-*` status changed.

**Amended 2026-08-16:** reconciled this transition boundary with ADR 0006. The
`study.v1` product semantics are accepted, while its Lexinet-owned schema,
server implementation, generated fixtures, panel consumer, and contract tests
remain pending. `BHP_LEXINET_SOURCE_DIR` remains the sole checkout input.
