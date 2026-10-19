# Plan: The Vocabulary App End to End on the Board

<!-- @canonical vocabulary-study-on-the-board-plan -->

**Status (2026-10-18):** In progress (issue #21, phase 1). Decision 1 below
takes the recommended option; the owner may change it before Step 3 merges.
Steps 1–5 need no board and no live Lexinet. Step 6 waits for the Lexinet
study listener (scshafe/lexinet#3) and the owner.

**Goal:** on the FRDM board, the vocabulary surface creates or resumes a
`study.v1` session against the live Lexinet over the tailnet, syncs a rating,
and shows that the rating survives a network drop (the outbox) and a restart.

**Fixed by the owner (issue #21):**
- The board's tailnet node carries exactly one tag, `tag:house-panel`.
- The study listener is `https://lexinet-study.<tailnet>.ts.net` (port 443,
  native TLS), with capability `scshafe.github.io/cap/lexinet-study-v1`, the
  operations create_or_resume and sync, and the profile ref `cole`.
- The panel stores no study credential (`BHP_VOCABULARY_PANEL_CREDENTIAL_FILE`
  stays empty; ADR 0010 `tailnet_node`).

**Out of scope:**
- the display cutover: making a physical display backend the production
  `bhp-panel-ui` path and enabling `S75bhp-panel-ui`;
- `S50tailscaled` in the Buildroot image (Step 5 only writes down the
  vendor-image procedure);
- the refresh-token and upload work in issue #23 (voice-lab#86 will post the
  panel-side requirements there).

## Starting point

- **`bhp-panel-ui` has two runtimes.**
  - **The production path** (`panel_ui/src/panel_ui_main.cpp`) runs
    `PanelUiService` with the `HeadlessRenderer` and the
    `FailDarkLifecycleDisplayCommitBackend`. It never lights the panel, and
    `S75bhp-panel-ui` refuses to enable it until an evidence-backed physical
    display backend replaces fail-dark. The image's `panel-ui.env.example.in`
    reserves the study keys "for the same display cutover".
  - **`--simulation-drm`** (`panel_ui/src/simulation_drm_main.cpp`) runs the
    simulator's `PanelSimulation` scene on DRM page-flip. This is what drew
    the panel in the FRDM demo
    ([demo plan](2026-10-18-frdm-panel-ui-demo.md)).
- **The live client already runs in the simulator.** With
  `BHP_BUILD_HTTP_LEXINET_CLIENT=ON`, `--lexinet-config FILE` (a simulator
  JSON) makes `PanelSimulation` construct `HttpRuntime`, `HttpLexinetClient`
  and `StudyWorker`. `panel_simulation_https_test.py` covers it against the
  fixture HTTPS server.
- **The network loop lives in the simulator.** Create or resume, the
  successor, outbox sync, 1 s pacing, the terminal stop and the lifecycle
  cancel are in `PanelSimulation::pump_study`
  (`simulator/src/panel_simulation.cpp`), not in `vocabulary_panel`.
- **Nothing reads `config/vocabulary-panel.env.example`.** The simulator
  JSON hard-codes the budgets (1000 ms, 3000 ms, 1 MiB, 7 days).
- **The FRDM bundle builds the HTTPS client OFF**
  (`scripts/deploy/frdm_panel_demo.sh`). The vendor sysroot has OpenSSL but
  no libcurl development files. The board has `libcurl.so.4`, `libssl.so.3`
  and `/etc/ssl/certs/ca-certificates.crt` (read over ssh on 2026-10-18,
  during the weather plan's source check).

## Decision

1. **Where the live study client runs on the board in phase 1.**
   - **A (recommended, taken):** `bhp-panel-ui --simulation-drm`, the runtime
     that draws, configured from the `BHP_VOCABULARY_PANEL_*` keys with
     `--study-config PATH`. The Step 6 run is a development record like the
     demo (`demo_not_gate_evidence`), not production evidence. Production
     enablement waits for the display cutover, a separate decision.
   - **B:** do the display cutover first (an ADR on DRM commit evidence, the
     LVGL scene in the production path, S75), then wire the client there.
     This is the right end state, but it is a larger change and delays phase 1.
   - **C:** wire the client into the headless production path only. It would
     sync, but it would never show a card or take a rating, so it proves
     nothing end to end.
   - Steps 1, 2, 4 and 5 are the same under all three options.

## Steps

- [x] **Step 1: Move the network loop into `vocabulary_panel`.** (2026-10-18)
  - Add `StudySynchronizer` (`@canonical study_synchronizer`) in
    `vocabulary_panel`. It drives one `FlashCardSession` through one
    `StudyWorker`, with the same rules as `pump_study`:
    - one operation in flight;
    - 1 s pacing;
    - a request only while the caller says the study surface is active (study
      surface, day mode, presence not absent);
    - create when there is no session, one successor per `refill_required`
      session, and sync while the outbox is pending;
    - a non-retryable error stops it until reset;
    - `cancel()` drops the in-flight completion.
  - `PanelSimulation` uses it, with no behavior change.
  - Tests in `vocabulary_panel/tests/study_synchronizer_test.cpp`, with a fake
    `LexinetClient` and a fake clock, cover each rule above.
  - Done: `vocabulary_panel/src/study_synchronizer.cpp`, and `PanelSimulation::pump_study`
    now only computes the gate and re-projects. `panel_simulation_https_resume` passes unchanged.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'study_synchronizer|panel_simulation'` exit 0;
    - `make verify` exit 0.
- [x] **Step 2: Read the study configuration.** (2026-10-18)
  - Add `read_study_client_configuration(path)` in `vocabulary_panel`. It reads
    the `BHP_VOCABULARY_PANEL_*` lines of an environment file (other prefixes,
    comments and blank lines are skipped, so the image's `panel-ui.env` works
    too) into `HttpLexinetConfig` plus the create request.
  - It fails with a structured error code naming the key when:
    - a key is unknown, duplicated or missing;
    - the URL is not `https://` with a host, or carries a path, query or user
      information;
    - a value is still a placeholder (`replace`, `placeholder.example`);
    - the auth mode is not `tailnet_node`, or the credential path is not empty;
    - a timeout or bound is zero or out of range, or the request timeout is
      shorter than the connect timeout;
    - the profile ref or a topic slug fails the `study.v1` bounds.
  - Tests in `vocabulary_panel/tests/study_client_configuration_test.cpp`,
    including `config/vocabulary-panel.env.example` itself (rejected for its
    placeholders) and a filled-in copy (accepted).
  - Done: `vocabulary_panel/src/study_client_configuration.cpp`, in
    `vocabulary_panel_core`, so it builds without libcurl.
  - Verify:
    - `ctest --test-dir .build/host-dev -R study_client_configuration` exit 0;
    - `make verify` exit 0.
- [x] **Step 3: Wire it into `bhp-panel-ui` (decision 1 A).** (2026-10-18)
  - `--study-config PATH` (with the existing `--state-directory`) builds the
    client, the worker and the synchronizer from Step 2 in `--simulation-drm`.
    `--lexinet-config` stays for the simulator's fixture-CA runs, and the two
    are mutually exclusive.
  - The renderer binary (`bhp-panel-render`) accepts the same `--study-config`
    plus a test-only `--study-test-ca-file`, which `bhp-panel-ui` does not have.
  - Extend `simulator/tests/panel_simulation_https_test.py` with a run through
    `--study-config`:
    - create a session against the fixture HTTPS server;
    - stop the server, rate and see it queued;
    - restart the server and the panel, and see the session restored and the
      queued rating saved exactly once.
    - (The fixture session holds one card, so a second rating would need a
      successor; the existing JSON runs already cover a saved rating.)
  - Docs:
    - `vocabulary_panel/README.md`, `panel_ui/README.md` (the two runtimes);
    - `config/vocabulary-panel.env.example`, which names `--study-config`;
    - `docs/interfaces/lexinet-contract.md`, "Panel delivery boundary".
  - Done: `PanelSimulation::attach_study_client`; `--study-config` in
    `simulation_drm_main.cpp` and `bhp-panel-render`. The test's CTest limit
    is now 60 s (it takes 17 s on the PC). `bhp-panel-ui` builds with
    `BHP_BUILD_PANEL_DRM` and `BHP_BUILD_HTTP_LEXINET_CLIENT` on.
  - Verify:
    - `ctest --test-dir .build/host-dev -R panel_simulation_https` exit 0;
    - `make verify` exit 0.
- [ ] **Step 4: Build the FRDM bundle with the HTTPS client.**
  - Add the pinned Debian trixie arm64 `libcurl4t64` and
    `libcurl4-openssl-dev` `.deb`s, with SHA-256s, to
    `scripts/build/frdm_vendor_sysroot.sh`.
  - `frdm_panel_demo.sh build` turns `BHP_BUILD_HTTP_LEXINET_CLIENT` on and
    allows `libcurl.so.4` and `libssl.so.3` in its library check.
  - `frdm_panel_demo.sh start` takes an optional `--study-config` file and
    copies it into the run directory with mode 0640.
  - Verify:
    - `python3 -m unittest discover -s scripts/deploy/tests -p test_*.py` exit 0;
    - `scripts/deploy/frdm_panel_demo.sh build --execute` exit 0, with the library check passing for `libcurl.so.4`.
- [ ] **Step 5: Write down the board's tailnet procedure.**
  - In `docs/operations/frdm-bringup.md`, add a "Tailnet identity" section
    for the owner. It covers:
    - fetching the ADR 0010 Tailscale `1.102.2` arm64 archive and checking its
      pinned SHA-256 on the PC;
    - where `tailscaled` and its state live on the vendor image (writing
      outside `/tmp` is owner-gated);
    - the exact `tailscale up` command with `--advertise-tags=tag:house-panel`
      and an auth key read from a file, never an argument;
    - how to check the node has exactly that one tag;
    - how to undo it.
  - Every step there is the owner's (tailnet enrollment and admin settings are
    `owner_gated`); the agent runs none of it.
  - Verify:
    - `make verify` exit 0.
- [ ] **Step 6: Check it on the board against the live Lexinet.**
  - Needs scshafe/lexinet#3 deployed, the board enrolled (Step 5, by the
    owner), and the owner's go-ahead before any board action.
  - Run the Step 4 bundle with a `vocabulary-panel.env` for the live listener
    and profile `cole`.
    - Create or resume a session.
    - Rate a card and see it saved.
    - Drop the board's network, rate again and see it queued; restore the
      network and see it saved.
    - Stop and restart the UI, and see the same session restored.
  - Verify:
    - the record under `hardware_tests/results/frdm/study-live-<UTC>/` shows each check with the Lexinet request IDs and no credential;
    - the owner's confirmation that Lexinet logged a tagged-node success for `tag:house-panel`.

## Risks

- **A shared clock.** `FlashCardSession` takes clock trust from the
  supervisor's solar state, as the simulator does. Nobody has measured how
  the board sets its wall clock or how far it drifts, so freshness stays
  monotonic (`clock_trusted=false` in the client).
- **Long requests.** The 1000/3000 ms budgets came from the simulator.
  Tailnet DERP relays may need more; Step 6 records the observed times, and a
  change is a configuration edit, not a code change.
- **Library drift.** The cross build links against Debian trixie's libcurl,
  and the board's is the vendor Yocto one. The library check covers the
  soname and the glibc ceiling, not the libcurl symbol versions; Step 6 is
  the real check.
