# Lexinet Consumer Contract Boundary

**Status:** transition document. The current panel adapter is a scaffold; the
proposed `study.v1` successor is pending roadmap Step 1 human review and does
not yet exist in Lexinet.

The product semantics, event model, scheduler provenance, and executable work
are defined in the
[Lexinet study architecture](../architecture/lexinet-study-integration.md) and
its linked implementation plan. The canonical machine-readable API contract
will live in Lexinet. This repository will consume generated or
provenance-stamped fixtures rather than maintain a second schema.

## Proposed `study.v1` boundary

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

The exact request/response fields, bounds, error envelope, and compatibility
rules are frozen in Lexinet only after the pending decision bundle is approved.
Required semantic changes then require a new protocol version and a coordinated
consumer update.

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
- Expose queued, syncing, saved, rejected, offline, and tracking-paused states;
  never imply that an unsynchronized event was saved.

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
