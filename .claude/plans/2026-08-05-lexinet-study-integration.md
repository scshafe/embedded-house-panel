# Plan: Integrate Lexinet Study Sessions with the House Panel

**Goal:** Add a secure, resumable Lexinet study service and a production panel
controller that presents varied concept cards through equivalent touch and
local voice navigation/rating commands, records replayable learning evidence,
remains useful through Tailnet interruption, and preserves the supervisor's
exclusive power authority.

**Out of scope:** Vocabulary editing on the panel; a full account system;
claiming scientifically optimized or personalized scheduling; general dictation; text-to-speech;
remote speech recognition; voice as a Linux suspend wake source; selecting a
future product board or final microphone; packaging an unmeasured speech engine
as the production default.

## Context

- The design contract is
  [`docs/architecture/lexinet-study-integration.md`](../../docs/architecture/lexinet-study-integration.md).
- The panel repository is the worktree containing this plan.
- `BHP_LEXINET_SOURCE_DIR` is the sole host-side input for the populated
  Lexinet checkout. The audit found it clean at revision `3e15ef6`; the
  originally supplied directory was empty. Cross-repository scripts must fail
  when the input is absent or invalid and must not infer either workstation
  path.
- The executed Lexinet source is clean on `main` at Step 5 local-boundary HEAD
  `ce42272df315f357e10998dd48e9ffd1a935c2ff`. It is a Go 1.26.5 service and CLI
  backed by PostgreSQL and pins Tailscale `v1.102.2`. In addition to the frozen
  contract, persistence, scheduler, service, routes, CLI, and shadow
  composition, Lexinet now implements native TLS on fixed Tailnet port 443,
  fresh per-request `WhoIs`, mutually exclusive node-capability/scoped-token
  authentication, scoped study authorization, and a structurally separate
  create/resume-and-sync router. Step 5 remains unchecked because real Tailnet
  policy/identity/revocation/certificate evidence and the panel Tailscale
  packaging choice remain external deployment gates; the resilient panel
  client is still Step 6.
- The panel currently has a forward-only fixture-backed `FlashCardSession`, a
  provisional synchronous ~~HTTP~~ HTTPS adapter, and a presentation-only LVGL
  renderer.
  It has no production graphical controller, offline outbox, backward
  navigation, media broker, or voice implementation.
- The personal-agenda design adds a sibling display application. Before this
  plan's graphical work, introduce one neutral `panel_ui` shell as the sole
  ~~LVGL/display/touch owner~~ LVGL/display/application-touch router. Under
  proposed ADR 0005, the supervisor is the sole raw touch observer and
  provenance issuer. Keep `vocabulary_panel` as a subordinate study domain and
  keep calendar/birthday state out of `PanelPolicy`.
- The
  [application capability ownership registry](../../docs/architecture/application-capability-ownership.md)
  makes this plan the first delivery authority, not the feature owner, for the
  neutral shell, shared HTTP runtime, shared media broker, and Buildroot
  migration. Later agenda work extends those exact capabilities.
- Panel-wide intents are namespaced (`study.flip`, `study.next`, `study.back`,
  `study.rate.*`, and touch-only `study.rating.undo`). Voice-eligible forms are
  registered in one versioned, language-tagged spoken-command catalog; the Undo
  intent deliberately has no spoken alias. One shared media broker owns the microphone through
    purpose-scoped leases and consumes only the content-addressed backend
  projection for the canonical active command set plus its matching validated,
  content-addressed recognition-policy artifact.
  Command-listening audio remains broker-local; a future, visibly initiated
  `wish_recording` purpose is governed by the separate personal-agenda design.
- Recommended semantics are: `flip` toggles front/back; `next` advances without
  implying mastery; `back` navigates bounded history and preserves the prior
  face; `again`, `hard`, `good`, and `easy` record an outcome only after the back
  is revealed, then advance. Touch and voice use the same semantic-intent path;
  a touch Undo appends a correction rather than rewriting history.
- Lexinet owns vocabulary, topic eligibility, session order, and durable study
  state. The panel owns only bounded card cache, navigation state, idempotent
  pending events, rendering, and input adapters.
- The proposed active `variety_first_v1` scheduler materializes an immutable
  finite coverage cycle, then uses the latest effective outcome to prioritize
  the next unmaterialized cycle in `again`, `hard`, unseen/unrated, `good`,
  `easy` order. It stores seed, version, reason, and projections, guarantees
  full eligible-corpus coverage before repetition, and makes no due-date,
  frequency, or scientifically optimized spaced-repetition claim.
- Append-only study events are canonical. Named/versioned per-card scheduler
  projections are rebuildable. Version 1 isolates a fake shadow runner from
  production authority; a later explicit experiment may add a pinned FSRS
  implementation after its input mapping and evidence threshold are reviewed.
- The preferred device credential is a tagged Tailnet node identity derived at
  the Lexinet listener with fresh `WhoIs`, authorized only for study
  create/resume and sync on configured profiles. Lexinet implements an
  owner-only scoped-token alternative behind the same native transport and
  local ceiling; it is a mutually exclusive startup mode, not a request-time
  fallback and never the broad loopback operator bearer.
- Tailnet identity identifies an authorized appliance; a Lexinet study profile
  identifies the learner. Re-provisioning the appliance must not reset or fork
  the learner's history.
- Host work stops at a measured on-target recognizer decision gate. A follow-up
  amendment packages only an engine that passes resource, privacy, accuracy,
  latency, and power evidence on the real board and microphone.
- No step may give the UI or voice process authority over backlight or suspend.
  No test fixture, log, diagnostic bundle, command argument, or image overlay
  may contain Tailnet state, bearer material, raw audio, or transcripts.

## Steps

- [ ] ~~**Step 1: Freeze product semantics and the cross-repository contract**~~
  - ~~One completion boundary coupled human review to cross-repository contract
    authoring.~~ Superseded by Steps 1a and 1b; every requirement is
    redistributed below.

- [x] **Step 1a: Review product semantics and the shared command-catalog decision**
  - Obtain human review of the seven Lexinet decisions in the architecture
    document: two-sided `flip`, unrated `next`, face-preserving `back`,
    topic-union corpus, one initial profile with touch/voice ratings,
    append-only learning evidence with rebuildable projections, and
    `variety_first_v1` plus an authority-isolated shadow runner for a later
    pinned FSRS experiment.
  - Review the shared spoken-command catalog decision at the same gate.
  - Record every decision as accepted, changed, or deliberately deferred.
    Silence or a blank remains pending and does not authorize Step 1b.
  - Output: reviewed dispositions for the seven Lexinet decisions and the
    shared spoken-command-catalog decision.
  - Verify: human review confirms every decision has an explicit disposition
    and that no blank or deferred decision was treated as approval.

- [x] **Step 1b: Author the cross-repository study contract**
  - Requires Step 1a.
  - In Lexinet, add a decision note and a canonical machine-readable
    `study.v1` contract for session create/resume/sync, complete card
    projections, `rated` and `rating_voided` events, scheduler provenance,
    singular `presented`/`revealed` transitions, durable `client_stream_id`
    sequencing and terminal `study_event_receipts`, active-scope
    create-or-resume convergence, server-authoritative cycle exhaustion, atomic
    predecessor terminalization/successor linking, the predecessor
    flush-receipt barrier, schema versions, bounds, stable error codes, and
    idempotency.
    Keep raw success resources and the existing error envelope unless a separate
    whole-API migration is approved.
  - Replace the panel's provisional next-card/review proposal with a consumer
    contract that links to the Lexinet authority, pins contract provenance, and
    defines transport, timeout, cancellation, size, and freshness validation.
  - Record the compatibility rule: Lexinet may add optional response fields in
    `study.v1`, but semantic or required-field changes require a new protocol
    version and a coordinated consumer update.
  - Output (Lexinet): decision record, `study.v1` OpenAPI/JSON Schema artifact,
    golden success/error examples, and contract validation test.
  - Output (panel): revised `docs/interfaces/lexinet-contract.md` and generated
    or provenance-stamped contract fixtures; no manually divergent authority.
  - Verify: Lexinet's contract validator rejects an invalid example; a panel
    test identifies the exact pinned `study.v1` protocol/version and rejects
    unknown required semantics.

- [x] **Step 2: Add the Lexinet study persistence model and merge rules**
  - Add paired `0007_study.up.sql`/`.down.sql` migrations following existing
    naming, checks, timestamps, and reverse-dependency conventions.
  - Model one-to-many learner-profile/session/event relationships, ordered
    unique session items, client-generated creation-request/event UUID
    uniqueness, durable random `client_stream_id` plus contiguous stream-scoped
    sequence uniqueness,
    scheduler name/version, normalized corpus identity/hash, predecessor session,
    and optimistic state versioning.
  - Define the active-cycle scope as learner profile, normalized corpus selector,
    and card kind. Enforce one nonterminal session per scope transactionally,
    regardless of a configured scheduler change. Apply a new scheduler only to
    the successor; immediate cutover requires an explicit audited termination.
    Separate client creation-request UUIDs from the authoritative returned
    session identity. Model `active`, `completed`, `waiting_for_corpus`,
    `superseded_empty`, and audited `abandoned` lifecycle states plus unique
    predecessor/successor linking. Treat `active` and `waiting_for_corpus` as
    the mutually exclusive nonterminal states for the scope.
  - ~~Defer derived per-card scheduler state until a scheduler actually needs
    it.~~ Store append-only learning events as the authority and add an explicit
    named/versioned, rebuildable per-card projection for the selected scheduler.
    A void event references the exact rating event UUID; folding treats that
    rating as absent without destroying either fact.
  - Allocate a gap-free per-profile ingestion ordinal transactionally for every
    newly accepted event. Use its high-water mark as the canonical scheduler
    prefix across client instances; retain per-client sequence only for source
    replay and order validation.
  - Add a client-stream receipt ledger binding every terminally adjudicated
    contiguous sequence to its event UUID, normalized hash, disposition, stable
    code, and optional canonical event/ordinal. Accepted events alone enter the
    scheduler ledger; semantic duplicates and stable terminal batch rejections
    consume stream sequences through receipts, while retryable failures do not.
    Bind each newly adjudicated receipt to the immutable flush receipt that
    originated its exact closed new-suffix range; later sync replay preserves
    that origin rather than rebatching old receipts. A terminal batch has one
    self-causative receipt and one shared failure code even when the first
    failing input follows earlier collateral inputs in the suffix.
  - Enforce one `presented` and one `revealed` fact per session item. Require the
    transition order `presented -> revealed -> rated -> rating_voided`, allowing
    each prerequisite from storage or earlier in one ordered atomic batch; a
    second UUID for a singular observation returns the original receipt without
    allocating another ingestion ordinal.
  - Keep editable content canonical in existing idea/name/body tables, while
    storing the immutable front/back/example projection issued with each session
    item plus its source identity and revision/hash. This is historical session
    evidence, not a second editable vocabulary model. Preserve source identity
    if a live idea foreign key is cleared by hard deletion.
  - Make the transaction's locked eligible-source read the cycle-corpus
    linearization point. Record that exact source set and projection in the
    storage transaction, require it before any active plan insert, and have
    storage bind/materialize each immutable item snapshot from those captured
    bytes rather than trusting caller-supplied front/back/example or
    revision/hash fields. Every vocabulary mutation that can change eligibility
    or issued content must take the same deterministic idea-row lock; a change
    ordered after the locked read belongs to the next cycle.
  - Extend and test idea merge/delete behavior so issued item snapshots and
    historical events remain intact, new sessions choose only eligible live
    ideas/the merge survivor, and every named/versioned derived scheduler
    projection has an explicit consolidation path. Because projection payloads
    are opaque to storage, merge must atomically invalidate both source and
    survivor projection rows; the owning named/versioned projector then rebuilds
    the survivor from canonical accepted events through the chosen prefix.
    Storage must never fabricate or generically merge algorithm-owned payload
    bytes.
  - Store an immutable final per-profile ingestion high-water on every
    terminal session, distinct from the scheduler input prefix. Complete an
    active predecessor only through the purpose-specific atomic successor
    operation after validating its exact flush barrier and canonical
    `presented` facts. When a previously empty corpus becomes nonempty, the
    same operation atomically marks the locked `waiting_for_corpus` sentinel
    `superseded_empty` and links exactly one active replacement without a flush;
    an unchanged empty corpus continues to return the existing sentinel.
  - Add a disposable-PostgreSQL migration/storage suite; string-only SQL tests
    are insufficient for unique constraints, transaction replay, rollback, and
    merge behavior.
  - Output (Lexinet): migrations, study models, focused storage interface and
    repository, merge integration, and database tests.
  - Verify: `go test ./internal/storage/... ./internal/model/...`; migrate up,
    exercise stream restart/reprovision, semantic duplicate/invalid transition,
    terminal rejection followed by a valid event, receipt replay/conflict,
    lifecycle constraints and predecessor/successor uniqueness, concurrent
    distinct creation UUIDs for one active scope, duplicate/replay,
    immutable-snapshot, and merge cases against
    disposable PostgreSQL; concurrently ingest from two client instances and
    prove one gap-free per-profile ordinal; migrate down, migrate up again, and
    compare the resulting schema contract.

- [ ] ~~**Step 3: Implement and prove `shuffle_bag_v1`**~~
  - ~~Materialize a rating-independent shuffled bag and retain ratings only as
    inert data.~~ This no longer satisfies the requested rating-aware selection
    behavior; keep it only as a deterministic comparison fixture.

- [x] **Step 3: Implement and prove versioned rating-aware scheduling**
  - Add `internal/study` with `StudyScheduler.plan(StudySchedulingSnapshot) ->
    StudyPlan` as the documented `@extension-point`; inject clock and seed rather
    than reading global time/randomness. Retain `shuffle_bag_v1` only as a test
    baseline, not the active product scheduler.
  - Freeze scheduler hashing as `lexinet-study-digest-v1`: SHA-256 over that
    ASCII domain plus NUL, followed by a purpose and every typed part as a
    four-byte big-endian length and raw bytes. Never hash map iteration or
    ambient JSON. Give configuration, tie, card-content, card-source, and corpus
    inputs distinct purposes; store lowercase hex and golden vectors. Source
    revisions include the chosen source row identities and UTC revisions, while
    the corpus digest includes the resolved sorted topic UUID union, card kind,
    and identity-sorted immutable card source/content revisions.
  - Select only active ideas in the requested topic union with a canonical name
    and usable back text. Use primary definition, first sorted definition, then
    summary as the explicit fallback chain; include at most one sorted example.
  - Implement active `variety_first_v1`: rank each eligible concept by its latest
    effective outcome (`again`, `hard`, unseen/unrated, `good`, `easy`), then by
    latest effective `presented` per-profile ingestion ordinal ascending, with a
    never-presented oldest sentinel sorting before every real ordinal, and
    finally a seeded deterministic tie-breaker among equal sentinels. Receipt
    time is evidence, not a selection key.
    Materialize every eligible concept exactly once in a finite coverage cycle,
    store scheduler name/version/seed and per-item selection reason, and avoid a
    prior-last/successor-first repeat by rotating only within the leading rating
    rank. If no same-rank alternative exists, preserve strict priority and emit
    `boundary_repeat_due_to_priority` provenance.
    Seeded ties apply only between never-presented cards and fall back to the
    lowercase card UUID on digest collision. Duplicate non-null latest-presented
    ordinals across different cards are corruption. If the merge-canonicalized
    predecessor tail equals the first card, left-rotate the maximal leading
    same-rank block exactly once and mark only its new head
    `boundary_repeat_rotated`; never rotate across a rating rank.
  - Keep an issued cycle immutable. Ratings and corrections influence only the
    next unmaterialized cycle; additions, topic edits, archive, and merge changes
    also enter at that boundary. Handle empty and singleton corpora without
    client guesses and keep issued snapshots reviewable through the bounded
    offline-retention window.
  - Fold append-only rating and void events into the named/versioned projection
    and prove it can be discarded and reproduced byte-for-byte. Deliver a
    shadow-runner boundary and fake isolation test, but do not implement FSRS in
    this step. A later explicit experiment may add a pinned FSRS adapter only
    after its input mapping and evidence threshold are reviewed; shadow output
    can never select production order or imply due dates, optimal frequency, or
    calibrated retention.
    Validate and commit the authoritative plan before invoking any shadow
    adapter. The post-commit runner receives deep copies and no store or
    transaction; reverse-order output, mutation, error, timeout, or panic can
    produce only a structured non-authoritative report and cannot change the
    plan, HTTP result, rollback, or retry behavior.
  - Output (Lexinet): scheduler interface, active variety-first implementation,
    shuffle baseline, card projection builder, event projector, mandatory
    authority-isolated shadow runner with a fake adapter, persistence
    orchestration, deterministic unit/property tests, and extension-point docs.
  - Verify: fixed clock/seed tests prove stable plans and provenance; corpus
    sizes 0, 1, 2, and representative larger sets prove uniqueness, coverage,
    exact rating-rank order, oldest-sentinel/latest-presented-ordinal behavior, immutable
    active cycles, correction folding, byte-identical projection replay, topic
    filtering, content fallback, within-rank boundary rotation, unavoidable
    cross-rank repeat provenance, and successor-cycle linking; shadow output
    cannot change any production plan.

- [x] **Step 4: Expose the Lexinet study API and operator CLI**
  - Requires completed Steps 2 and 3. One canonical `StudySessionService` owns
    create/resume, get, and sync. It owns the outer transaction and calls the
    Step 3 materializer through the Step 2 transaction-bound store; handlers do
    not orchestrate repository calls. The common lock order is profile
    ingestion row, active-scope anchor, stable-UUID session rows, client stream,
    then idempotency/receipt rows. Never expose raw `pgx.Tx` outside storage.
  - Close three persistence/composition seams before mounting handlers:
    (1) replace the materializer's caller-pre-sized item-ID slice with a bounded
    injected allocator invoked only after durable receipt resolution and locked
    eligible-corpus capture; replay and convergence never allocate; (2) add a
    forward-only study API migration that binds each sync flush receipt to its
    original page request and canonical immutable `SessionPage` JSON text plus
    SHA-256, so identical sync replay cannot regenerate a page from mutable
    live-source links; and (3) replace the two-read committed-cycle helper with
    one read-only consistent-snapshot authority read containing the exact
    creation receipt, immutable session issuance fields, and ordered items. A
    study-owned validator compares that fresh authority byte-for-byte with the
    committed materialization before any optional shadow call.
  - Put public composition in `internal/studysession`, not the pure
    `internal/study` scheduler package or HTTP package. Its focused repository
    adapter may import storage; storage must not import the service.
    `StudySessionService` is the `@canonical study_session_service` and owns
    normalization, authorization, admission, transaction invocation, committed
    readback, stable paging, and response construction. Each handler calls
    exactly one service method and never receives the existing broad server
    `Store` interface.
  - Implement idempotent `POST /api/v1/study/sessions`, resumable
    `GET /api/v1/study/sessions/{id}`, and atomic
    `POST /api/v1/study/sessions/{id}/sync` from the frozen contract.
  - Session create-or-resume locks the active-cycle scope so distinct concurrent
    creation UUIDs converge on one authoritative unfinished session. Successor
    creation requires a predecessor flush receipt, validates that every
    materialized predecessor item has one canonical `presented` fact, and in
    the same locked transaction records an exact input ingestion high-water,
    marks the predecessor `completed`, and links exactly one successor. Local
    prefetch exhaustion is insufficient. Concurrent distinct creation UUIDs
    and replays naming the predecessor return that linked successor. An
    unchanged empty corpus returns one `waiting_for_corpus` sentinel; only a
    changed eligibility snapshot with an eligible card atomically marks it
    `superseded_empty` and creates an active cycle. Sync accepts ordered event
    UUIDs and
    positions, writes terminal per-stream receipts for accepted,
    semantic-duplicate, or stable rejected sequences, advances authoritative
    state transactionally, assigns each newly accepted canonical event the next
    per-profile ingestion ordinal in submitted order, and returns an identical
    bounded page of already issued items. A duplicate retains its original
    ordinal; retryable failures adjudicate no sequence.
    Reconcile an immutable replay prefix before one new contiguous suffix. A
    valid suffix may mix accepted and semantic-duplicate receipts. If any new
    suffix event has a terminal domain failure, every new suffix sequence gets
    the first failure's stable terminal-rejection code and no new event,
    ordinal, or projection is committed. Retryable/request-level failures write
    no receipt. Increment session state once for newly written terminal
    receipts, not for pure replay or an empty flush.
  - Scope every idempotency key to its authorized session/principal and bind it
    to a normalized request hash. An identical replay returns the original
    receipt; changed input returns `409 IDEMPOTENCY_CONFLICT`. Accept ordered
    `presented`, `revealed`, `rated`, and `rating_voided` events under one
    persisted `client_stream_id` and contiguous sequence. Every observation or
    causal target must be authorized and must already exist in durable storage
    or precede the dependent event in the same atomic ordered batch.
    Permit at most one effective rating per session item; a void permits a later
    replacement without deleting prior attempts or corrections.
    Continue accepting valid retained rating/correction events for completed
    predecessors; once a successor exists, those late facts affect only the
    next unmaterialized cycle.
    Introduce an opaque `StudyPrincipalID`/`StudyAuthorizer`; Step 4 maps the
    existing authenticated operator path to one stable operator principal and
    never derives or persists bearer bytes. Step 5 replaces only principal
    derivation. Scope create keys by principal/profile/scope/request UUID and
    sync keys by principal/session/sync UUID.
    The Step 4 composition installs a named `OperatorStudyAuthorizer` that
    grants create/read/sync only to the exact stable operator principal across
    all profiles. This explicit transitional allow-all-operator policy is not
    an absent authorization check. Study routes always use dedicated bearer
    middleware: an empty configured token, missing/wrong bearer, or missing
    typed principal context returns the same `401` and performs zero service
    work, even though legacy loopback routes may retain optional authentication
    until Step 5. Step 5 replaces derivation/policy without changing service or
    storage signatures.
  - Canonicalize normalized request hashes with RFC 8785 JSON after domain
    normalization. Exclude the idempotency UUID from its content hash and keep
    principal in the key namespace; include the operation, versions, canonical
    profile and resolved sorted topic UUIDs, card kind, predecessor or null,
    page, and, for sync, path session, stream, expected version, ordered events,
    and page. Normalize UUIDs to lowercase and times to UTC RFC3339Nano; reject
    duplicate resolved topics. Event hashes include every normalized event
    field including event UUID and sequence.
    Pin the RFC-listed Apache-2.0 Go implementation
    `github.com/cyberphone/json-canonicalization` at
    `v0.0.0-20241213102144-19d51d7fe467`; wrap it behind one
    `@canonical study_request_jcs` leaf and execute RFC 8785/I-JSON vectors.
    Do not substitute ordinary `encoding/json` ordering or hash raw request
    bytes. Resolve topic references to stable IDs and canonical display refs,
    reject aliases resolving to one duplicate ID, then revalidate and lock all
    normalized topic IDs inside the materialization transaction used for
    eligibility capture. A vanished topic is `STATE_STALE`, never a plausible
    empty corpus; a later rename does not change the ID-based scope.
  - Creation receipts are immutable. `created` means that request materialized
    a new root; `converged_to_existing` means its first adjudication found the
    unfinished cycle; `successor` binds a predecessor request to the newly made
    or already-linked successor. Reserve `resumed` and `replayed` in v1;
    identical-key replay returns the original receipt. Return 201 only when the
    current transaction materialized the returned session, otherwise 200.
    GET/nested session resources have a null creation receipt. Empty-sentinel
    activation creates and links one successor without a flush receipt.
  - Distinguish empty corpus, exhausted local prefetch, stale content/state,
    malformed input, authorization denial, and transient service failure with
    stable codes. Apply strict decoding, body/count/text bounds, request
    correlation, and context cancellation.
    Strict decoding counts raw bytes including whitespace, accepts at most
    262,144 bytes, and rejects invalid UTF-8, duplicate members, unknown members
    at every level, multiple/trailing values, unsupported content encoding, and
    non-JSON media types before service invocation. Marshal a complete success
    or error before writing headers; every response is at most 1,048,576 bytes
    and carries a server-generated bounded `X-Request-ID`. Map only allowlisted
    stable study codes to 400/401/403/404/409/413/429/500/503 and never copy
    `StudyError.Cause`, internal context, panic values, bodies, card content,
    profile refs, credentials, URLs, or raw errors into HTTP/CLI output or logs.
  - Apply finite host-service budgets: 5-second header, 30-second body/read,
    60-second write, 120-second idle, and 64-KiB header ceilings; a 30-second
    authoritative study-operation context; and non-queuing admission of four
    global/two per-principal authoritative operations. Saturation returns `429`
    before storage. Cancellation or timeout before commit writes no receipt;
    cancellation after commit leaves the result replayable. Shadow work owns a
    separate bounded slot and is invoked only for a newly created nonempty cycle
    after exact committed readback; replay, convergence, waiting, commit
    failure, and readback mismatch invoke it zero times, and shadow
    error/timeout/panic/busy never changes the authoritative HTTP result.
  - Add `lexinet study session`, `lexinet study sync`, and `lexinet study state`
    operator commands through the existing Go client; do not couple the Linux
    panel to the macOS Swift client or its drifting fixtures.
    Freeze grammar as `study session --request FILE|-`, `study sync SESSION_ID
    --request FILE|-`, and `study state SESSION_ID [--after-position N] [--limit
    N]`; do not accept inline JSON. Bound stdin/file input to the protocol body
    ceiling and preserve request/retry metadata in structured errors. Page by a
    deterministic immutable-item prefix under both the requested count and a
    983,040-byte page budget, then marshal and enforce the one-MiB whole-response
    ceiling before writing.
    Use a dedicated study HTTP client rather than generic `doJSON`: reject
    endpoint userinfo/query/fragment, redirects, non-HTTP schemes, cleartext
    non-loopback origins, TLS failure, and HTTPS-to-HTTP downgrade; use finite
    connect/TLS/header/total deadlines; read request/success/error bodies through
    max-plus-one overflow detectors; and preserve only bounded status, stable
    code, request ID, retry-after, and retryability metadata. Never
    automatically retry POST.
  - Output (Lexinet): handlers, focused store interfaces, CLI/client commands,
    fake-store unit tests, live database handler tests, and contract examples
    emitted from real types.
  - Verify: `go build ./... && go test ./... && go vet ./...`; replay the same
    session, page, sync, and event UUIDs and prove one response/state transition;
    reuse an idempotency key with changed input and receive an explicit conflict;
    accept and idempotently replay an offline `presented`, `revealed`, `rated`,
    then `rating_voided` chain in one atomic batch; reject invalid transitions;
    reconcile a distinct-UUID duplicate observation without a new ordinal;
    concurrently sync two client streams and prove unique gap-free
    per-profile ingestion ordinals and a stable high-water prefix; prove a
    terminal rejection or semantic duplicate cannot block the next contiguous
    event and that changed receipt replay conflicts; race distinct creation
    UUIDs and receive one active cycle; reject local-only exhaustion, race
    concurrent successor requests and receive one linked successor, replay it,
    poll one stable empty sentinel, then atomically activate it after eligible
    content arrives; acknowledge a final predecessor rating before successor
    creation and prove its exact snapshot high-water includes it; accept a
    retained late correction against a completed predecessor without rewriting
    its existing successor;
    prove `next` or `presented` never writes a `good` outcome; mutate source
    content after session creation and prove its issued projection is unchanged;
    run CLI round trips against a disposable server and PostgreSQL instance.
    Also prove topic deletion cannot become empty success, exact-limit versus
    over-limit raw bodies, duplicate/ambiguous JSON rejection, normalized JCS
    equivalence and semantic mutation, all stable error mappings/redaction,
    auth/authorizer short-circuiting, admission/cancellation, committed-readback
    mismatch suppressing shadow, sync-page byte stability across merge/delete,
    response/page budgets without partial writes, endpoint/redirect/TLS policy,
    and CLI request/response overflow handling. Run focused service/server/CLI
    and live-PostgreSQL suites under `go test -race`.

- [ ] **Step 5: ~~Establish least-privilege HTTPS Tailnet access~~ Remediate the
  live unauthenticated Tailnet path and establish least-privilege HTTPS access**
  - Treat this as remediation, not greenfield authorization work. At the
    reviewed baseline, an empty service bearer made `requireBearer` return the
    protected route group unchanged; the shipped Docker configuration leaves
    `auth_token` empty; and the then-documented TCP-mode `tailscale serve` bridge
    supplies neither caller identity nor application authentication. All
    vocabulary mutation routes shared that group, so the nontagged-node
    negative test did not hold. Lexinet HEAD
    `ce42272df315f357e10998dd48e9ffd1a935c2ff` now closes that local code and
    documentation exposure with a separate native-TLS study boundary. This is
    implemented local behavior, not real Tailnet deployment evidence.
  - Review-amendments Step 15a's fail-fast empty-token/Tailnet startup guard was
    completed in Lexinet commit `178d9ea` and is only a narrow interim
    mitigation in the historical sequence. The final local boundary supersedes
    it for native Tailnet service: the broad bearer remains loopback/operator
    authority, is not accepted as Tailnet-node identity, and must not be
    provisioned onto the physically accessible panel. A host-side raw TCP or
    reverse-proxy exposure is not a supported alternative.
  - Split listener authentication from capability authorization. A tailnet
    listener wrapper derives a principal with `tsnet`/LocalAPI `WhoIs` and
    marks the transport in request context; loopback/operator bearer behavior
    remains available and cannot forge that marker.
    The implemented Tailnet listener is native `tsnet` `ListenTLS` on port 443,
    calls `WhoIs` for every request, and serves a separate study-only router. It
    does not accept forwarded identity/capability headers, raw TCP, Funnel, or
    a reverse-proxy fallback. The router exposes only create/resume and sync;
    operator GET/state and every vocabulary route remain loopback-only.
  - Require a dedicated appliance tag and authorize it only for study
    create/resume/sync on configured learner-profile references. Add a negative
    test proving the same principal cannot access another profile or create,
    edit, merge, archive, or delete vocabulary content.
    Require an exact configured caller tag plus a configured custom application
    capability under a domain the operator controls. Implemented capability
    objects are closed, bounded operation/profile sets and are intersected with
    Lexinet's local create/resume-and-sync/profile ceiling. Tailscale's union of
    matching grants never overrides that local ceiling. Derive the persisted
    `tailnet-node:<uuid>` principal deterministically from the opaque stable node
    ID using the frozen two-stage domain-separated UUIDv5 mapping; never persist
    the raw stable ID, node name, address, user, or tag as identity.
  - Use certificate-validated native `ListenTLS` only. If the real Tailnet
    cannot issue and validate the required MagicDNS hostname certificate, fail
    the deployment gate. There is no Tailscale HTTPS reverse-proxy fallback and
    the panel client never silently downgrades to cleartext application HTTP.
  - Review and pin supported Tailscale versions before relying on current
    TLS/identity behavior. Lexinet now pins Go `1.26.5`, Tailscale `v1.102.2`,
    and its Docker builder together. Keep the narrowly scoped token-file mode
    behind the same native transport and study capabilities.
    Node-capability mode and scoped-token mode are mutually exclusive startup
    configurations, never request-time fallbacks. Scoped-token mode still
    requires native Tailnet TLS plus a successful per-request `WhoIs`, uses an
    independently provisioned nonsecret subject UUID and owner-only token file,
    and grants only the configured profiles and create/resume-and-sync surface.
    It never derives identity from token bytes.
  - Lexinet now provides redacted Tailnet grant/config shapes and revocation
    guidance. Deployment must replace every placeholder with reviewed
    host/tag/domain/profile values, provision an already enrolled owner-only
    mode-`0700` state directory, and keep auth keys, node state, certificates,
    and tokens out of Git, image overlays, arguments, URLs, logs, and diagnostic
    archives.
  - Output: Lexinet principal/authorizer boundary and listener tests; panel and
    Lexinet deployment documentation; redacted grant/config examples; explicit
    credential-revocation procedure.
  - Verify: unit tests reject spoofed identity headers and wrong tags; a real
    tagged test node can call study endpoints over validated HTTPS but receives
    denial on destructive routes; a nontagged node cannot reach or authorize to
    the study service; certificate failure is fail-closed.
  - Verification scope clarification: Item 4 requires real native certificate
    issuance for the configured MagicDNS hostname, client hostname validation,
    a completed TLS handshake, and fail-closed invalid-certificate/hostname
    behavior. A synthetic injected `ListenTLS` wrapper failure proves local
    cleanup only and is not Item 4 evidence. The additional real wrong/extra-tag,
    live-revocation, and cross-profile cases remain Step 5 evidence gates
    without changing the four mapped verification clauses above.
  - Version review on 2026-08-17 selected current stable Tailscale `v1.102.2`,
    whose module requires Go `1.26.5`; Lexinet pins both and its Docker builder
    together at the recorded HEAD. The verified Buildroot `2026.05.1` input
    supplies Go `1.26.3` and Tailscale `1.78.1`, so the panel runtime cannot be
    called complete until the user selects either a SHA-256-pinned official ARM
    binary package or an approved source-build toolchain upgrade. Do not substitute
    the newest old-toolchain-compatible Tailscale release and do not check this
    step without sanitized real tagged, nontagged, wrong-tag, revocation, and
    certificate evidence.

- [ ] **Step 6: Replace the panel's provisional client with a resilient study data plane**
  - Replace `fetch_next_card` assumptions with typed create/resume/sync models
    matching `study.v1`; validate protocol version, session-item and source
    identities, bounds, UTF-8, immutable content projection/revision, positions,
    and complete error objects.
  - Preserve the guarded ~~HTTP~~ HTTPS adapter's TLS verification, redirect
    refusal, credential-file handling for the fallback path, size limits, trace
    IDs, and typed failures. Move every request to a cancellable worker so
    rendering and input threads never block on Wi-Fi or Tailnet recovery.
  - **Shared HTTP runtime sub-step**
    - Create `shared/http_runtime/` as the canonical panel-wide HTTP lifetime,
      cancellation, and common-transport-budget capability. It is the sole owner
      of `curl_global_init` and `curl_global_cleanup`; no feature client, adapter,
      test fixture, or executable may call either function directly.
    - Construct and successfully initialize the runtime at the process
      composition root before any client, worker, or easy handle exists. On
      shutdown, reject new work, propagate the shared cancellation signal, join
      every registered HTTP worker, destroy all handles, and only then perform
      global cleanup. Initialization failure and invalid/repeated shutdown return
      explicit structured outcomes rather than leaving a partially live runtime.
    - Define shared cancellation primitives and one validated transport-budget
      contract for connect deadline, whole-request deadline, and response-byte
      ceiling. Reject zero, negative, overflowed, or internally inconsistent
      values before work starts; do not hide a caller timeout behind an unrelated
      default.
    - Inject the live runtime and validated budgets into `HttpLexinetClient` and
      its worker instead of constructing global libcurl state inside the
      vocabulary feature. Vocabulary-owned `study.v1` models, authorization and
      credential policy, request/response validation, retry classification,
      cache/outbox rules, and delivery/degraded semantics remain in
      `vocabulary_panel/`; the shared runtime must not become a generic study
      client or persistence layer.
    - Output (shared HTTP runtime): `shared/http_runtime/` README, CMake target,
      public lifetime/cancellation/budget interfaces, libcurl implementation,
      focused runtime tests, and the injected vocabulary adapter/worker seam.
    - Verify (shared HTTP runtime): after review-amendments Step 12 adds the
      `host-dev` reachability path, run `cmake --preset host-dev`,
      `cmake --build --preset host-dev`,
      `ctest --preset host-dev --output-on-failure -R
      'http_runtime|http_lexinet_client'`, and
      `scripts/verification/verify_host_cpp.sh vocabulary_panel`. Named tests
      prove init-before-handle ordering, one process lifetime across multiple
      injected clients, validated-budget rejection, cancellation unblocking,
      worker join and handle destruction before cleanup, fail-closed shutdown,
      TLS verification, redirect refusal, and response-size limits. A
      tracked-source assertion using `git grep -n -E
      'curl_global_(init|cleanup)[[:space:]]*[(]' -- '*.c' '*.cc' '*.cpp'
      '*.cxx' '*.h' '*.hpp'` must report both calls only below
      `shared/http_runtime/`. The
      current `host-debug` preset and `verify_host_cpp.sh` do not compile the HTTP
      adapter/runtime, so they are not evidence for this sub-step until that
      Step 12 verification work lands.
  - Add a bounded, versioned, owner-only card/session cache and idempotent event
    outbox with crash-consistent atomic commits. An event payload, sequence, and
    UUID plus the persisted random `client_stream_id` become durable together
    before it is shown as queued; the stream sequence survives process restart
    while reprovisioning creates a new stream; pending events
    are never evicted and leave the retry queue only after Lexinet acknowledges
    the UUID or returns a stable typed terminal rejection. Retain a bounded
    content-free rejection receipt so unsaved state survives restart.
    Reserve enough outbox capacity for one `rating_voided` correction before
    accepting a rating. Persist at most one Undo lease naming the exact rating
    UUID and immediate successor snapshot. General admission counts its reserved
    slot even after the rating is acknowledged. Undo consumes the reservation;
    the next accepted non-Undo study intent, a replacement lease, explicit
    routing to another product surface, or study-session termination releases
    it. ~~A terminal server rejection marks the rating visibly unsaved, removes
    the affordance, and releases the reservation without emitting a void.~~ A
    grouped rating transaction records every current-item promotion, `rated`,
    and touch-successor `presented` constituent. Local commit failure changes
    nothing. Stable terminal batch rejection marks every constituent visibly
    rejected together, preserves already-rendered face/history/successor state,
    excludes the rejected facts from exhaustion and identity-changing reissue,
    and releases Undo without emitting a void; a voice-successor marker outside
    the group remains provisional.
    Network loss, absence, and suspend retain it.
    Corruption or incompatible schema fails visibly rather than becoming empty.
    Preserve the existing canonical event rule for non-voice session entry and
    touch-originated presentation. A first item reached only through voice gets
    a bounded session-local provisional `presented` marker and any dependent
    first `revealed` marker; neither receives an outbox entry or client-stream
    sequence, and both disappear at session termination or restart. Restart
    discards the provisional render and enters a non-card resume gate rather
    than redisplaying an issued item with no canonical `presented`; touch resume
    creates it before display, while voice resume recreates provisional state. A
    study touch bound to the active session, session item, immutable content
    revision, and render generation, or an accepted rating for that item,
    atomically promotes the prerequisites. No caller supplies a promotion flag
    or event UUID.
  - Make item count, byte count, prefetch low-water mark, pending-event count,
    and maximum offline retention explicit validated configuration. Never evict
    an unacknowledged event; flush it before evicting its referenced card
    snapshot. Use the server ingestion ordinal for total order and server receipt
    time as the canonical timestamp when panel UTC is untrusted.
  - Render the cached current card before networking after resume, sync in the
    background, and expose explicit `queued locally`, `syncing`, `saved`, and
    `rejected` delivery states plus cache exhausted, offline with history,
    service empty, contract invalid, and retryable error. If the outbox is full,
    ~~any transition needing a new fact fails visibly under the documented
    “tracking paused” policy.~~ Any transition needing a new canonical fact
    fails visibly under that policy. ~~Navigation may continue only through
    already presented/revealed history; it cannot silently show an unseen cached
    item or reveal a back without queue capacity.~~ Voice navigation may show an
    unseen cached item or first reveal only as visibly provisional
    `voice — confirmation required` state; it cannot sync, satisfy cycle
    exhaustion, or masquerade as queued/saved evidence. Validate and retain scheduler name/version,
    cycle seed, and selection-reason provenance with each cached issued item.
    When a rated card has no prefetched successor, advance to a durable
    completion/refill-required snapshot that retains Undo; background refill
    cannot replace it while the lease remains live. Preflight the entire rating
    transition: ~~rating plus Undo reservation needs two slots; showing a
    never-presented successor needs a third and atomically commits its
    `presented` event before render. With only two, use a tracking-paused
    non-card successor and leave the cached unseen item unshown.~~ reserve the
    rating, Undo correction, and every current-item provisional prerequisite
    promotion. A touch rating that shows a never-presented successor also needs
    and atomically commits that successor's `presented` slot before render; a
    voice rating leaves its newly shown successor provisional. If the base and
    prerequisite slots fit but the touch successor slot does not, use the
    tracking-paused non-card successor. Missing any base or prerequisite slot
    rejects the rating and leaves the current card unchanged.
  - Before requesting a successor cycle, serialize against local event creation,
    flush and acknowledge every pending predecessor event, persist the returned
    stream/profile flush receipt, and include it in create. Never materialize a
    successor from a stale receipt merely because prefetch is low; only the
    server's every-item-presented predicate can complete the cycle. Treat
    `STUDY_CYCLE_NOT_EXHAUSTED` as stable non-destructive state, and reuse a
    returned `waiting_for_corpus` sentinel rather than polling into new empty
    sessions. An Undo that wins the local barrier is flushed first; one accepted
    after server materialization remains valid against the completed predecessor
    and is the documented late-correction case.
  - Use a local fake HTTP server plus Lexinet-exported golden examples; add a
    cross-repository live contract test that accepts an explicit Lexinet source
    path rather than assuming either local checkout path.
  - Output (panel): study client models/adapter, cache/outbox, cancellable worker,
    configuration, fixtures with provenance, and failure/recovery tests. The
    Shared HTTP runtime sub-step retains its own output boundary above.
  - Verify: ~~`scripts/verification/verify_host_cpp.sh vocabulary_panel`;~~ the
    Shared HTTP runtime sub-step's intended `host-dev`/tracked-source checks pass,
    and the Step 12-expanded
    `scripts/verification/verify_host_cpp.sh vocabulary_panel` runs the adapter,
    runtime, and vocabulary tests; tests
    cover truncation, malformed UTF-8/JSON, unknown version, timeout,
    cancellation, TLS failure, disk full, atomic-write interruption at every
    boundary, corrupted cache, duplicate acknowledgement, idempotency conflict,
    ordered retry, same-batch presented/reveal/rating/void replay, stream
    sequence/receipt persistence and reprovisioning, pending-event
    non-eviction, acknowledgement while Undo remains available, ~~permanent
    rejection release without a void,~~ grouped permanent-rejection disposition
    for all promotion/rating/successor constituents with preserved visible state
    plus exhaustion exclusion plus Undo release without a void, final-card
    completion with Undo,
    ~~rating behavior with two versus three available slots and with an
    already-presented forward successor,~~ rating behavior across rating/Undo capacity plus provisional-prerequisite
    promotion capacity plus touch-successor durability versus voice-successor
    provisional handling and an already-presented forward successor,
    reservation consume/release, no
    reservation leakage across restart, and
    ~~full-outbox presented/revealed/rated action rejection versus fact-free
    history navigation,~~ full-outbox touch fact/promotion/rating rejection versus visibly provisional
    voice presentation/reveal and fact-free history navigation plus exact-bound
    promotion with restart/session-end discard plus a non-card restart gate,
    final-rating
    flush-before-successor ordering including
    an Undo racing the barrier, server rejection of locally exhausted but
    authoritatively incomplete cycles, one stable empty sentinel, retained late
    correction, and bounded card eviction; live
    contract test passes against the Lexinet disposable server.

- [ ] ~~**Step 7: Refactor the panel into one production interaction controller**~~
- [ ] ~~**Step 7: Add vocabulary interaction to the neutral production panel shell**~~
  - ~~One completion boundary coupled the platform spine to the vocabulary
    surface.~~ Superseded by Steps 7a and 7b; every requirement is
    redistributed below.

- [x] **Step 7a: Establish the neutral production panel spine**
  - Requires roadmap Step 1 and Lexinet Step 1a. It may run in parallel with
    Lexinet Steps 1b–6.
  - Establish `panel_ui` as the unprivileged sole owner of LVGL, display,
    application hit testing, and touch routing. Add ADR 0005's supervisor-owned
    raw-touch observation/provenance ports and normalized non-blocking event
    stream; the UI cannot open the raw device, author provenance, redeem an
    observation, or send a feature request on that stream.
  - Version the supervisor protocol so its daytime value is lifecycle-neutral
    (`day_active`), preserve the one-client sequence-bound
    dark-frame/render-ready handshake, and keep feature routing entirely
    outside the supervisor.
  - ~~Add one versioned `InteractionIntent` model and one controller method used
    by touch, host fixtures, and voice.~~ Add one versioned, namespaced
    panel-wide intent envelope used by touch, host fixtures, and voice. Carry
    one closed top-level semantic `intent_id` with no version 1 domain payload.
    Register `study.flip`, `study.next`, `study.back`, `study.rate.*`,
    touch-only `study.rating.undo`, and the reserved touch-only
    `agenda.start_wish_recording` and `agenda.submit_wish` IDs with their
    respective `capture_start` and `external_submission` effect classes and no
    spoken forms; this step does not enable the agenda effects.
  - Require voice evidence to carry a catalog `spoken_form_id` belonging to the
    bound active set and mapping exactly to `intent_id`; touch emits the same ID
    directly. Validate source instance, monotonic sequence, render generation,
    active surface, lifecycle mode, and transition state. Do not queue input
    across loading, commit, suspend, or resume.
  - Assign one local ingress order. The first valid intent wins a configurable
    measured conflict window; classify a matching second intent as duplicate and
    a differing one as conflict rather than applying or queuing it. Give every
    accepted intent visible acknowledgement and every rejection a typed,
    content-free diagnostic reason.
  - ~~Wire the controller directly to the supervisor mode socket and make the
    vocabulary renderer the production UI.~~ Wire the neutral `panel_ui` shell
    to the supervisor socket. Add its pure controller, headless renderer,
    sibling-surface contracts, and neutral production executable. Replace
    printable-ASCII assumptions with one shell-owned bounded Unicode/font
    policy supporting future contact and calendar text, and add the
    `bhp-panel-ui` Buildroot entry point.
  - ~~Output (panel): a vocabulary-owned production executable and
    vocabulary-specific Buildroot entry point.~~ Output: canonical panel intent
    envelope and registry, neutral shell and pure controller, headless renderer,
    supervisor handshake extension, supervisor touch-observation/provenance
    seams, neutral production executable, `bhp-panel-ui` Buildroot entry point,
    and deterministic shell/protocol/race tests.
  - Verify: host shell tests reject invalid intent/source/form mappings,
    nonmonotonic sequences, stale generations, and invalid lifecycle/surface
    states; duplicate/coalescing and conflict ordering are deterministic;
    loading, commit, suspend, and resume mode transitions reject or cancel
    affected input rather than queuing it;
    supervisor tests prove a crashed, unready, or slow touch consumer cannot
    hold power policy hostage, no bright frame precedes render-ready, and
    disconnect or restart invalidates pending touch observations; packaging
    exposes one neutral UI entry point.

- [x] **Step 7b: Add the vocabulary surface to the neutral panel shell**
  - Requires Lexinet Steps 6 and 7a. Consume their shell, envelope, worker,
    cache, and outbox; do not create peer capabilities.
  - Make a two-sided card and bounded browser-style history canonical in
    `FlashCardSession`: new cards open on front, `flip` toggles, `next` follows
    forward history or consumes prefetch, and `back` restores the prior face.
  - Decompose session availability, connectivity, face/navigation, and
    event-delivery state instead of extending the current fused
    `FlashCardStage`; one reducer enforces their combined invariants and emits
    the render snapshot.
  - Route `study.flip`, `study.next`, `study.back`, `study.rate.*`, and
    touch-only `study.rating.undo` through one vocabulary controller method.
    Accept a rating only while the back is revealed, durably enqueue it before
    advancing, and expose a one-tap touch Undo that durably appends
    `rating_voided` for the exact current durable rating UUID. Undo carries no
    caller-supplied target, resolves the UUID exclusively from the current
    durable lease, and is forbidden for voice.
    Preserve one semantic reducer path while making evidence durability
    source-qualified: voice-only first presentation/dependent reveal is
    provisional, a bound study touch or accepted rating promotes only the
    current item's prerequisites, and a successor newly shown by a voice rating
    remains provisional.
  - Connect the vocabulary controller to the shell's cancellable study worker,
    cache/outbox, and subordinate LVGL surface. Add touch targets for
    flip/next/back, Again/Hard/Good/Easy, and rating Undo; render through the
    shell-owned Unicode/font policy.
  - Output: vocabulary intent/session/controller types, reducer and render
    snapshot, subordinate LVGL vocabulary surface and touch flow, and
    deterministic vocabulary/controller/outbox tests.
  - Verify: host tests prove touch semantics, face/history restoration, boundary
    no-ops, forward-history reuse, prefetch depletion, no implicit rating,
    rating rejection on the front, durable-before-advance, touch-only
    lease-resolved Undo with no arbitrary target that leaves the successor
    visible, final-card completion/refill state with Undo, one effective rating
    per session item with replacement only after void, outbox correction
    reservation and terminal-rejection release, and study-worker cancellation
    on shell-directed surface or lifecycle closure.

- [ ] **Step 8: Add the privacy-bounded voice intent work stream on the host**
  - ~~Define a vocabulary-only media broker whose entire protocol assumes that
    every capture emits only `flip`, `next`, or `back`.~~ Define one shared,
    least-privileged media-broker process that alone may own raw ALSA capture,
    and one purpose-scoped lease protocol. Enable only `command_recognition` in
    this plan: it emits namespaced semantic intents and never exports audio,
    transcripts, or raw engine scores. Reserve, but do not implement,
    `wish_recording` for the separately reviewed birthday-wish work stream.
  - Add `CommandRecognizer` as an engine-independent `@extension-point` plus a
    deterministic fake/recorded-corpus harness. Create the one canonical
    language-tagged spoken-command catalog and schema, with namespaced intent,
    phrase variants, reviewed phoneme variants/provenance, active contexts,
    effect class, and status. Encode the hard version 1 admission matrix:
    reversible local effects, `bounded_learning_observation`, and
    correctable ratings may qualify only with their distinct safeguards;
    `capture_stop` is restricted to the recording purpose's stop-only set; and
    capture start, external submission, power, and illumination are forbidden.
    Voice admission never makes `presented` or `revealed` correctable: a
    voice-only first observation stays provisional until the exact active item
    is confirmed by touch or an accepted rating.
    Pair review cannot waive this matrix. Add a reproducible two-phase
    acceptance gate. Phase A source-independently inspects every registered
    `intent_id`, including touch-only IDs, traverses all registered handlers,
    and records every endpoint-to-effect pair plus its deduplicated effect set;
    unresolved edges conservatively reach every compatible target and
    unenumerable edges fail. Phase B derives voice-emittable IDs from each
    validated active spoken-form projection and applies the unchanged matrix to
    every effect in each ID's Phase-A set. Any one forbidden effect fails.
    Touch-only IDs must be absent from every voice projection.
  - Derive one content-addressed `ActiveCommandSet` for each allowed context,
    then deterministically project it into backend-specific grammar,
    keyword/token, pronunciation-lexicon, or fake artifacts. Record catalog,
    active-set, projection-format, backend build/model, artifact, and
    `recognition_policy_digest` identities in voice intent evidence. Define one
    strict recognition-policy schema and canonical JSON serializer/builder for
    VAD, endpointing, confidence/runner-up margins, silence, cooldown/debounce,
    and post-processing. Bind each policy artifact to its backend artifact and
    recognizer build/model, derive its digest from canonical bytes, and have the
    broker recompute it on load; never trust a caller-supplied digest, maintain
    handwritten feature grammars, or imply that every backend consumes one
    universal grammar format.
  - Add a deterministic analysis/candidate-adviser tool that reports exact text
    and phone collisions, normalized phoneme edit similarity, invocation-stripped
    similarity, shared prefix/suffix, containment, context overlap, and optional
    feature-weighted distance. Fail on overlapping exact collisions and require
    review for provisional thresholds or unreviewed out-of-vocabulary
    pronunciations; rank proposed phrases by their minimum distance from active
    commands without describing the score as a safety probability. Version and
    test text/phone normalization, empty handling, edit-similarity denominator,
    prefix/suffix denominator, invocation stripping, and worst-case aggregation
    across pronunciation variants using golden metric fixtures.
  - Implement complete-phrase grammar matching for the approved active-context
    navigation and `Lexi again/hard/good/easy` rating phrases, one accepted
    intent per utterance, replay rejection, silence/cooldown handling, and a
    visible acknowledgement plus touch Undo for ratings. A false rating may
    affect an already-materialized successor cycle; its correction affects the
    next unmaterialized cycle and that residual risk must remain explicit.
  - Gate command capture on a ~~supervisor-issued generic media-safety window~~
    supervisor-published lifecycle window and a broker-owned,
    broker-validated `command_recognition` lease, plus `day_active`, the
    active Lexinet surface, asserted presence, a connected intent consumer, and
    any available physical mute state. The supervisor need not know application
    purpose semantics. Any surface change, night, absence, suspend, mute,
    disconnect, or fault must close ALSA and wipe volatile audio. ~~The broker
    acknowledges quiescence by a deadline.~~ Its closure reply is telemetry,
    not proof. ~~Before blank/suspend the supervisor independently verifies the
    kernel-visible capture owner/device closure and, where exposed, microphone
    rail-off state.~~ The supervisor blanks immediately, then verifies that
    evidence before the suspend-state write. Missing independent observation
    fails the media-enablement evidence gate; broker failure or timeout takes
    the bounded fail-closed termination/media-off path. Record a software-only
    limitation when prototype hardware cannot prove microphone rail-off. Voice
    never blocks touch, immediate blanking, or power policy.
  - Freeze one broker lease state machine. For a future touch-gated purpose, a
    successful observation claim separately creates a uniquely identified
    immutable `armed` lease. The UI receives only its opaque `lease_id`; start
    carries only that ID over the same authenticated connection. The broker
    validates its stored peer, provenance, purpose, lifecycle, and deadlines
    against current state before atomically consuming start authorization and
    transitioning `armed -> active`; stop transitions `active -> closed`; and
    expiry, lifecycle closure, consumer disconnect, mute, or broker restart
    transitions `armed|active -> closed`. Repeated, closed, expired, and
    mismatched starts return distinct structured outcomes. This plan keeps
    `wish_recording` disabled.
  - Keep touch fully functional with the broker missing, muted, crashed, denied
    microphone access, or returning malformed messages.
  - Update process, power, UX, protocol, threat-model, and service-sandbox
    documentation together; explicitly state that PIR, not speech, wakes Linux.
  - Output (panel): canonical catalog/schema, pronunciation lock/provenance,
    confusability report and candidate adviser, active-command-set builder,
    content-addressed backend projectors, recognition-policy schema, canonical
    artifact builder/configuration, runtime digest verification,
    namespaced intent/evidence protocol,
    purpose-scoped shared media broker/fake recognizer, separate broker
    lease/quiesce channel, shell adapter, host corpus harness, service
    definitions, and privacy/failure tests. Do not package a real recognizer as
    selected or enable `wish_recording`.
  - Verify: schema/catalog tests prove every active phrase has reviewed
    pronunciation provenance, a namespaced intent, contexts, and effect class;
    forbidden voice effects fail even with a pair waiver; active command sets
    and every backend projection reproduce deterministically and carry the exact
    artifact/policy chain; recognition-policy canonicalization reproduces its
    digest, mismatched policy/backend/model identities fail closed, and changing
    any decision-affecting recognition setting changes the policy digest; exact
    overlapping collisions fail; golden normalization and
    near-neighbor/candidate fixtures report independently reproducible vectors;
    ~~host tests prove touch/voice transition equivalence including all four
    ratings, one intent per utterance, front-side rating rejection, visible
    acknowledgement/Undo, no stale/replayed action, shutdown at every gating
    transition, volatile-buffer erasure, malformed-client isolation, and
    continued touch operation;~~ host tests prove touch/voice
    semantic-transition equivalence including all
    four ratings while voice-only first presentation/dependent reveal remains
    provisional, exact-bound touch/rating promotion is atomic, a voice-rating
    successor remains provisional, unpromoted state cannot sync or satisfy
    exhaustion and disappears at session end/restart, restart enters a non-card
    resume gate, grouped terminal rejection dispositions every constituent, one
    intent per utterance, front-side rating rejection, visible
    acknowledgement/Undo, no stale/replayed action, shutdown at every gating
    transition, volatile-buffer erasure, supervisor-observed capture closure
    without trusting broker self-report, malformed-client isolation, and
    continued touch operation; repository
    search and captured network tests find
    no raw audio or transcripts outside the in-memory broker test boundary.
    Phase A reports every intent's endpoint-to-effect pairs and deduplicated
    effect set; Phase B proves that every voice projection excludes touch-only
    IDs, checks every reachable effect under the unchanged matrix, and rejects
    the projection if any effect is forbidden. No admitted voice-emittable ID
    reaches `capture_start`, external submission, power, or illumination.
    Broker tests cover the one-shot
    `armed -> active -> closed` state machine and structured repeat/closed/
    expiry/binding failures.

- [ ] ~~**Step 9: Package and exercise the complete nonselected-engine integration**~~
- [ ] **Step 9: Package the shared shell and nonselected-engine integration**
  - Replace or narrow the vocabulary-specific UI package and service with the
    neutral `bhp-panel-ui` package. Add separate service users/groups,
    role-specific Unix-socket directories, per-service state paths, restart
    limits, dependency ordering, configuration, and diagnostic redaction for
    the supervisor, shell, vocabulary feature, and media broker. Reserve a
    distinct personal-context-client principal and provenance ACL contract for
    later agenda packaging without installing that absent service.
  - Ensure only the broker can open the exactly classified microphone device,
    only `panel_ui` can open the display, only the supervisor can open the
    exactly classified touch device or write backlight/power interfaces, and no
    role shares a service UID. Atomically revoke the vocabulary/UI service's
    current raw `bhp-touch` membership when supervisor forwarding is enabled.
    Provenance peers reciprocally validate exact UIDs and root-owned socket
    identities; protocol version/session/sequence, disconnect, and restart fail
    closed. Do not use generic `audio` or `input` groups and do not embed Tailnet
    state or Lexinet credentials in the image.
  - Extend build/deploy/provisioning scripts for explicit Lexinet endpoint,
    profile, topic set, cache/outbox bounds, host contract-test path, catalog
    revision, language tag, active-command-set digest, backend projection format,
    backend artifact digest, recognizer build/model identity, recognition-policy
    artifact path, and derived digest. Preserve safe dry-run and explicit target
    behavior.
  - Run a Tailnet fault matrix: unavailable at boot, slow recovery, disconnect
    during sync, duplicate response after timeout, server restart, certificate
    rotation/failure, stale session, corrupted cache, and cache exhaustion.
  - Run a lifecycle matrix with the fake recognizer: day/night, present/absent,
    UI/broker crash, rapid touch/voice overlap, suspend before sync completion,
    and resume with queued events.
  - Output: reproducible image integration, deployment/config docs, redacted
    fault-injection scripts, and machine-readable test summaries.
  - Verify: `make verify`; Linux Buildroot package/config evaluation passes;
    cross-repository end-to-end tests prove deterministic rating-aware
    selection, offline navigation/rating/Undo, idempotent reconciliation,
    least-privilege route denial, generated-catalog integrity, and independent
    supervisor progress under every injected UI/network/voice failure.

- [ ] **Step 10: Compare constrained recognizers on the actual BeagleBone and stop at a decision**
  - Select and document the real microphone/interface first; verify electrical,
    kernel, ALSA, and power behavior without granting the UI device access.
  - Build isolated, removable PocketSphinx JSGF/FSG and quantized sherpa-onnx
    keyword-spotting experiment packages from the same canonical active command
    sets but their appropriate backend-specific grammar versus keyword/token and
    pronunciation-lexicon projections. Do not promote either into the production
    image during the experiment.
  - Before tuning, freeze development, single-use comparison/selection, and
    untouched final-confirmation manifests. Sessions are disjoint for every speaker;
    speakers are disjoint when the intended population permits, and each
    intended speaker otherwise has held-out sessions. Tune only on development
    data. Each inspected decision manifest is single-use: a result-informed
    change requires a fresh predeclared manifest from reserved uninspected
    sessions or new data. Compare candidates on the selection set, then require
    the unchanged selected build/policy to pass final confirmation. Capture at
    least 100 held-out confirmation utterances per active command and closest
    catalog neighbors across intended speakers, distances, orientations, and
    representative room noise, plus at least 24 held-out confirmation hours of
    active-listening negative audio. Include separately labeled loudspeaker
    replay, crafted/psychoacoustic audible inputs, ultrasonic or near-ultrasonic
    modulated carriers, and structure-borne out-of-band injection against the
    exact microphone, analog front end, ADC, and enclosure; human inaudibility
    is never rejection evidence. Retain consented/redacted raw corpus
    material outside Git and commit only content-free manifests/measurements.
  - Measure command success, a directional command-by-command confusion matrix,
    wrong-command substitution, broker-level false accepts, UI-applied false
    actions, p50/p95
    visible-action latency, RSS, peak memory, CPU, image delta, thermal behavior,
    ~~active power, quiesce latency, and 100 suspend/resume cycles with
    stale-event detection.~~ active power, independently observed
    capture-owner/device and available rail-off quiesce latency, and 100
    suspend/resume cycles with stale-event detection.
  - Compare results with the provisional gates in the architecture document and
    requalify the entire active catalog whenever an engine/model, microphone,
    pronunciation lock, phrase, active set, backend projection, or recognition policy
    changes. Predeclare denominators and calibration rules; report 95% binomial
    intervals for utterance metrics and exact Poisson exposure intervals for
    false-event rates. Record every artifact/policy/corpus/split/round hash and failed
    condition; do not average away per-command failures or describe a 24-hour
    observed count as the underlying household rate.
  - Output (panel): isolated experiment recipes, schema-valid redacted results,
    and an ADR selecting one engine, selecting “touch only on Black,” or
    declaring the board unsuitable for this feature.
  - Verify: human review can reproduce all metrics from named artifacts and
    prove the selected unchanged artifact/policy passed an untouched
    confirmation manifest that was never reused after result-informed changes; no
    production default changes without the ADR; `make verify` passes and the
    release image remains unchanged until the plan is amended for the measured
    winner.

## Done criteria

- Product semantics and API ownership are explicit, versioned, and
  machine-testable in both repositories without duplicate hand-maintained
  authority.
- Lexinet returns complete, topic-filtered, nonrepeating, rating-aware coverage
  cycles with scheduler provenance and accepts idempotent rating/correction
  reconciliation while append-only events remain authoritative.
- ~~The panel renders cached cards promptly, exposes honest offline/sync state,
  and implements equivalent `flip`, `next`, `back`, and four-rating behavior for
  touch and semantic voice intents, including a touch correction path.~~ The
  panel renders cached cards promptly, exposes honest offline/sync state, and
  implements equivalent `flip`, `next`, `back`, and four-rating semantic
  behavior for touch and voice, while voice-only first presentation/dependent
  reveal remains visibly provisional until exact-bound touch/rating promotion;
  touch Undo remains the correction path.
- ~~One neutral `panel_ui` owns display/touch and routes namespaced intents.~~
  One neutral `panel_ui` owns display and application touch routing; the
  supervisor alone observes raw touch and issues single-use provenance; and
  vocabulary remains a feature reducer rather than the appliance shell.
- The panel's Tailnet identity can use only study capabilities; compromise does
  not grant vocabulary mutation, and transport/authentication fail closed.
- ~~Raw command-listening audio remains confined to the shared media broker;
  `wish_recording` is not enabled by this plan, the broker cannot control or
  delay suspend, and the PIR remains the wake path.~~ Raw command-listening
  audio remains confined to the shared media broker; `wish_recording` is not
  enabled by this plan, the broker cannot control or delay suspend,
  supervisor-observed capture closure—not broker self-report—is
  the quiescence proof, and the PIR remains the wake path.
- One reviewed command catalog produces canonical active command sets,
  deterministic backend projections, and reproducible confusability reports;
  the deterministic two-phase gate derives source-independent endpoint effects
  before checking voice projections against the unchanged matrix; static scores
  remain advisory and the on-target corpus supplies the actual directional
  confusion evidence.
- Host, database, contract, fault-injection, and Buildroot verification pass in
  both repositories.
- On-target evidence ends in an explicit recognizer/board decision. Packaging a
  measured winner is a deliberate amendment or follow-up plan, not an implicit
  guess in this one.

**Amended 2026-08-05:** the personal-agenda proposal exposed that the planned
vocabulary executable, global intent enum, media broker, Buildroot package, and
service ownership would become competing appliance authorities. ~~Steps 7–9~~
Steps 7a–9 now converge on one neutral `panel_ui`, namespaced feature intents, one
purpose-scoped media broker, and role-specific IPC/state ownership. Lexinet
~~Steps 1–6 remain otherwise independent.~~ Step 1a is the review gate; Steps
1b–6 remain otherwise independent.

**Amended 2026-08-05:** ~~the end-to-end roadmap permits the neutral shell and
protocol-v2 foundation inside Step 7 to begin after Step 1, in parallel with
Lexinet server/data work. Step 7 is not checked complete until Step 6 is
integrated and the vocabulary surface, intent routing, LVGL ownership, and
render-readiness tests all pass.~~ The independently verifiable Step 7a may
begin after roadmap Step 1 and Lexinet Step 1a, in parallel with Steps 1b–6;
Step 7b requires both Step 7a and Step 6.

**Amended 2026-08-06:** the user asked for Again/Hard/Good/Easy through both
touch and voice, a modular rating-aware next-card algorithm, durable evidence
for later scheduler changes, and tooling that keeps a growing multi-application
voice vocabulary distinguishable. Step 3 now replaces the rating-inert shuffle
with `variety_first_v1` behind a versioned scheduler seam; Steps 2, 4, 6, and
~~7~~ 7b make ratings and corrections replayable; Steps 8–10 add one phoneme-provenance
catalog, canonical active command sets, content-addressed backend projections,
deterministic command-confusability analysis, candidate advice, and later
on-device directional confusion evidence. Independent review further fixed the
canonical per-profile ingestion ordinal, same-batch correction rule, single Undo
reservation lifecycle, intent-field invariants, shadow-runner scope, hard voice
effect admission, artifact identity chain, reproducible metric formulas, and
held-out statistical qualification protocol.

**Amended 2026-08-07:** reconciled ~~Steps 7–9~~ Steps 7a–9 and the done
criteria with proposed ADR 0005. The supervisor becomes the raw touch observer, the UI retains
application routing, touch-only start/submit intents receive stable IDs, the
two-phase call-graph gate derives effects before voice projection admission,
the broker lease start is one-shot, and packaging revokes raw UI touch access
under distinct exact service principals. No step or product status changed, and
no hardware behavior is claimed.

**Amended 2026-08-07:** corrected the production adapter transport wording to
HTTPS, matching the client's fail-closed URL validation. The intentionally
loopback fake HTTP test server and existing code symbol names remain unchanged;
no checkbox or decision status changed.

**Amended 2026-08-07:** split Steps 1 and 7 into independently verifiable
review/authoring and spine/vocabulary boundaries. All four replacement steps
remain pending; no product-decision status or hardware claim changed.

**Amended 2026-08-07:** added the named Shared HTTP runtime sub-step to Step 6.
It makes tracked `shared/http_runtime/` the sole process-wide libcurl lifetime
owner, orders cleanup after cancellation and worker join, shares validated
transport budgets, and injects the runtime without moving study semantics out
of `vocabulary_panel/`. Its intended host tests and tracked-source assertion are
explicitly gated on the review-amendments Step 12 work that makes the currently
omitted HTTP adapter/runtime reachable. No checkbox or product-decision status
changed.

**Amended 2026-08-08:** applied the review-amendments Step 14 decisions. A
voice-only first `presented` fact and dependent reveal are bounded provisional
session state until exact-bound touch or rating promotion; a non-card restart
gate and grouped terminal-rejection disposition close the recovery branches,
and executable capacity/test clauses cover them. Broker self-report is telemetry
rather than quiescence proof; immediate blanking precedes bounded supervisor
verification before the suspend-state write. The on-target acoustic corpus
includes crafted audible and out-of-band injection classes. No `LEXI-*` or
`VOICE-*` status, checkbox, production default, or hardware claim changed.

**Amended 2026-08-16:** reframed Step 5 as remediation of the reviewed live
empty-bearer Tailnet exposure, recorded that its nontagged-node negative test
does not currently hold, and separated the narrow Step 15a fail-fast guard from
the final scoped identity/TLS design. The guard is complete in sibling Lexinet
commit `178d9ea`, but a separately provisioned nonempty service bearer remains
a native-listener or external-proxy prerequisite in the interim. No checkbox,
`LEXI-*` status, or other pending product-decision status changed.

**Amended 2026-08-16:** the user accepted `LEXI-001`–`LEXI-007` and
`VOICE-001` without blanks or deferrals. ADR 0006 records the exact
dispositions and the shared spoken-command-catalog choice. Step 1a was
completed after its human proof row and repository documentation verification
were recorded.

**Amended 2026-08-16:** completed Step 1b at sibling Lexinet commit `3a15a08`
and the matching panel consumer boundary. Lexinet now owns the canonical
`study.v1` OpenAPI/schema, manifest, decision record, golden resources, and
negative validator. The panel export is provenance-stamped from that committed
authority and reproduces contract bundle SHA-256
`2b58b02ba543bac297763a3d8a2008645dcf18f18d221ac20c596580cf7be096`;
its test rejects an unknown required semantic. Both automated proof rows are
recorded without claiming any Step 2 persistence or service implementation.

**Amended 2026-08-16:** completed the independently verifiable Step 7a host
spine. Canonical shared code now owns application identity, strict protocol-v2
encoding/digests, and the normalized-touch stream; `panel_supervisor` owns the
immutable fail-dark presentation gate and authority-measured touch provenance;
and persistent `bhp-panel-ui` assembles the neutral controller, Unicode policy,
headless renderer, bounded lifecycle reconnect loop, and a backend that can
emit only `failed`. The intent registry reserves the reviewed vocabulary and
agenda IDs without enabling agenda effects. Packaging installs one neutral UI
entry point under the distinct unprivileged UID and leaves its service disabled.
Mapped host shell, supervisor, packaging, all-slice, and repository proofs pass.
No physical renderer, production normalized-touch transport, ARM package build,
on-target evidence, vocabulary surface, or Step 7b completion is claimed.

**Amended 2026-08-16:** completed pre-implementation inventories for Steps 3
and 4 and resolved their underspecified technical boundaries without changing
`study.v1` product semantics. Step 3 now freezes typed length-framed scheduler
digests, total ordering and same-rank boundary rotation, corruption behavior,
and post-commit-only shadow isolation. Step 4 now explicitly requires Steps 2
and 3, gives one service the outer transaction and lock order, freezes the
opaque principal seam and normalized hashes, distinguishes replay prefixes from
one atomic new suffix, keeps creation receipts immutable, and bounds the exact
CLI/pagination surface. Both steps remain unchecked pending implementation and
their mapped executable evidence.

**Amended 2026-08-16:** Step 2 execution review fixed the live-vocabulary
linearization and opaque-projection merge boundaries. The locked eligible-source
read is the current cycle's corpus point; storage captures and binds issued
snapshot bytes while all eligibility/content mutators share deterministic idea
locks. Idea merge invalidates disposable source/survivor scheduler projections
for namespace-owned canonical replay instead of synthesizing unknown payload
fields. Step 2 remains unchecked pending the full event-receipt, ordinal,
successor-barrier, migration, concurrency, and repository evidence.

**Amended 2026-08-17:** Step 2 execution review made receipt-batch provenance
and terminal lifecycle evidence explicit. Every first-adjudication batch is a
closed new-suffix range owned by its immutable flush receipt; later replay does
not rewrite receipt origin, and grouped terminal rejection permits a later
causative input while preserving one cause and code for the whole suffix. Each
terminal session records its final accepted-event high-water. The atomic
successor operation also owns the no-flush transition from a locked empty
sentinel to one linked active replacement when eligibility becomes nonempty.
Step 2 remains unchecked until the production event adjudicator, ordinal race,
successor/waiting replacement, and their exact mapped proofs pass.

**Amended 2026-08-17:** completed Step 2 at sibling Lexinet commit
`158dea68bffb2b77321d20738afb1ddf99b19ad9`. The paired migration, study
models, transaction-bound repository, atomic event adjudicator, deterministic
merge invalidation, and successor/waiting replacement operations now enforce
the accepted persistence contract. All four exact mapped PostgreSQL/model
proofs passed and are recorded. Step 3 scheduling and Step 4 service/API work
remain unchecked and pending.

**Amended 2026-08-17:** completed Step 3 at sibling Lexinet commit
`517da8a3823fbaa0d70018831be75ed734c2f96d`. The `StudyScheduler` extension
point, active `variety_first_v1`, shuffle baseline, card/event projectors,
transaction-bound cycle materializer, and authority-isolated shadow-runner seam
now have exact mapped unit and PostgreSQL evidence. The panel fixture provenance
is repinned to that final committed source without changing the contract bundle.
The public `StudySessionService`, study routes and CLI, and production
composition that invokes shadow work only after authoritative commit remain
Step 4 work; Step 4 and every later incomplete step stay unchecked. No accepted
product decision changed.

**Amended 2026-08-17:** completed Step 4 at sibling Lexinet commit
`e8a9d52e35bdd55aa7451f4413c781f8371ebb8f`. The canonical
`StudySessionService`, strict study HTTP routes, dedicated transitional operator
authorization, bounded CLI/client, immutable replay/page composition, and
post-commit-only shadow invocation are implemented. All 22 mapped obligations
passed with uncached exact, focused, live-PostgreSQL, aggregate, and race
evidence; the disposable harness left no containers. The panel export is
repinned to this committed source without changing the frozen contract bundle.
Step 5 scoped Tailnet identity/TLS, Step 6 resilient panel data plane, packaging,
and every hardware gate remain pending. No accepted product decision changed.

**Amended 2026-08-17:** converted Step 5's reviewed trust boundary into an
executable native-tsnet design before implementation. Tailnet node-capability
and scoped-token modes are mutually exclusive; both require native
certificate-validated TLS and fresh per-request `WhoIs`, and only the former
accepts the exact configured caller tag plus custom application capability.
The Tailnet router is structurally limited to create/resume and sync, with a
frozen domain-separated stable-node principal mapping and a local profile/
operation ceiling. Current stable Tailscale `v1.102.2` requires Go `1.26.5`.
Lexinet can pin that boundary, but panel packaging and real Tailnet policy,
identity, certificate, and revocation evidence remain explicit user/external
gates; Step 5 stays unchecked.

**Amended 2026-08-17:** recorded clean sibling Lexinet HEAD
`ce42272df315f357e10998dd48e9ffd1a935c2ff`. The local native
`tsnet.ListenTLS` transport, separate create/resume-and-sync router, fresh
per-request `WhoIs`, frozen stable-node principal mapping, mutually exclusive
node-capability/scoped-token authentication, local operation/profile ceiling,
redacted deployment shapes, and revocation guidance have landed. Verification
Item 1 is recorded. Separate focused suites also cover bounded/closed grant
parsing, mode mixing, owner-only token/state validation, lifecycle cleanup, and
scoped operation/profile enforcement without widening that mapped item. The
passing synthetic `ListenTLS` failure test proves the wrapper closes and
exposes no fallback; it is not Verification Item 4 proof.
Real correctly tagged, untagged, wrong/extra-tag, live-revocation,
certificate-issuance/MagicDNS/hostname/handshake/invalid-certificate,
destructive-denial, and cross-profile evidence remain pending, as does the
panel choice between the SHA-256-pinned official ARM package and an approved
source-build toolchain. The frozen `study.v1` fixture provenance is unchanged,
no roadmap checkbox changed, and Step 5 stays unchecked.


**Amended 2026-09-25 (panel-side implementation and bounded proof):**
`b5ca0b3` implements the Step 6 data plane and shared HTTP runtime. The earlier
warning that `verify_host_cpp.sh` cannot reach the adapter/runtime is now
resolved: its vocabulary slice configures the HTTP-enabled `host-dev` target
and runs those CTests. `.build/study-commit-verify.log` and
`.build/study-commit-docs.log` record the isolated commit's passing repository
and documentation gates. Tracked-source inspection reports `curl_global_init`
and `curl_global_cleanup` only under `shared/http_runtime/`.

Named tests cover strict wire validation, TLS/hostname/redirect/size refusal,
whole-request deadlines, cancellation and joins, two-sided bounded history,
source-qualified provisional observations, durable ratings and touch Undo,
rejection recovery, cache eviction, five atomic-write process-death boundaries,
and real fixture-HTTPS initial creation and synchronization response-loss
replay. Initial and successor creation UUIDs are persisted before network use.
The fixture roundtrip restarts both peers, retains the same client stream,
converges idempotent successors and flushes a late predecessor correction.
The per-clause map records the proven host cells; the live contract test
against the external disposable Lexinet server remains open.
No sibling repository or live Tailnet deployment was modified.

Step 7b now routes touch and validated fake-broker intents through the same
shell/controller/reducer and subordinate LVGL surface. The native/guest reports
at `.build/sim/native/report.json` and `.build/sim/vm/report.json` each record
29 passing scenarios. This visible integration does not by itself replace the
step's complete reducer and shell-cancellation matrix. Step 8 has a persistent
fake broker process, generated catalog/active-set/backend/policy artifacts,
confusability fixtures, two-phase effect checks and one-shot lease/closure tests.
Pronunciations are semantic fixtures; production authenticated lifecycle/claim
IPC, independent capture closure, real capture and acoustic qualification remain
unproven. `wish_recording` is disabled.

Step 9 has simulation-specific Debian services, DRM page-flip completion,
supervisor-only raw evdev, authenticated normalized forwarding, synthetic sysfs
backlight and uinput presence. Both QEMU input protocols and the Mac Cocoa
pointer path are exercised. Suspend remains disabled. This is not production
Buildroot package/config evaluation, live Tailnet/authorization evidence, a real
recognizer selection or any FRDM measurement. Steps 6, 8 and 9 remain
unchecked until their remaining mapped obligations are recorded. Step 7b's
complete host clause is recorded below.


**Amended 2026-09-25 (guard proof completion):** follow-up host tests caught
and fixed Back from completion losing the remembered card, an error at the first
history boundary, and touch traversal into provisional history without durable
prerequisite promotion. The focused nine-test CTest suite passes after those
fixes. New cases prove replacement ratings only after an exact void,
already-presented forward reuse, full-outbox touch refusal versus provisional
voice, session-end discard and restart gates, duplicate acknowledgements,
stream reprovisioning, reservation release across restart, a stable empty
sentinel, injected ENOSPC preserving the committed payload, and rejection
excluding successor creation even after local navigation reaches the end.
Verified-TLS fixture tests additionally exercise conflicting create/sync
idempotency payloads, same-batch presented/revealed/rated/void replay and
server rejection of an unpresented predecessor. Shell integration proves
boundary no-ops and real in-flight HTTPS cancellation on night/surface closure.
These host proofs are recorded in the map. The final combined `make verify`
completed at `2026-09-26T05:58:55Z` in `.build/virtual-panel-final-verify.log`,
and the refreshed Debian guest passed 37/37 CTests. Step 7b is complete: its
whole host clause covers touch semantics, face/history and boundary behavior,
front-rating refusal, durable advance, exact touch Undo and replacement ratings,
correction capacity/rejection, and shell-directed in-flight HTTPS cancellation.
It consumes Step 6's implemented panel-side capabilities; the external disposable
Lexinet run remains the unrecorded Step 6 join proof and is not implied by
Step 7b's completion.

**Amended 2026-09-26:** the user selected the SHA-256-pinned official ARM
static Tailscale archive at `1.102.2` and node-capability panel authentication
(panel ADR 0010). This settles Step 5's packaging choice only. The real tagged,
untagged, wrong-tag, live-revocation and certificate evidence remains open, so
Step 5 stays unchecked.

**Amended 2026-09-26:** recorded the Step 6 join proof against an external,
disposable Lexinet. `make verify-lexinet-live` builds Lexinet `ce42272` from an
explicit `BHP_LEXINET_SOURCE_DIR`, serves loopback `study.v1` with disposable
PostgreSQL 16 and a disposable bearer, and drives the panel's
`HttpLexinetClient` and durable reducer. It covers create/resume, idempotent and
crash replay, idempotency and state conflicts, authentication failure, server
restart, offline recovery, successor linking and atomic suffix rejection
([validation](../../docs/testing/lexinet-live-contract-validation.md)). The
evidence is `simulation_not_hardware_evidence`. Step 5's real Tailnet evidence
remains open, so Steps 5 and 6 stay unchecked.
