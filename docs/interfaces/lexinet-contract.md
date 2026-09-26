# Lexinet Consumer Contract Boundary

**Status:** `study.v1` is frozen by Lexinet and pinned here for panel
consumption. Lexinet's migration, repository, event-adjudication, merge, and
successor persistence layer, rating-aware scheduler/projectors, cycle
materializer, canonical study service, strict public routes, operator CLI, and
post-commit-only shadow composition are implemented. Lexinet's separate native
TLS Tailnet study router and scoped node-capability/token authorization are also
implemented locally; real Tailnet policy/identity/revocation/certificate
evidence remains a later gate. The panel's resilient study client is
implemented with host and fixture evidence, ADR 0010 records the panel
Tailscale packaging choice, and the historical next-card adapter was removed.

The product behavior is defined in the
[Lexinet study architecture](../architecture/lexinet-study-integration.md).
The machine-readable wire authority is the Lexinet-owned
`${BHP_LEXINET_SOURCE_DIR}/contracts/study.v1/openapi.json`, accompanied by its
`contracts/study.v1/manifest.json` and Lexinet decision
`docs/decisions/0001-freeze-study-v1-contract.md`. The panel does not maintain a
second schema.

## Pinned authority and exported evidence

The checked-in fixture provenance pins:

- protocol `study.v1` and schema version `1`;
- Lexinet repository base revision
  `ce42272df315f357e10998dd48e9ffd1a935c2ff`;
- OpenAPI SHA-256
  `422b4ed6e9336b642bd32b923c8d9989ee0d7e0d624883ae4593a8266d8eaa19`;
- manifest SHA-256
  `c0a0c5e731704978fe0beee88c790762c1cb2cbc2f96fa22749978eff113079e`;
- full contract/example bundle SHA-256
  `b90be0dd4a1054b7de065aee125561a927a5faf19e2f2451090426c0488f4242`.

The panel's
[`contract-provenance.json`](../../vocabulary_panel/tests/fixtures/study_v1/contract-provenance.json)
records those values and a digest for every byte-for-byte exported example.
Refresh and verify the bounded export from an explicit checkout; never infer a
sibling path:

```sh
python3 vocabulary_panel/tools/export_study_v1_contract_fixtures.py \
  --lexinet-source-dir "$BHP_LEXINET_SOURCE_DIR"
python3 vocabulary_panel/tools/export_study_v1_contract_fixtures.py \
  --lexinet-source-dir "$BHP_LEXINET_SOURCE_DIR" --check
```

The repository base revision locates the committed audited source tree. The
contract bundle digest independently identifies the exact contract content and
is the content-integrity pin.

## Frozen `study.v1` operations

The contract exposes raw success resources at three capabilities:

```text
POST /api/v1/study/sessions
GET  /api/v1/study/sessions/{session_id}
POST /api/v1/study/sessions/{session_id}/sync
```

- Create atomically returns the existing active scope or creates one immutable,
  finite coverage cycle for an authorized profile, normalized topic set, and
  card kind. Scheduler configuration is provenance, not part of active-scope
  uniqueness. Distinct creation UUIDs converge on the same nonterminal cycle.
- Resume returns the exact issued session and bounded page. Every item contains
  a complete immutable card projection, live source identity and content
  revision, position, card identity, and selection reason. The session carries
  scheduler name/version, configuration digest, seed, planning time, and exact
  per-profile ingestion high-water.
- Sync accepts at most 64 ordered events under one durable
  `client_stream_id` and contiguous `client_sequence`. `presented` and
  `revealed` are singular transitions. `rated` and `rating_voided` are
  append-only; a rating is exactly one of `again`, `hard`, `good`, or `easy`,
  and a void targets the exact rating UUID being corrected.
- Sync reconciles an immutable already-adjudicated replay prefix, then at most
  one new contiguous suffix. Either partition may be empty. A valid new suffix
  may mix `accepted` and `semantic_duplicate` receipts. Only an `accepted`
  receipt creates scheduler evidence; a semantic duplicate identifies its
  existing canonical fact and ordinal.
- If any new event has a stable terminal domain failure, every event in that
  new suffix receives `terminal_rejected` with the first failure code. Those
  receipts consume the stream positions together, but no new-suffix canonical
  event or per-profile ingestion ordinal is committed. The immutable replay
  prefix is unchanged. Retryable request failures adjudicate no new sequence.
- Local page exhaustion is never authoritative. Successor creation requires a
  fresh predecessor flush receipt and no earlier local pending event. In one
  active-scope transaction Lexinet proves a canonical `presented` fact for
  every predecessor item, completes the predecessor, and links exactly one
  successor. Replays and concurrent creation UUIDs return that successor.
- An unchanged empty eligible corpus converges on one `waiting_for_corpus`
  session. A changed eligibility snapshot may atomically supersede that
  sentinel with one active cycle.
- Valid retained ratings and voids may arrive after predecessor completion. A
  correction after successor materialization affects only the next
  unmaterialized cycle.

Create idempotency is keyed by `creation_request_id`; sync idempotency is keyed
by `sync_id`. Reuse with a different normalized SHA-256 input returns
`IDEMPOTENCY_CONFLICT`. Replaying an identical `sync_id` preserves the
original event receipts, flush receipt, and immutable issued page bytes. The
surrounding session envelope remains live and may report later authoritative
lifecycle or state, so the complete sync response is not promised byte-stable.
All success resources carry `protocol_version`, `schema_version`,
`required_semantics`, and `generated_at`; every HTTP response carries bounded
`X-Request-ID` correlation. Errors preserve Lexinet's existing raw envelope:

```json
{
  "error": {
    "protocol_version": "study.v1",
    "schema_version": 1,
    "code": "STUDY_STATE_STALE",
    "message": "the expected session state version is stale"
  }
}
```

## Compatibility and semantic validation

The panel requires protocol `study.v1`, schema version `1`, and the exact
ordered required-semantics set exported in the provenance file. It rejects an
unknown, missing, duplicated, or reordered required semantic as
`LEXINET.RESPONSE_SCHEMA_INVALID`. It also rejects missing required fields,
wrong types, invalid UUIDs/digests/timestamps, inconsistent identities or
positions, out-of-order sequences, invalid transition cardinality, incomplete
scheduler/source provenance, malformed errors, and bodies outside contract
bounds before producing a domain object.

Lexinet may add optional response fields within `study.v1`; consumers must
ignore additions they do not need after validating all known required fields.
An added required field, added required semantic, cardinality or meaning
change, or removal requires a new protocol version and coordinated atomic
consumer cutover. Requests remain closed objects and Lexinet rejects unknown
request fields.

The contract bounds are:

- request body: 262,144 bytes;
- response body: 1,048,576 bytes;
- normalized topic references: 32;
- events per sync: 64;
- items per page: 64;
- profile/topic reference: 128 Unicode code points;
- card front: 512 Unicode code points;
- definition and example: 16,384 Unicode code points each.

Check `Content-Length` when present and enforce the response ceiling while
streaming; an absent or dishonest header never bypasses the limit. HTTP success
with malformed data is `LEXINET.RESPONSE_SCHEMA_INVALID`, never an empty corpus.

## Transport, deadlines, and cancellation

- Production endpoints require certificate-validated HTTPS, JSON media types,
  and no redirects. Tailnet reachability does not replace narrow Lexinet study
  authorization.
- Lexinet's implemented Tailnet endpoint is native `tsnet.ListenTLS` on fixed
  port 443 with a separate create/resume-and-sync-only router and fresh
  per-request `WhoIs`. GET, operator, health/version, and vocabulary routes are
  loopback-only; forwarded identity/capability headers, raw TCP, Funnel, and
  reverse-proxy fallback are not transport alternatives.
- Prefer an authorized tagged-node identity. If the mutually exclusive
  scoped-token deployment mode is selected, read its bearer from a
  root-provisioned, owner-only file. Credentials never enter arguments, URLs,
  environment diagnostics, logs, fixtures, or images.
- Connect and whole-request deadlines are explicit positive finite
  configuration values. The whole-request deadline must be at least the
  connect deadline. DNS, connection, TLS, upload, response streaming, and
  bounded retries all consume the caller's one whole-request budget; no hidden
  library default may extend it.
- A shared cancellation signal interrupts connection, transfer, retry delay,
  and response parsing. Cancellation returns a typed cancelled outcome, is not
  retried, and cannot publish a partially parsed response.
- Retry only connection failures, timeouts, `429`, and `5xx`, using bounded
  delay, the original creation/sync identity, and remaining caller budget.
  Stable schema, authorization, conflict, and receipt rejections stay visible.

## Freshness validation

Every response's `generated_at` must parse as an RFC 3339 UTC timestamp.
Deployment must supply a positive finite maximum snapshot age and a finite
nonnegative allowed clock skew; no production default is inferred from an
unmeasured board clock or network. When wall time is trusted, reject responses
older than the maximum age or farther in the future than the permitted skew.
When wall time is untrusted, mark wall-clock freshness unknown and use the
monotonic request age plus the server's `state_version` and
`profile_ingestion_high_water` ordering; never silently label the result fresh.

For one session, a response may repeat the same state version for an idempotent
replay but must not regress below an already accepted version. The ingestion
high-water must likewise not regress within the same authorized profile.
Conflicting content at the same identity/version is
`LEXINET.RESPONSE_SCHEMA_INVALID`. Cached content older than the configured
maximum offline interval remains explicitly degraded rather than authoritative.

## Panel delivery boundary

- Network operations run on cancellable workers; render and event threads never
  perform synchronous HTTP.
- Make an event durable in the bounded local outbox before displaying it as
  queued. Only a terminal acknowledgement removes it. Rating admission reserves
  capacity for its append-only Undo correction.
- Voice-only first presentation and its dependent reveal remain bounded,
  session-local provisional evidence. A matching study touch or accepted
  rating may atomically promote prerequisite observations before any dependent
  canonical event. Restart enters a non-card resume gate.
- A stable terminal grouped-batch rejection leaves any immutable replay prefix
  intact and marks every fact in the one new suffix rejected with the first
  failure code. Rejected promotion, rating, and touch-successor facts cannot
  satisfy exhaustion or be reissued under new identities.
- Queued, syncing, saved, rejected, offline, tracking-paused, and provisional
  states remain distinct; the UI never implies that unsynchronized or
  provisional evidence was saved.

## Current implementation boundary

Lexinet now persists the accepted study lifecycle, immutable issued snapshots,
append-only events and terminal receipts, gap-free profile ordinals, merge
invalidation, and atomic successor/waiting replacement boundaries. It also
implements the deterministic `StudyScheduler` seam, `variety_first_v1`,
rebuildable card/event projections, transaction-bound cycle materialization,
the canonical `StudySessionService`, strict bounded handlers/routes, the
operator CLI/client, and authority-checked post-commit-only shadow invocation.
The dedicated bearer and `OperatorStudyAuthorizer` remain the loopback operator
policy. Lexinet HEAD `ce42272df315f357e10998dd48e9ffd1a935c2ff`
implements the local native-TLS Tailnet boundary, fresh transport identity,
mutually exclusive node-capability/scoped-token modes, and local
create/resume-and-sync/profile authorization ceiling. Step 5 still owns the real
deployment evidence and panel packaging decision.

The historical `fetch_next_card` / single-review adapter was removed on
2026-09-25; it had no issued session identity, correction stream, scheduler
provenance, or safe reconciliation. Production panel study requests stay
disabled until the cross-repository live tests, real scoped Tailnet deployment
evidence, and packaging steps are verified together.

Still-required deployment inputs are the explicit Lexinet MagicDNS endpoint and
native TLS certificate evidence, learner profile and normalized topic set,
deadline/freshness/cache bounds, exact Tailnet tag/app-capability policy, and
mode-specific credential provisioning/revocation procedure. ADR 0010 selects
node-capability mode, so the panel provisions no study credential, and the
SHA-256-pinned official ARM Tailscale `1.102.2` package.

**Amended 2026-08-16:** replaced the provisional next-card transition document
with the Lexinet-owned `study.v1` authority, deterministic provenance export,
exact compatibility rules, and panel-side contract tests. This freezes the
cross-repository wire contract without claiming that either runtime data plane
has been implemented.

**Clarified 2026-08-16:** made the already-approved atomic sync boundary
explicit as immutable replay prefix plus one new contiguous suffix, scoped
identical-sync replay stability to receipts, flush receipt, and issued page,
and refreshed executable fixtures. This changes no `study.v1` wire shape,
required semantic, schema version, or protocol version.

**Amended 2026-08-17:** pinned the fixture export to committed Lexinet revision
`158dea68bffb2b77321d20738afb1ddf99b19ad9` and recorded completion of the
Step 2 persistence layer. The scheduler, public study service/CLI, resilient
panel client, authorization, and packaging gates remain pending.

**Amended 2026-08-17:** repinned the unchanged `study.v1` contract bundle to
final Lexinet revision `517da8a3823fbaa0d70018831be75ed734c2f96d` and recorded
Step 3 completion. The scheduler/projector/materializer implementation and
shadow-runner isolation seam now have exact mapped evidence. Public service
composition, routes, CLI, production post-commit shadow invocation, the
resilient panel client, authorization, and packaging remain pending. No frozen
wire semantic or accepted product decision changed.

**Amended 2026-08-17:** repinned the unchanged `study.v1` contract bundle to
final Step 4 Lexinet revision `e8a9d52e35bdd55aa7451f4413c781f8371ebb8f`.
The canonical service, strict routes, bounded operator CLI/client, transitional
operator authorization, immutable response composition, and post-commit-only
shadow invocation now have exact mapped normal, live-PostgreSQL, and race
evidence. The resilient panel client, final scoped Tailnet authorization, and
packaging remain pending; no wire semantic or accepted decision changed.

**Amended 2026-08-17:** recorded clean Lexinet implementation HEAD
`ce42272df315f357e10998dd48e9ffd1a935c2ff` without repinning the unchanged
`study.v1` contract provenance above. Native `ListenTLS`, the separate
create/resume-and-sync router, fresh `WhoIs`, and scoped node-capability/token
authorization are implemented locally, and Verification Item 1 is recorded.
The synthetic `ListenTLS` failure test is wrapper-cleanup evidence only, not
real certificate issuance/MagicDNS/hostname/handshake/invalid-certificate
proof. Real tagged, untagged, wrong/extra-tag, live-revocation,
destructive/cross-profile, and certificate evidence plus the panel packaging
choice remain pending; Step 5 stays unchecked.

**Amended 2026-09-26:** repinned the unchanged `study.v1` contract bundle to
Lexinet `ce42272df315f357e10998dd48e9ffd1a935c2ff` with the export tool; only
`contract-provenance.json` changed. Recorded ADR 0010: the panel authenticates
by node capability with no stored credential and installs the SHA-256-pinned
official Tailscale `1.102.2` arm64 archive. Both `vocabulary-panel.env.example`
files now carry `study.v1` configuration instead of next-card paths. Real
Tailnet evidence and production enablement remain pending.
