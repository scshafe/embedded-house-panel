# Plan: Prove Personal Agenda and Birthday-Wish Feasibility

**Goal:** resolve the platform and trust-boundary unknowns that block a credible
production plan for Apple/Google calendar and contact data, local birthday-wish
recording/transcription, and Messages submission through a logged-in Mac
companion.

**Out of scope:** production provider synchronization; a production `panel_ui`;
real-recipient messaging; persistent panel audio; cloud transcription;
voice-only submission; arbitrary recipients; Buildroot enablement; meeting
alerts; waking the panel or Mac for calendar events; implementing the
conditional roadmap in the architecture document.

## Context

- The design authority is
  [`docs/architecture/personal-agenda-and-birthday-wishes.md`](../../docs/architecture/personal-agenda-and-birthday-wishes.md).
- The panel repository is the worktree containing this plan.
- The current graphical service, LVGL ownership, Unix-socket protocol, Buildroot
  package, user/group, and state directory are vocabulary-specific. Production
  work requires one neutral `panel_ui`; it must not create a second display
  process or add agenda state to `PanelPolicy`.
- The
  [application capability ownership registry](../../docs/architecture/application-capability-ownership.md)
  assigns the first neutral shell, shared HTTP runtime, media broker, and
  Buildroot migration to the Lexinet delivery plan. This feasibility plan may
  measure boundaries but may not create competing implementations.
- The future media broker must have purpose-scoped capture. Vocabulary command
  audio stays broker-local; a birthday-wish recording is a separate, visible,
  bounded purpose that may upload one consented artifact.
- The Lexinet plan first delivers the shared spoken-command catalog,
  pronunciation provenance, confusability analysis, active-set building, and
  backend projection.
  This feasibility plan reviews agenda command semantics only; later production
  work registers namespaced agenda intents in that catalog rather than creating
  a private phrase list.
- Use the macOS-system EventKit and Contacts providers as the initial path.
  Direct Google API adapters remain independent extension points and do not
  need to be production-implemented in this feasibility tranche.
- A signed per-user macOS application is required to test Calendar, Contacts,
  Automation, Shortcuts, logged-in-session, and background-item behavior
  honestly. A root daemon or unsigned command-line mock cannot answer those
  questions.
- The current host selects `/Library/Developer/CommandLineTools` and has Swift
  6.3.1, but not a selectable full Xcode installation. Step 2 must make full
  Xcode availability an explicit environment prerequisite before creating or
  signing the native app probe.
- ~~The current development host is macOS 26.5.2 build 25F84 with Messages 26.0
  build 1450.600.61.1.5.~~ Step 1 execution observes macOS 26.6.1 build 25G76
  with Messages 26.0 build 1450.700.71. Its Messages scripting dictionary accepts text and
  file inputs, but that observation does not prove unattended reliability,
  recipient resolution, attachment presentation, or delivery semantics.
- The Mac currently runs Tailscale 1.98.10, while the Buildroot baseline packages
  1.78.1. The Tailnet experiment can use the Mac's current Serve capabilities,
  but production compatibility requires a coordinated, reviewed panel pin.
- All live Messages tests require a separately approved self/test recipient.
  The executor must stop for explicit user approval immediately before the first
  actual submission. No friend is ever a feasibility-test target.
- Test artifacts use synthetic calendars, contacts, audio, and transcripts
  where the platform permits. Local evidence that necessarily touches personal
  provider data is redacted before any result enters Git.
- No secret, provider identifier, contact handle, name, calendar title, message
  body, audio, transcript, Shortcut name, Tailnet state, or TCC database content
  may enter Git, command arguments, URLs, logs, or diagnostic archives.
- This plan ends in an ADR. Production work begins only in a reviewed follow-up
  plan based on the measurements; a failed Messages result may select
  `DraftOnlyTransport` rather than block read-only agenda work.
- After the fixture harness in Step 2, Steps 3–6 are independent feasibility
  tracks. A failed Messages track does not prevent read-only provider or
  Tailnet evidence and does not authorize production work.

## Steps

- [ ] **Step 1: Freeze the feasibility contract and safety matrix**
  - Review and record the ~~ten product decisions~~ eleven product decisions in
    the architecture document, including provider priority, the still-pending
    household-timezone data path and configuration/projection ownership,
    birthday window and February 29 policy, voice-prepare versus
    touch-to-record/submit semantics, recording bound, retention,
    playback-gated audio acceptability, companion availability, compromised-UI
    authority, command-distinctiveness disposition, and companion repository
    boundary. `AGENDA-011` remains deliberately deferred here: record that
    neither timezone alternative is selected and carry the choice to roadmap
    Step 6 after the feasibility evidence and ADR. Prefer `Lexi, finish
    recording`; the requested `Lexi, end birthday wish` remains pending a
    reviewed context-separated acoustic exception because it is a near neighbor
    of `send birthday wish`.
  - Decision-review subpart status (2026-08-16): complete. The user accepted
    `AGENDA-001`–`AGENDA-010` and deliberately deferred `AGENDA-011` to roadmap
    Step 6 after feasibility evidence and its ADR. This does not complete this
    step or authorize a probe: the experiment README, outcome model, matrix,
    limits, redaction evidence, test-recipient class, maximum side effects, and
    manual stop-point review below remain roadmap Step 5 work.
  - Define stable experiment outcomes for every probe:
    `available`, `permission_denied`, `unsupported`,
    `failed_before_submission`, `submitted`, and `delivery_unknown`. Do not use
    `sent` or `delivered` for an automation return value.
  - Freeze a redacted test matrix covering macOS Calendar/Contacts permissions,
    source selection, Messages sign-in and sender identity, direct Apple Events,
    fixed Shortcuts, text, synthetic audio files, local transcription,
    app relaunch, login-item launch, locked user session, network loss, Mac
    sleep/wake, permission revocation, and OS restart.
  - Freeze limits for probe message count, self/test recipient, audio duration
    and size, staging retention, timeout, and the manual stop points before any
    external side effect.
  - Freeze the rule that audio submission is disabled without complete local
    playback, and that a voice recognition may open but cannot start the
    exportable recording purpose. A later `wish_recording` implementation may
    recognize stop only through a purpose-internal active command set containing
    `agenda.stop_wish_recording`; it cannot activate the general command grammar.
  - Output: `experiments/macos_personal_companion/README.md` containing the
    experiment contract, redaction rules, manual prerequisites, and matrix;
    no provider code or real submission yet.
  - Verify: human review confirms the product defaults, test recipient class,
    maximum side effects, redaction policy, and stop points; repository search
    finds no real identifier or content fixture.

- [ ] **Step 2: Build a signed fixture-first Mac companion probe**
  - Add an isolated native macOS probe under
    `experiments/macos_personal_companion/` with one stable bundle identity,
    Hardened Runtime, a visible onboarding/status window, and a per-user
    `SMAppService` login-item experiment.
  - Define fixture-first `CalendarProvider`, `ContactProvider`,
    `TranscriptionProvider`, and `MessageTransport` extension points. The default
    transport is `DraftOnlyTransport`; Apple Events and Shortcuts remain
    explicitly disabled until their test step.
  - Add the exact usage descriptions and entitlements required for the measured
    Calendar, Contacts, Apple Events, and localhost network probes. Do not add
    Accessibility, Full Disk Access, Contacts notes, arbitrary file access, or a
    root helper.
  - Expose a local-only bounded probe API that accepts synthetic fixture
    identities and returns structured content-free results. Reject arbitrary
    AppleScript, Shortcut names, recipient handles, file paths, calendar
    predicates, and shell strings.
  - Add deterministic fixtures and tests for provider normalization, partial
    birthdays, private/all-day/recurring meetings, opaque recipient routes,
    draft hashes, expiry, immutable route/sender/transport revisions, atomic
    single-use confirmation, and no-retry handoff state.
  - Compose fixture calendar and contact providers in every Apple/Google-shaped
    pairing and prove the aggregator, minimized snapshot, and UI contract do not
    change. This proves provider independence without implementing live Google
    OAuth in the feasibility tranche.
  - Output: signed probe target, fixture adapters, local-only API, unit tests,
    entitlement/permission inventory, and a reproducible build recipe.
  - Verify: a clean build and test run succeeds without requesting personal-data
    permissions or invoking Messages; code signing reports the expected stable
    identity and entitlements; the default transport cannot submit.

- [ ] **Step 3: Measure macOS-system Calendar and Contacts behavior**
  - Add logically read-only EventKit and Contacts.framework probe adapters.
    Fetch selected sources and a bounded time window on background queues; never
    write an event or contact.
  - Request permissions only from the visible signed app and record denied,
    granted, restricted, and revoked behavior. Verify that the login-item path
    uses the same intended code identity and does not hide a permission prompt.
  - Normalize only redacted counts and synthetic-shaped results. Prove partial
    birthday years, source/account selection, unified contacts, provider change
    notifications, recurrence expansion, all-day events, private-event
    projection, household-timezone boundaries, trusted-clock gating, same-day
    soft-stale redaction, hard expiry, and prior-day suppression.
  - If both iCloud-backed and Google-backed macOS accounts are locally
    available, exercise each without recording account labels or data. If not,
    mark that matrix cell `not_configured`; do not create or modify an account.
  - Measure behavior while the app is foregrounded, a login item, locked,
    relaunched, and permission-revoked.
  - Output: adapters kept inside the experiment, synthetic contract fixtures,
    and a content-free result table naming OS/app build and observed permission
    states.
  - Verify: read probes return bounded normalized data or a stable typed error;
    a code and network review finds no provider payload in logs or fixtures; no
    calendar/contact mutation API is reachable from the probe.

- [ ] **Step 4: Prove or reject Messages text and audio-file transports**
  - Stop and obtain explicit user approval for the exact self/test recipient,
    maximum test messages, marker text class, and synthetic audio file before
    enabling either live transport.
  - Provision recipient and sender identity locally in the Mac UI. Expose only
    an opaque route to the probe; never accept a handle from a remote request or
    select a recipient by display name. Give route, resolved destination, sender
    identity, and transport configuration immutable revisions in each prepared
    dispatch.
  - Implement isolated Shortcuts and direct Apple Event adapters without
    interpolating untrusted values into script or shell source. Use fixed
    allowlisted automation targets and structured data parameters.
  - Submit uniquely marked, non-sensitive text and a short synthetic AAC/M4A
    file to the approved self/test route. Record whether the result is a generic
    attachment, whether handoff is observable, and whether any delivery/read
    state is actually available.
  - Exercise foreground, login-item, locked, companion-relaunch, Messages
    relaunch, network loss, Mac sleep/wake, permission revocation, timeout, and
    OS restart cases within the approved message bound.
  - Never automatically retry after the adapter crosses its handoff boundary.
    Atomically consume the single-use confirmation and durably record
    `dispatch_started`, attempt reference, prepared-dispatch revision, and
    content hash before invoking automation. Race concurrent submissions and
    prove only one can invoke the adapter. Force a timeout and process crash at
    that boundary and prove restart converts the state to `delivery_unknown`
    without replay, requiring manual Messages reconciliation.
  - Change the provisioned route, sender, and transport after preview and prove
    submission fails before adapter invocation until a new prepared summary is
    reviewed. Treat every error after invocation begins as `delivery_unknown`
    unless the exact adapter proves a stronger acceptance signal.
  - Output: two isolated adapters, content-free OS-version result matrix, exact
    permission/onboarding procedure, and a recommendation for Apple Events,
    fixed Shortcuts, or `DraftOnlyTransport`.
  - Verify: the user manually confirms the bounded self/test transcript in
    Messages; the recorded result never claims delivery without evidence; every
    ambiguous post-handoff fault produces no automated retry; repository and
    process-list searches find no recipient, body, audio, or generated script
    source.

- [ ] **Step 5: Measure local transcription and attachment formats**
  - Use synthetic or separately consented, non-personal audio fixtures. Compare
    panel-friendly mono PCM/WAV input, any measured compressed alternative, and
    Mac-side AAC/M4A output for duration, size, decode reliability, and Messages
    attachment presentation.
  - Test Apple Speech only with on-device recognition required and supported.
    Make unsupported hardware/language a typed result, never an implicit network
    fallback.
  - If needed, test one isolated local Whisper-class adapter and measure install
    size, peak memory, transcription latency, accuracy on the bounded fixture,
    and whether packaging is justified for this household deployment.
  - Verify that a stop-phrase tail can be removed without truncating ordinary
    speech and that maximum duration/size and cancellation delete every staged
    artifact.
  - Prove the audio path exposes complete playback through a measured output
    before confirmation. When playback is missing or fails, prove audio
    submission is unavailable; duration or waveform alone does not satisfy the
    gate. A host output can validate the control flow but cannot enable target
    audio; leave it disabled until the actual panel playback path is measured.
  - Output: format/transcription matrix and a recommendation for
    `AppleOnDeviceSpeech`, `LocalWhisper`, or disabled text conversion; no raw
    fixture or transcript committed.
  - Verify: captured network traffic shows no STT egress; unsupported on-device
    speech fails closed; hashes prove every staging file is deleted after each
    terminal state; results name exact tool/model/OS versions without content.

- [ ] **Step 6: Prove the Tailnet capability boundary against fake submission**
  - Upgrade and pin a supported Tailscale experiment version before depending
    on Serve app-capability forwarding; do not silently mutate the production
    image pin in this step.
  - Run the companion API on localhost only behind private Tailscale Serve
    HTTPS, with Funnel disabled and exact Tailnet grants for the dedicated panel
    test identity.
  - Add a fixture `personal_context_client` with a temporary Ed25519 device key
    outside Git. Require a companion-issued single-use challenge and signature
    over canonical method/path, requested capability, body hash, and key ID for
    every data or mutation request; persist challenge consumption and map the
    public key independently to allowed capabilities.
  - Define separate `agenda.read`, `birthday_wishes.prepare`,
    `birthday_wishes.transcribe`, and `birthday_wishes.submit` app capabilities.
    Use only `DraftOnlyTransport` for remote authorization tests.
  - Bind prepare/confirmation state to panel principal, current occurrence and
    household date, fresh snapshot, opaque recipient, immutable route/resolved
    destination/sender/transport revisions, content hash, media type, nonce, and
    expiry. Reject replay, changed content, stale occurrence, expired
    confirmation, oversized uploads, unknown content type, and wrong capability
    with stable errors.
  - Atomically enforce one submission attempt per provisioned route per
    household date in the fake transport. A `delivery_unknown` result remains
    locked until explicit local reconciliation.
  - Test the tagged panel, another tagged node, an untagged user node, LAN
    bypass, direct localhost, spoofed incoming headers, missing/changed
    signatures, reused challenges, concurrent confirmation, and revoked grants.
    A same-user Mac process with forged Serve headers but no panel signature
    must receive denial. If Serve is unsuitable, compare a direct Tailnet
    listener plus LocalAPI identity, retaining mandatory signed requests.
  - Output: redacted grants, fixture signer, fake-only end-to-end harness,
    negative authorization tests, version evidence, key-pairing/revocation
    procedure, and a Serve-versus-direct-listener result.
  - Verify: only exact signed panel/capability combinations succeed; forged
    direct-loopback headers fail; one challenge and confirmation produce at most
    one fake handoff under concurrency and restart; no remote test can reach a
    real Messages adapter; the backend is unreachable directly from LAN/tailnet;
    Funnel is absent; packet/log review contains no personal data or secrets.

- [ ] **Step 7: Publish the decision and stop before production work**
  - ~~Add ADR 0004 selecting the initial calendar/contact providers, Messages
    transport or draft-only fallback, audio attachment contract, local
    transcription adapter, Mac login/background assumptions, Tailnet
    authorization path, online/offline draft policy, and companion repository
    boundary.~~
  - Add a decision record whose four-digit prefix is chosen by scanning
    `docs/decisions/` on the current target branch when the record is created,
    selecting the initial calendar/contact providers, Messages
    transport or draft-only fallback, audio attachment contract, local
    transcription adapter, Mac login/background assumptions, Tailnet
    authorization path, online/offline draft policy, and companion repository
    boundary.
  - Record audio as disabled without complete playback and record the accepted
    fact that compromised `panel_ui` can exercise allowlisted message authority;
    name a separate physical confirmation input as the stronger future boundary
    if that limitation is unacceptable.
  - Record every unproven or failed matrix cell. Distinguish a platform
    limitation from a test not run, and state the exact macOS/Messages/Tailscale
    versions to requalify after upgrades.
  - Amend the architecture document where measured facts differ. Do not erase
    the rejected alternatives or weaken the touch-confirmation and no-retry
    boundaries to make a transport appear successful.
  - ~~Create a reviewed follow-up production plan beginning with the neutral
    `panel_ui`/protocol prerequisite. Coordinate it with the amended Lexinet
    Steps 7–9 so there is one shell, one media broker, one HTTP runtime owner,
    and separate vocabulary and message persistence semantics.~~
  - Hand the measured ADR and its explicit prerequisites to Step 6 of the
    end-to-end roadmap. That step alone owns creation and review of the
    follow-up production plan; do not draft or implement it in this feasibility
    plan. The handoff must require agenda intents, active command sets, and
    backend projections to extend the one shared spoken-command capability.
  - ~~Output: `docs/decisions/0004-select-personal-agenda-companion-boundary.md`,
    updated content-free research results, and a new production plan or an
    explicit read-only/draft-only direction.~~
  - ~~Output:
    `docs/decisions/NNNN-select-personal-agenda-companion-boundary.md` using the
    selected free prefix, updated content-free research results, and a new
    production plan or an explicit read-only/draft-only direction. Immediately
    before merge, re-scan the destination branch and renumber the file and
    references if that prefix now collides.~~
  - Output:
    `docs/decisions/NNNN-select-personal-agenda-companion-boundary.md` using the
    selected free prefix, updated content-free research results, an explicit
    read-only/draft-only direction where applicable, and the prerequisites for
    the roadmap's separate production-planning gate. Immediately before merge,
    re-scan the destination branch and renumber the file and references if that
    prefix now collides.
  - Verify: human review can reproduce the ADR from named result artifacts;
    `make verify` passes; no production service, Buildroot default, real-recipient
    route, or persistent panel-media path has been enabled.

## Done criteria

- The exact Apple permission, logged-in-session, lock/sleep/restart, and
  background-item behavior is measured on a named macOS build.
- EventKit and Contacts can supply the minimized provider-neutral model, or the
  ADR names a direct Google-first alternative.
- Mixed fixture provider compositions prove calendar and contacts can vary
  independently without changing the normalized contract.
- Text and generic audio-file Messages automation is either proven within a
  bounded self/test matrix or explicitly rejected in favor of draft-only
  behavior.
- Local transcription and audio format choices are evidence-backed and fail
  closed against cloud processing.
- The Tailnet relay independently authorizes read, prepare, transcribe, and fake
  submission capabilities with mandatory signed single-use requests, without
  trusting loopback headers or exposing a LAN listener.
- No friend is contacted, no personal payload or credential enters Git/logs,
  and no ambiguous automation result causes a retry.
- ~~The companion-boundary ADR and a reviewed follow-up plan make the next
  production action explicit without implementing past the genuine platform
  unknowns.~~
- The companion-boundary ADR makes the roadmap's separate reviewed-planning
  gate the next production action without implementing past the genuine
  platform unknowns.

**Amended 2026-08-05:** Removed the fixed ADR 0004 reservation because it
collided with the pending BeagleBone hardware-baseline ADR. Pending plans now
select a free number from the current target branch at ADR creation and repeat
the collision check immediately before merge.

**Amended 2026-08-05:** Moved follow-up production-plan creation and review to
Step 6 of the end-to-end roadmap so this feasibility plan has one terminal
condition: publish measured evidence and its ADR, then stop.

**Amended 2026-08-06:** the shared command-confusability review found the
requested stop phrase too close to the prepare phrase for silent admission.
Step 1 now reviews `finish recording` as the preferred form and carries any
exception into the later panel-wide catalog; this feasibility tranche still
does not implement or select a recognizer. The later production plan must use a
purpose-internal, stop-only active command set during `wish_recording`; it may
not run the general command projection over exportable recording audio.

**Amended 2026-08-07:** Step 1 now carries all eleven `AGENDA-*` decisions,
including the deliberately pending timezone-data and ownership choice. It does
not select an alternative or complete the still-unchecked review step; roadmap
Step 6 owns the post-evidence decision.

**Amended 2026-08-16:** recorded the completed decision-review subpart without
checking Step 1. The user accepted `AGENDA-001`–`AGENDA-010` and kept
`AGENDA-011` deferred; roadmap Step 5 still must author and review the bounded
experiment contract, matrix, redaction evidence, limits, recipient class,
maximum side effects, and manual stop points before any feasibility probe.

**Amended 2026-08-16:** recorded the Step 1 execution environment drift and
authored the canonical proposed experiment contract at
`experiments/macos_personal_companion/README.md`. The current host is macOS
26.6.1 build 25G76 with Messages 26.0 build 1450.700.71 and Tailscale 1.98.10;
full Xcode remains unavailable under the selected Command Line Tools path. The
contract freezes the outcome taxonomy, fixture/live matrix, zero-by-default
external-effect boundary, proposed 12-handoff/two-audio ceiling, self/test-only
recipient class, redaction allowlist, and eight manual stop points. Step 1 stays
unchecked until explicit human safety review; no provider access, capture,
Tailnet change, login-item change, or Messages handoff occurred.
