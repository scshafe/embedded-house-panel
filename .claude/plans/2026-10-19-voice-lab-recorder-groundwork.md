# Plan: Groundwork for the voice-lab Recorder App

<!-- @canonical voice-lab-recorder-groundwork-plan -->

**Status (2026-10-19):** In progress (issue #23, phase 3, the parts that do
not need voice-lab's wire format). The decisions below are the agent's, taken
where issue #23 and voice-lab's D-056 leave a choice; the owner may change
them before the step that depends on them merges.

**Goal:** everything the recorder app needs that does not depend on voice-lab's
`contracts/corpus-client.v1/` bundle: the three design changes issue #23 asks
for first, an HTTP client that can `GET` with `If-None-Match` and `PUT` with
`Content-Range` under longer budgets, the threat model for the stored refresh
token, the Pocket ID device-code sign-in, and the recorder's screens on
fixture data, recording through the media broker's `voice_take` purpose.

**Fixed by the owner (issue #23) and by voice-lab (D-056, posted on #23):**
- The same pattern as the vocabulary app: a cached catalog with its `ETag`,
  "Continue Missing", record with a live level meter, review, retake and
  reject, and a durable outbox that uploads each take by voice-lab's
  resumable intake protocol and then sends decisions with stable request IDs.
- Sign-in is Pocket ID's device-code grant through the public client
  `voice-lab-panel`; the refresh token sits in a `0600` file owned by the
  uploader's user.
- Pocket ID v2.13 rules: the refresh token rotates on every refresh with no
  grace, and reusing a rotated token revokes the family; show
  `verification_uri` and the user code, never `verification_uri_complete`;
  enforce `expires_in` locally; send no `resource` parameter; `aud` is an
  array; access tokens last 15 minutes.
- Capture only through the media broker's `voice_take` purpose
  ([voice-take capture plan](2026-10-19-voice-take-capture.md)).
- Do not build the wire format before the bundle lands.

**Out of scope (waits for the bundle, voice-lab D-056 step 5):**
- the voice-lab contract pin and its fixtures, the take package
  (`manifest.json`, `qc.json`, `READY`, `continuity.json`), the intake and
  decision requests, the uploader process, and a
  `make verify-voice-lab-live`;
- anything on the tailnet: the board is not enrolled yet.

## Starting point

- **Adding an app touches six places.** `PanelIntentRegistry`'s fixed
  15-entry array (`panel_ui/src/panel_intent_registry.cpp`), the implemented
  surface list (`simulator/src/application_catalog_configuration.cpp`), the
  surface map, routes and scenario enums in `simulator/src/panel_simulation.cpp`
  and `simulator/scenario.schema.json`, the `surface_id ==` branches in
  `panel_ui/src/lvgl_panel_display.cpp`, and the hand-written file and test
  lists in `scripts/verification/verify_host_cpp.sh`.
  `shared/spoken_command_catalog/catalog.py` parses the registry's source text,
  so moving the definitions must keep that parse working or replace it.
- **The outbox is the study reducer.** `FlashCardSession` owns the queue and
  its receipts; the generic parts are `FileStudyStore` (owner-only directory,
  `flock`, temporary file, fsync, rename, directory fsync, sticky
  "uncertain"), `StudyWorker` (one in-flight request on `HttpRuntime`) and
  `StudySynchronizer` (fixed 1 s pacing, no backoff). There is no chunked or
  resumable upload.
- **HTTP is POST-only JSON.** `HttpLexinetClient` builds its own curl
  requests: `POST`, JSON in and out, a 1 MiB response cap, one deadline, three
  attempts. `shared/http_runtime` owns curl's global state, cancellation and
  workers but has no request API.
- **The contract pin is lexinet-shaped.** `scripts/lexinet/pinned_source.py`
  is generic apart from its constants; the exporter and
  `contract-provenance.json` assume lexinet's `contracts/<id>/manifest.json`
  layout.

## Decisions

1. **The descriptor is compiled in (taken; issue #23 says no plugin loader).**
   One `PanelAppDescriptor` per app: its ID, surface, intents (with their
   effect class and touch, voice and fixture permissions), renderer factory,
   scenario state names, and its routes. The registry, the simulator and the
   renderer read the list of descriptors. The runtime catalog
   (`applications.json`) keeps its role: order, titles, icons and enabling.
2. **One request API in `shared/http_runtime` (taken).** Methods `GET`,
   `POST` and `PUT`; a fixed set of request headers (`Authorization`,
   `Content-Type`, `Content-Range`, `If-None-Match`, `X-Request-ID`); a body
   from memory or from a byte range of an open file; a per-request budget
   with a ceiling of 600 s and a response cap that stays 1 MiB by default.
   Redirects stay refused and TLS verification stays on. The lexinet client
   moves onto it with no behaviour change.
3. **What moves to `shared/` (taken).** The durable file commit (from
   `FileStudyStore`), a bounded exponential backoff, and a resumable chunked
   upload driver that resumes from the server's reported offset. The study
   reducer, its receipts, and each domain's credentials and retry policy stay
   in their domains.
4. **The refresh token gets its own ADR (taken).** ADR 0010 keeps the panel
   free of any lexinet credential. The voice-lab refresh token is a new,
   narrower exception, so it is a new ADR, not an edit of 0010.

## Steps

- [x] **Step 1: One app descriptor.** (2026-10-19)
  - Add `PanelAppDescriptor` and the compiled-in list (`home`, `study`,
    `agenda`) in `panel_ui`; `PanelIntentRegistry`, the simulator's surface
    list, surface map, routes and scenario fields, and the renderer's surface
    dispatch read it.
  - Keep `catalog.py`'s intent parse working against the new source, with a
    test that fails if it finds a different intent set.
  - No behaviour change: every golden stays byte-identical, and the registry
    test's 15 intents are unchanged.
  - Update `panel_ui/README.md`'s "new functional app" recipe.
  - Done: `panel_ui/src/panel_app_descriptors.cpp` holds the home, study and
    agenda descriptors. `PanelIntentRegistry`, the simulator's surface list,
    routes and scenario fields read them, and `LvglPanelDisplay` dispatches
    through `surface_renderers()`. Every golden is unchanged (0 changed
    pixels). The registry still holds the same 15 intents, now in descriptor
    order. New tests: `panel_ui_panel_app_descriptor_test`,
    `scenario_schema_descriptor_test`, and a catalog test that pins the voice
    catalog's text parse to those 15 IDs (a comment in the new file first
    broke it).
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'panel_intent_registry|panel_application_grid|panel_simulation|panel_display'` exit 0;
    - `make verify` exit 0 with the golden comparison unchanged.
- [x] **Step 2: The HTTP request API.** (2026-10-19)
  - `HttpRequest` and `HttpResponse` in `shared/http_runtime` (Decision 2),
    with `ETag` and `Content-Range` handling and a file-range body.
  - `HttpLexinetClient` uses it; its tests pass unchanged.
  - Tests against a local HTTPS fixture server: `GET` with `If-None-Match`
    answered `304`, a chunked `PUT` with `Content-Range` that the server
    verifies byte for byte, a 600 s budget accepted and 601 s refused, the
    response cap, a redirect refused, and cancellation mid-upload.
  - Done: `shared/http_runtime/include/bhp/http_runtime/http_request.h` and
    `http_request_test`. The test server is plain HTTP on loopback; TLS
    verification is covered by the lexinet HTTPS tests, which now go through
    the same `perform()`. "A redirect refused" means it comes back as a 3xx
    status and is never followed; the lexinet client still rejects it.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'http_runtime|http_lexinet'` exit 0;
    - `make verify` exit 0.
- [x] **Step 3: The shared durable store and outbox mechanics.** (2026-10-19)
  - `shared/durable_store`: the atomic file commit from `FileStudyStore`
    (owner-only directory, lock, fsync, rename, directory fsync, sticky
    uncertainty); `FileStudyStore` uses it with its file names and error
    codes unchanged.
  - A bounded exponential backoff with injected clock and randomness.
  - A resumable chunked upload driver: given a file, a chunk size and ports
    for "ask the server's offset" and "put this range", it resumes from the
    server's offset, never from local belief, and stops on a structured
    refusal.
  - Update the ownership registry row for the outbox.
  - Done: `shared/durable_store` (`DurableFile`, `Backoff`,
    `ResumableUpload`). `FileStudyStore` wraps `DurableFile`; its file names,
    error codes and crash test are unchanged. The ownership registry gains a
    row for the mechanics, and the vocabulary outbox row keeps its rules.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'durable_store|study_store|resumable_upload|backoff'` exit 0;
    - `make verify` exit 0.
- [x] **Step 4: One contract-pin tool.** (2026-10-19)
  - `scripts/contracts/pinned_source.py`: the fetch and the clean-checkout
    check, parameterised by repository, provenance file and cache directory;
    `scripts/lexinet/pinned_source.py` becomes a thin lexinet configuration.
  - Write down in `docs/interfaces/` how a second contract is pinned (the
    provenance shape, `--check`, and the live check), so voice-lab's bundle
    uses the same mechanism when it lands.
  - Done: `scripts/contracts/contract_pin.py` (`ContractPin`; error codes take
    the contract's name). `scripts/lexinet/pinned_source.py` keeps its API and
    `LEXINET_*` codes, and the study.v1 `--check` export is unchanged
    (9 fixtures, bundle `b90be0dd…`). Written down in
    `docs/interfaces/contract-pinning.md`. The module is named
    `contract_pin.py`, not `pinned_source.py`, because two modules with the
    same name on one import path shadow each other.
  - Verify:
    - `python3 -m unittest discover -s scripts/contracts/tests` exit 0;
    - `python3 -m unittest discover -s scripts/lexinet/tests` exit 0;
    - `make verify` exit 0.
- [x] **Step 5: ADR 0012, the refresh token on the panel.** (2026-10-19)
  - The threat model for the `0600` refresh-token file: whose subject it
    acts for, what it reaches (voice-lab's allow-list only), who can read it
    (the uploader's UID; never the UI, the broker or the supervisor), the
    rotation rules (single flight, write the new token with temporary file,
    fsync and rename before using the new access token), what reuse does
    (revokes the family; the panel signs in again), revocation (the owner
    removes `voice-lab-panel` from Pocket ID's authorized apps), and the
    15-minute access-token lifetime.
  - The device-code screen shows `verification_uri` and the user code only,
    and enforces `expires_in`.
  - Update the threat model document and ADR 0010's related list.
  - Done: `docs/decisions/0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md`,
    with a registry row, a new asset row and revocation step in
    `docs/security/threat-model.md`, and a reference from ADR 0010. If the
    durable write of a rotated token fails, the uploader signs in again rather
    than use the new access token.
  - Verify:
    - `make verify` exit 0.
- [ ] **Step 6: The recorder screens on fixture data.**
  - A `voice_lab` descriptor (Step 1): sign-in (the URI and user code),
    the prompt list with "Continue Missing", record with a live level meter,
    review, retake, reject, and an outbox status line.
  - Recording goes through `MediaLeaseClient` (voice-take capture plan Step
    6). In the simulator, `MediaBroker` runs in-process on the fake source,
    with the supervisor's `MediaWindowEndpoint` and a fake touch authority, so
    a recorder scenario records a deterministic take (moved here from the
    voice-take plan's Step 6).
  - Scenarios and goldens for each screen; the catalog is a fixture until the
    bundle defines it.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab|panel_simulation'` exit 0;
    - `make verify` exit 0 with the new goldens recorded.
- [x] **Step 7: Device-code sign-in and the token store.** (2026-10-19)
  - A device-code client for the uploader (RFC 8628 against Pocket ID's
    endpoints): poll at the given interval, stop at `expires_in`, no
    `resource` parameter, `aud` read as an array.
  - The token store: `0600`, owner-only directory, single-flight refresh,
    atomic replacement before use, and a structured "sign in again" state
    after `invalid_grant`.
  - Tests against a fake Pocket ID: approval, expiry while pending,
    rotation, a reused token, a crash between write and use, and that no
    token reaches a log, an argument or a URL.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/pocket_id_sign_in.h`
    (`discover`, `DeviceCodeSignIn`, `RefreshTokenStore`, `VoiceLabSession`).
    Endpoints come from the issuer's discovery document, which voice-lab's
    HOUSE-PANEL design describes for the owner's client. "A crash between write
    and use" is tested as a failed durable write, which signs out without
    using the new access token. The tokens travel only in form bodies; the code
    has no logging.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'device_code|token_store'` exit 0;
    - `make verify` exit 0.

## Risks

- **The bundle may change the shapes.** Steps 2, 3 and 7 follow voice-lab's
  posted requirements and its existing desktop intake (`PUT` with
  `Content-Range`, resume from the session status), not the bundle. If the
  bundle differs, the bundle wins and the driver changes.
- **The descriptor refactor touches every golden.** It must be
  byte-identical; any drift is a bug in the refactor, not a new golden.
- **The voice catalog parses C++ text.** If the parse cannot follow the
  descriptor, the generator reads a generated list instead, in the same PR.
