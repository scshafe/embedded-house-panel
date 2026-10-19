# Live Lexinet study.v1 contract validation

The panel's production `study.v1` client (`HttpLexinetClient`) and durable
reducer (`FlashCardSession` with `FileStudyStore`) ran against a real Lexinet
built from the pinned checkout, not the fixture server. See
[pre-board plan Step 6](../../.claude/plans/2026-09-25-pre-board-completion.md)
and the [Lexinet consumer contract](../interfaces/lexinet-contract.md).

- Lexinet `ce42272df315f357e10998dd48e9ffd1a935c2ff`, built with `go version go1.26.5 linux/amd64`.
- postgres (PostgreSQL) 16.13 (Ubuntu 16.13-0ubuntu0.24.04.1): a disposable cluster under ignored `.build/lexinet/`.
- Loopback `study.v1` with a disposable bearer. The bearer and the database are
  deleted at teardown. The bearer never reaches panel configuration, images or
  the virtual panel (Lexinet decision 0003).
- A synthetic corpus of 3 cards, seeded through Lexinet's JSON CLI.
- Evidence: `simulation_not_hardware_evidence` (`real_lexinet_loopback_disposable_postgres`), run on Linux x86_64
  on 2026-09-26. This is not Tailnet, TLS-certificate or FRDM evidence.

## Scenarios

| Scenario | Result | Checks |
|---|---|---|
| `create_resume` | pass | 9/9 |
| `study_and_sync` | pass | 12/12 |
| `auth_failure` | pass | 7/7 |
| `offline_queue` | pass | 7/7 |
| `recover` | pass | 8/8 |
| `crash_after_send` | pass | 6/6 |
| `recover` | pass | 8/8 |
| `resume_after_server_restart` | pass | 9/9 |
| `exhaust_and_successor` | pass | 12/12 |
| `atomic_rejection` | pass | 6/6 |

What the scenarios prove, in order:

1. Create and resume. Replaying the creation request returns the same session.
   A new creation ID for the same active scope resumes it. Changing the body
   under the same ID is rejected with `IDEMPOTENCY_CONFLICT`.
2. Study and sync. Every event is accepted. An identical replay returns the same
   receipts and flush receipt. A changed body under the same `sync_id` is
   rejected with `IDEMPOTENCY_CONFLICT`, and a stale `expected_state_version`
   is rejected with `STUDY_STATE_STALE`.
3. Authentication failure. A wrong bearer returns non-retryable `UNAUTHORIZED`,
   and the queued events stay in the durable outbox.
4. Offline, then recovery. With Lexinet stopped, a restarted client reuses the
   same durable `sync_id`, and the retryable transport failure keeps the
   outbox. After Lexinet restarts, the outbox delivers once and the delivery
   state reaches `saved`.
5. Crash after send. The client exits after Lexinet commits but before it
   accepts the response. On restart it resends the same request, and Lexinet
   replays a byte-identical flush receipt.
6. Server restart. After Lexinet restarts on the same database, the session
   resumes and a touch Undo (`rating_voided`) is accepted.
7. Exhaustion and successor. Lexinet ends the cycle, and the successor links its
   predecessor. Lexinet's CLI confirms the link and that the predecessor is no
   longer active.
8. Atomic rejection. In a two-event suffix, one event names a content revision
   Lexinet never issued. Both events receive `terminal_rejected` with the same
   first code (`STUDY_CONTENT_REVISION_MISMATCH`). No canonical event is
   ingested, both stream positions are consumed, and the replay is identical.

## Reproduce

```sh
make verify-lexinet-live
```

Without `BHP_LEXINET_SOURCE_DIR`, the runner fetches the pinned Lexinet commit
from GitHub into `.build/lexinet/source/`; pass
`BHP_LEXINET_SOURCE_DIR=/absolute/path/to/lexinet` to test another checkout.
The report's `lexinet.source` and `lexinet.matches_contract_pin` say which was
used. The target builds `vocabulary_panel_study_live_lexinet_contract_client` in the
`host-dev` tree. It then runs `scripts/lexinet/live_contract.py`, which builds
Lexinet with the local Go toolchain and starts a disposable PostgreSQL 16
cluster. The machine-readable report is written to
`.build/lexinet/live-contract/report.json`. The [recorded report](lexinet-live-contract-validation.json)
is a verbatim copy of this run's report.
