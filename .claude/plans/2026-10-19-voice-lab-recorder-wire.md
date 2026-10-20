# Plan: The voice-lab Recorder on voice-lab's Contract

<!-- @canonical voice-lab-recorder-wire-plan -->

**Status (2026-10-19):** In progress (issue #23, phase 3, the part that
waited for voice-lab's bundle). voice-lab published
`contracts/corpus-client.v1/` and its fixture server at `8f51656`
(voice-lab#108, #109; voice-lab#86 closed). The owner relayed the pin:
`manifest.json` SHA-256 `84503852…475b` and bundle digest `496302a4…45d1`.
It follows the [recorder groundwork plan](2026-10-19-voice-lab-recorder-groundwork.md),
whose Steps 1–7 are done.

**Goal:** the recorder records a take, packages it as voice-lab's
`corpus-take/v2`, uploads it by the resumable intake, and sends decisions,
all checked against the pinned bundle and, live, against voice-lab's own
fixture server.

**Fixed by voice-lab's bundle (PROTOCOL.md at the pin):**
- Sign-in, refresh and token storage as the groundwork plan's Step 7 built
  them.
- A take is five files: `manifest.json`, `qc.json`, `READY` (written last),
  `original.wav` and `continuity.json`. The take id is
  `take-<SHA-256 of original.wav>`, the profile `xvf3800-linear-4`.
- Every record the panel writes is canonical JSON with one final `\n`,
  byte for byte as the bundle's vectors give it.
- The intake: declare a session, `PUT` each file with `Content-Range` from
  the server's `received_bytes`, finalize, then poll until `accepted` or
  `refused`. Decisions use a stable `req-<32 hex>` and the take's
  `decision_revision`.
- The D-021 standing approval and its hash-chained ledger, with
  `upload-started` written before the first byte.

**Out of scope:** anything on the tailnet. The board is not enrolled yet, so
the board's first sign-in, its D-021 approval and a real upload wait for the
owner (Step 7).

## Decisions

1. **The panel keeps the examples, not the schemas (taken).** As for
   lexinet, the export copies the bundle's examples byte for byte with a
   provenance file, and `--check` verifies the whole bundle at the pin: each
   file's SHA-256 and size as `manifest.json` binds them, nothing unbound,
   and voice-lab's bundle digest.
2. **The live check runs voice-lab's own code (taken).** `make
   verify-voice-lab-live` runs `cole_voice_lab.contract_fixture` from the
   pinned checkout. It uses a venv of CPython 3.12 with the API image's
   hash-locked closure (`requirements/vault-linux.txt`) and serves with a
   throwaway TLS certificate. Like `make verify-lexinet-live`, it is never
   part of `make verify`.
3. **The uploader gets a kept take by a hand-off from the broker (taken;
   the voice-take plan's Decision 4 left it to this plan).** The broker stays
   the only owner of its store, and the uploader sees only takes the owner
   kept, as ADR 0012 requires.
   - The hand-off directory is owned by the uploader's UID, with a group that
     only the broker and the uploader belong to, mode `2770`. A take's
     directory there is `0770` and its files `0640`, so the uploader can read
     a take and remove it, and nothing else can enter.
   - On Keep, the UI asks the broker, over the lease protocol, to hand the
     take over. The broker copies `original.wav` and `capture.json` into
     `.incoming-<take id>`, renames it into place, and deletes its own copy.
   - The uploader packages the take into its outbox (Step 3) and removes the
     hand-off copy. A take that is rejected or retaken never leaves the
     broker.
4. **The UI talks to the uploader over its own socket (taken).** It follows
   ADR 0005's rules: exact-UID `SO_PEERCRED` both ways, newline-delimited JSON
   frames with closed key sets, and stale-frame refusal. Frames may reach
   64 KiB, because a catalog's prompts do not fit the media protocols' 4,096
   bytes.
   - The UI sends what the owner does: sign in, approve the PC, keep a take
     for a prompt, and read the status.
   - The uploader answers with its state: the device-code prompt (the URI and
     code only), the approval preview, the outbox, and the prompts with their
     status.
   - The UI holds no token and makes no request to voice-lab.

## Steps

- [x] **Step 1: The pin.** (2026-10-19)
  - `scripts/voice_lab/voice_lab_pin.py` (`ContractPin("VOICE_LAB", …)`),
    the exporter `scripts/voice_lab/export_corpus_client_v1_fixtures.py`
    with `--check`, and the provenance and examples under
    `voice_lab_recorder/tests/fixtures/corpus_client_v1/`.
  - An offline fixture test: the pin, every file against its provenance, the
    golden take's WAV rebuilt from its recipe, the records binding each
    other, the continuity rule in integer nanoseconds, and the canonical
    form against the vectors.
  - `docs/interfaces/voice-lab-contract.md`, with the pin and the repin
    commands.
  - Done: the export from the pinned commit gives the relayed manifest SHA-256
    and bundle digest, with 42 bundle files and 18 examples. Exporter tests
    against a synthetic bundle cover a changed, unbound or missing file,
    another contract, an invalid example without its refusal, and a stale
    export. The offline fixture test is part of `make verify`. The `--check`
    fetch needs the network, so it is not.
  - Verify:
    - `python3 scripts/voice_lab/export_corpus_client_v1_fixtures.py --check` exit 0;
    - `ctest --test-dir .build/host-dev -R 'corpus_client_v1'` exit 0;
    - `make verify` exit 0.
- [x] **Step 2: The live check: sign-in and the library.** (2026-10-19)
  - `scripts/voice_lab/live_contract.py` and `make verify-voice-lab-live`:
    the fixture server from the pinned checkout, as Decision 2 says.
  - A live client (`voice_lab_recorder_live_contract_client`) on the
    groundwork's sign-in code and `shared/http_runtime`:
    - device-code sign-in through the fake Pocket ID, with a pending poll first;
    - a refresh that rotates and is saved;
    - the rotated token reused, which revokes the family and signs out;
    - `GET` the catalog and the panel's library view, then a `304` with
      `If-None-Match`;
    - the library without `?profile=`, refused `403 client_scope`.
  - Done: three scenarios, `sign_in`, `library` and `reuse_revokes`, all
    passed against the fixture at the pin. The fixture's root is in the
    temporary directory because voice-lab's corpus store refuses its own
    files on btrfs (`os.listdir()` on an older directory descriptor misses new
    entries there). The report carries checks, counts and error codes, never a
    token.
  - Verify:
    - `python3 -m unittest discover -s scripts/voice_lab/tests` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [x] **Step 3: Canonical JSON and the take package.** (2026-10-19)
  - A canonical JSON writer that passes every vector, floats included.
  - The packager: from a broker take (its WAV and continuity log) and the
    prompt, catalog and device, the five files, written with `READY` last.
  - The golden take rebuilt byte for byte from the same inputs.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/canonical_json.h` and
    `take_package.h` (`write_take_package`, `inspect_wav`, `qc_metrics`,
    `xvf3800_capture_profile`).
    - nlohmann's `dump()` disagreed with Python's `repr` for 29 of 1,216
      floats (the switch to exponent form at 1e16, and digits that are not
      the shortest), so floats are formatted from `std::to_chars` by Python's
      rule.
    - QC is voice-lab's at the pin, on the primary channel: exact integer
      sums, the same thresholds and findings.
    - The golden take's `manifest.json`, `qc.json`, `READY` and
      `continuity.json` come out byte for byte from a broker take built from
      the examples.
    - The channel map, room and primary channel are inputs. The array reports
      none of them, and which channels are raw on the board is unmeasured.
      An unread serial, firmware string or setting is refused, never filled
      in.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'canonical_json|take_package'` exit 0;
    - `make verify` exit 0.
- [x] **Step 4: The intake and decisions.** (2026-10-19)
  - The intake client on `ResumableUpload`: declare, send from the server's
    offset, finalize, and poll the session.
  - Decisions with stable request IDs, polled to `applied` or `refused`, and
    the library and history with their `ETag`.
  - The live check sends the golden take, interrupts an upload and resumes
    it, keeps the take, and meets a `revision_conflict`.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/corpus_client.h`
    (`CorpusClient`, `intake_session_request`, `new_request_id`). A `401`
    refreshes the access token once (`VoiceLabSession::invalidate_access_token`)
    and asks again. The golden package's session request and the decision body
    are the bundle's examples byte for byte. Live, at the pin:
    - `intake_interrupted`: the link drops after two 16 KiB chunks, and the
      server holds 32,768 bytes;
    - `intake_resume`: a new process resumes from there, finalizes, and the
      PC's own validator and admission accept the packaged golden take, with
      its receipt;
    - `decide`: `kept` is applied, a replay of the same request answers the
      same outcome, the revision advances once, a stale revision is refused
      `revision_conflict`, and the prompt is `kept` in the panel's view.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_intake|voice_lab_decision'` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [x] **Step 5: The D-021 approval and the ledger.** (2026-10-19)
  - The approval record (endpoint, tailnet, node, tag, expiry), stored
    write-once at `0400` after its digest is reviewed, and its binding.
  - The hash-chained ledger, with `upload-started` before the first byte.
  - The node and tag check before each session, behind a port; the
    `tailscaled` side waits for enrollment.
  - The approval screen, where the owner reviews and confirms, is Step 7.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/home_host.h`
    (`home_host_approval`, `HomeHost::approve/load/entries/append`,
    `check_peer`), following voice-lab's `home_host.py` at the pin.
    - The example approval, binding and `upload-started` entry come out byte
      for byte, and the approval's digest is the one the example binding names.
    - Refused: more than 366 days, an endpoint outside the tailnet or in
      upper case, unsorted classes, a wrong reviewed digest, a replaced
      approval, a tampered or expired one, a completed upload without its
      receipt, and a ledger with a gap.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_approval|voice_lab_ledger'` exit 0;
    - `make verify` exit 0.
- [x] **Step 6: The uploader's outbox.** (2026-10-20)
  - The outbox the uploader runs: its refresh token (ADR 0012), the D-021
    directory and a durable queue. The process itself, `bhp-voice-lab-uploader`,
    comes with its socket in Step 7: it has nothing to do until the UI can
    send it a kept take.
  - The outbox worker, one take at a time. It packages a handed-off take,
    checks the approval and the node, writes `upload-started`, uploads, and
    waits for the session's outcome. It then writes `upload-completed` or
    `upload-refused` and, for an accepted take, sends `kept` with a stable
    request id.
  - It backs off on retryable answers, signs out on `invalid_grant`, and
    stops a take for good on a refusal. After a restart it resumes from the
    server's offset.
  - Tests against an in-memory voice-lab and a fake node resolver, and live:
    a handed-off golden take goes through the worker to `kept`.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/outbox.h`
    (`Outbox::open/keep/step/status`).
    - The queue is `outbox.json` in a DurableFile. A take moves through
      `queued`, `uploaded`, `accepted` and `kept`, or ends at `refused`.
    - `keep` builds the capture profile from the hand-off's device record and
      the board's settings. With the golden device record, that gives the
      golden manifest byte for byte. It removes the hand-off copy once the
      package and its record are durable.
    - Before each attempt it loads the approval, checks that it covers
      `capture`, and checks the node. It writes `upload-started` once per
      attempt in a process, and `upload-completed` with the receipt's
      digest (voice-lab's `record_sha256`), or `upload-refused`.
    - The `kept` request id is saved before the first send. A request that
      was lost is sent again under the same id (404 on its status), and a
      `revision_conflict` reads the history and decides again, at most twice.
    - Tests: blocked without an approval, on another node, or signed out;
      a restart that resumes without resending a byte; a refused take; a lost
      decision. Live: a new take goes through the outbox to `kept` in
      voice-lab's history.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_uploader'` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [x] **Step 7: The UI and the uploader (Decision 4), and the hand-off (Decision 3).** (2026-10-20)
  - `bhp-voice-lab-uploader`, under its own UID, running the outbox and
    serving the UI's socket.
  - The uploader protocol and the recorder screens on it:
    - sign-in (the URI and code);
    - the approval preview and its confirmation;
    - the prompts with "Continue Missing";
    - the outbox status line.
  - The broker's hand-off on Keep, through the lease protocol.
  - The simulator runs the uploader in process against a fake voice-lab.
  - Done in three PRs.
    - **Part 1 (the hand-off):** `hand_off` and `handed_off` in the lease
      protocol; `TakeStore::hand_off` and `MediaBroker::hand_off` with
      `--handoff-dir`; Keep in the recorder hands the take off. Until Step 8,
      the FRDM demo's broker has no hand-off directory, so a kept take stays
      in the broker's store there.
    - **Part 2 (the uploader):** `uploader_protocol.h` (with `UploaderClient`)
      and `uploader_service.h`, and `bhp-voice-lab-uploader`, which resolves
      the PC's node with `tailscale status --json`. `FrameChannel` gains a
      frame bound per channel; the media protocols keep 4,096 bytes. Live,
      the service goes from a device sign-in, through the approval by digest,
      to a missing prompt shown as kept.
    - **Part 3 (the screens):**
      - Two intents, `voice_lab.sign_in` and `voice_lab.approve`, touch
        only, with their effect-graph entries.
      - Two screens: Sign in, with the code screen now its own
        `signing_in` state, and Approve your PC, showing the endpoint, node,
        expiry and the record's digest.
      - The status line says why uploads wait.
      - The simulator projects the uploader's state onto the screens and
        sends its requests. After the broker's `handed_off`, it asks the
        uploader to keep the take for its prompt.
      - `bhp-panel-sim-drm` takes `--voice-lab-uploader-socket` and
        `--voice-lab-uploader-uid`.
      - The integration test plays the uploader over a socket pair through
        sign-in, approval, a recorded and kept take, the next missing
        prompt, and a lost uploader.
      - There is no in-process uploader: the real one is tested in
        `voice_lab_recorder` and live.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab|panel_simulation|media_broker'` exit 0;
    - `make verify` exit 0 with the new goldens recorded.
- [ ] **Step 8: The uploader in the FRDM demo.**
  - Its UID, the hand-off and token directories, and its process in
    `frdm_panel_demo_board.py`. Off the tailnet it reports voice-lab
    unreachable.
  - Verify:
    - `python3 -m unittest discover -s hardware_tests/tests` exit 0;
    - `make verify` exit 0.
- [ ] **Step 9: On the board (waits for the owner).**
  - The owner enrolls the panel, approves the first device code from a
    phone, and confirms the D-021 approval on the panel.
  - A spoken take is uploaded, accepted and kept.
  - Verify:
    - a board record under `hardware_tests/results/` of the accepted take;
    - the owner's confirmation that the take is in voice-lab's library.

## Risks

- **The fixture needs CPython 3.12.** The hash-locked closure has 3.12
  wheels only. The live check takes `--python`, or finds `python3.12`.
- **The bundle can move.** A repin is an explicit-checkout export and a
  reviewed diff of the examples, as for lexinet.
