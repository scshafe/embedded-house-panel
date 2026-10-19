# Vocabulary panel

This domain owns the unprivileged Lexinet study feature beneath `panel_ui`.
Its subordinate render surface shares the shell-owned LVGL lifetime.
`FlashCardSession` is the canonical reducer, durable outbox and browser-history
owner. The shell owns input ordering, source authentication, routing and rendering;
the supervisor alone observes raw touch and controls power (ADR 0005). A renderer
consumes `FlashCardSnapshot` and cannot change learning state or power policy.

**Amended 2026-09-25:** replaced the historical four-stage next-card/review
scaffold with the accepted two-sided `study.v1` model. The old `FlashCardStage`,
`ReviewCommand`, `ReviewReceipt` and next-card HTTP methods were removed. This is
host/fixture integration evidence; it does not close Tailnet deployment,
production service enablement or any FRDM gate.

## Public interfaces

- `FlashCardSession::apply_intent` is the one semantic transition path for
  flip, next, back, four ratings, touch-only Undo and resume. The exact current
  session/item/content-revision/render-generation binding is mandatory.
- `FlashCardSnapshot` separates availability, connectivity, face, delivery,
  stale/clock status, provisional evidence and available actions. History is
  bounded and restores each item's last visible face.
- `StudyStore` commits a complete candidate state before publishing its effects.
  `FileStudyStore` uses an owner-only directory and file, exclusive process lock,
  bounded writes, temporary-file fsync, atomic rename and directory fsync.
  Corrupt, incompatible and unwritable files fail visibly. A post-rename
  directory-sync failure is explicitly indeterminate and requires restart.
  The boundary observer is a test seam for actual process-death injection.
- `MemoryStudyStore` is explicitly fixture-only. Its snapshots have
  `durable_storage=false`; applications that need restart use `FileStudyStore`.
- `LexinetClient` has typed `create_or_resume` and `sync` operations. The strict
  `study_wire_codec` consumes Lexinet's frozen `study.v1` contract, including
  required semantics, complete immutable cards, scheduler/source provenance,
  UTF-8/string/page limits, error objects and terminal receipts. The external
  contract authority is Lexinet at the pinned revision, fetched from GitHub or
  taken from an explicit `BHP_LEXINET_SOURCE_DIR` checkout.
- `HttpLexinetClient` borrows the process's `shared/http_runtime` instance.
  `StudyWorker` owns one bounded work/completion mailbox; rendering polls it and
  never performs HTTP. Per-operation cancellation and shared shutdown both
  interrupt work; generation cancellation suppresses stale completions.
- `StudySynchronizer` (`@canonical study_synchronizer`) drives one
  `FlashCardSession` through one `StudyWorker` from the UI thread. It keeps one
  operation in flight, waits a pacing interval (1 s) between attempts, and sends
  only while the caller reports the study surface active (study surface, day
  mode, presence not absent). Without a session it creates; a
  `refill_required` session gets one successor with the predecessor's flush
  receipt; a pending outbox syncs. A non-retryable error stops it until
  `reset()`, and `cancel()` drops the in-flight completion. Every host that
  shows the study surface uses it; the simulator was the first.
- `read_study_client_configuration` (`@canonical study_client_configuration`)
  reads the live client's settings from the `BHP_VOCABULARY_PANEL_*` lines of
  an environment file (`config/vocabulary-panel.env.example`, or the image's
  `panel-ui.env`; other keys and comments are skipped). Every study key is
  required once. It rejects with a `STUDY_CONFIG.*` code that names the key,
  never the value:
  - an unknown or duplicated key, quotes, spaces or `$` in a value;
  - a placeholder (`replace…`, `…placeholder…`);
  - a base URL other than `https://HOST[:PORT]`;
  - an auth mode other than `tailnet_node`, or a non-empty credential path
    (ADR 0010);
  - a profile or topic ref outside the `study.v1` pattern, more than 32
    topics, or a repeated topic;
  - a timeout or bound out of range, or a request timeout shorter than the
    connect timeout.

  The result always has the production transport policy: no test CA, no plain
  HTTP, and an untrusted wall clock.

## Evidence and durability semantics

Non-voice entry and first touch presentation/reveal become durable events before
display. Voice-only first presentation and dependent reveal remain visibly
provisional, with no stream sequence or outbox entry. Exact-bound touch or a
rating atomically promotes the active item's prerequisites. A newly displayed
voice-rating successor stays provisional. Restart discards provisional markers
and opens a non-card resume gate; touch resume records evidence before display.

A rating atomically reserves its own slot, any prerequisite promotions and one
future correction slot. A touch successor needs its own presented slot; otherwise
it remains unseen behind the tracking-paused completion state. Undo resolves only
the reducer's current durable rating UUID, appends `rating_voided`, consumes the
reservation and leaves the successor visible. Acknowledgement does not release
Undo. The next accepted non-Undo intent, surface exit or replacement does.
Terminal group rejection preserves the rendered state, marks every constituent
rejected and removes Undo without fabricating a void. Rejected evidence is never
silently reissued or counted as cycle exhaustion.

The random client stream, sequences, UUIDs, immutable event payloads, initial
creation request, current sync request and successor creation request persist together. Retry and restart
reuse their exact identities. No pending event is evicted. Refill can evict only
acknowledged cards outside visible history and the Undo lease. If input races a
refill and pins all candidates, acknowledgements still commit and the refill is
explicitly deferred. Unconsumed items are requested again by their issued
position; the panel does not invent scheduling order.

`prepare_successor` serializes a durable creation request against the fresh
predecessor flush receipt. The server decides exhaustion. A successor received
while Undo is live is staged without replacing completion. A late Undo stays
against the completed predecessor: the prior identical sync is replayed first
to refresh its live state version, then the correction is sent in stream order.
Next activates the staged successor after pending predecessor work clears.

## Bounds and composition

`StudyBounds` validates explicit card count per retained current/staged session
(at most two sessions during a late-correction boundary), history count, shared byte count,
pending-event count (at most the contract's 64), prefetch low-water and maximum
offline retention. Defaults are host fixture values. Long-retained cache is
marked stale. Trusted-clock HTTP use additionally requires an explicit maximum
snapshot age and permitted skew; untrusted wall time never claims freshness.

Composition order is runtime, client, worker, reducer/store, renderer. Shutdown
cancels and destroys the worker before runtime cleanup and releases client
handles. Runtime initialization, budget errors and repeated shutdown have typed
outcomes. No feature owns `curl_global_init` or `curl_global_cleanup`.

A UI loop calls `session.prepare_create(profile_ref, topic_refs)` before its
first network request, submits that persisted request to
`worker.create_or_resume(request, trace_id)` and polls `take_completion()`.
It installs a successful session on the UI thread. A restart reuses the saved
creation UUID and scope; changing that pending scope fails closed. For
synchronization it calls `session.prepare_sync()`, submits the returned exact
request to `worker.sync()`, and applies the returned synchronization with
`session.accept_sync()`. At completion it may submit `prepare_successor()` through
the same create operation; it must keep syncing pending late corrections. `needs_sync()` avoids polling a
fully cached, acknowledged session; it also detects the configured prefetch low-water.

## Fixture HTTPS server

```sh
python3 vocabulary_panel/tools/fixture_study_server.py \
  --certificate .build/sim/fixture-ca.pem \
  --private-key .build/sim/fixture-key.pem \
  --state-directory .build/sim/fixture-study-state --port 9443
```

The server binds loopback only, requires an explicit owner-only private key and
persists bounded synthetic state. It exposes the fixture profile `fixture-default`
and sorted topics `fixture-systems`, `fixture-words`. Its schema-v2 fixture state
supports idempotent create/sync, terminal suffix rejection, restart, flush
receipts, 16 bounded synthetic cycles, server-authoritative exhaustion and late
predecessor corrections. It is not Lexinet's scheduler or authorization service.
`POST /fixture/control` accepts `offline`, `terminal_rejection` and `delay_ms`.

The client still verifies peer and hostname. The test CA is accepted only for
`https://127.0.0.1:<port>` or, with the explicit simulation-only
`allow_qemu_host_test_ca` flag, `https://10.0.2.2:<port>`. Production HTTPS/Tailnet
and credential policy remain unchanged. CA/private-key/state files are ignored
build products and must never be committed.

## Live Lexinet contract

`make verify-lexinet-live` drives this client and the durable reducer against
a real Lexinet built from the pinned commit (fetched from GitHub) or from an
explicit `BHP_LEXINET_SOURCE_DIR=/absolute/path/to/lexinet` checkout. `scripts/lexinet/live_contract.py` owns the disposable
PostgreSQL 16 cluster, the loopback bearer and a synthetic corpus under ignored
`.build/lexinet/`, and deletes the secrets and database at teardown. The runner
defaults to a JSON dry run. `vocabulary_panel_study_live_lexinet_contract_client`
prints one JSON report per scenario and is never part of default CTest. The
[validation record](../docs/testing/lexinet-live-contract-validation.md) lists the
scenarios. Results are `simulation_not_hardware_evidence`; Tailnet, certificate
and FRDM evidence stay separate gates.

## Verification

`make verify` reaches the vocabulary/runtime HTTP tests through the host-dev
CMake configuration. Focused commands:

```sh
cmake --preset host-dev
cmake --build --preset host-dev
ctest --preset host-dev --output-on-failure -R 'http_runtime|vocabulary_panel'
```

Tests cover the reducer semantics and bounds, in-flight identity restart,
owner-only file locking, corruption, failed commits, five actual process deaths
(open/write/fsync/rename/directory-fsync), strict contract cases, verified TLS,
CA/hostname refusal, redirects, response limits, cancellation and worker joins.
The C++ HTTPS roundtrip test starts the persistent Python fixture, exercises
rating/Undo, successor replay and late corrections, restarts both peers, and
kills the client after initial creation or synchronization commits on the server
but before local installation or acknowledgement to prove exact replay recovery.
Fixture evidence is not a run against the external
Lexinet/PostgreSQL deployment; that cross-repository evidence remains separate.

Related: [subordinate LVGL surface](gui/lvgl/README.md),
[flash-card semantics](../docs/behavior/flash-card-ux.md), and
[shared HTTP runtime](../shared/http_runtime/README.md).
