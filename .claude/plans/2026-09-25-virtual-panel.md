# Plan: Finish the Host Applications and Virtual Panel

**Goal:** reproduce all display states through one LVGL surface implementation,
first as deterministic PNGs, then in a native Mac window, then through the real
Linux services in a Debian AArch64 QEMU guest.

**Out of scope:** FRDM evidence or enablement, NXP firmware, live personal data,
Messages, a real recognizer, AGENDA-011, sibling repository writes, and pushing.

## Context

The user explicitly requests the three simulation tiers and host application
work on `virtual-panel`, starting at `60b8fc0`. ADRs 0001 and 0004–0008 retain
authority. Lexinet Steps 6 and 7b must share the canonical reducer and durable
data plane; the visible shell can be developed against fixtures while those
dependencies are implemented, but their full checkboxes require all mapped
proofs. Agenda is a synthetic presentation only, independent of the unapproved
companion feasibility work. ADR 0009 defines simulation evidence boundaries.

The host is macOS 26.6.1 arm64, 24 GiB RAM, approximately 65 GiB free at start.
SDL2, libpng, OpenSSL, Lima and Colima were present. CMake/Ninja were installed
during the preliminary environment check before the full installation rule
arrived. Further system-wide installation awaits the user's explicit response.

## Steps

- [x] **Step 1: Record simulation boundaries and reproducible inputs**
  - Add ADR 0009 and a simulation-only board profile, schema assertions and
    source/dependency pins. Preserve the sole active FRDM profile.
  - Verify: `make verify-docs`

- [ ] **Step 2: Render shared surfaces and deterministic goldens**
  - Move LVGL lifetime/display ownership to the neutral shell. Render vocabulary,
    agenda fixtures, night, blank and degraded states into 1024x600 memory.
    Commit baseline PNGs and a review contact sheet. Compare exact pixels by
    default and emit a bounded tolerance report plus diff images on mismatch.
  - Verify: `make verify-panel-goldens`

- [ ] **Step 3: Complete the study reducer and resilient data plane**
  - Execute the panel-side Lexinet Step 6 and 7b requirements together: shared
    HTTP lifetime, typed fixture HTTPS, cancellable work, bounded atomic state,
    two-sided history, source-qualified events, durable ratings and touch Undo.
  - Verify: `scripts/verification/verify_host_cpp.sh vocabulary_panel`

- [ ] **Step 4: Forward supervisor-observed touch to the shell**
  - Add exact-identity Linux evdev observation and bounded authenticated
    normalized forwarding with observer-measured contact duration. Keep raw
    input outside the UI and preserve lifecycle invalidation and fail-dark.
  - Verify: `scripts/verification/verify_host_cpp.sh panel_supervisor`

- [ ] **Step 5: Run every scenario in the native simulator**
  - Add the opt-in SDL2 binary, strict JSON scenario/input protocol, keyboard
    controls and capture mode. Use shared surface/controller code and clearly
    synthetic lifecycle, data, night color and commit observations.
  - Verify: `make verify-panel-simulator`

- [ ] **Step 6: Add fake command recognition and effect admission**
  - Implement the shared fake-only broker, lifecycle closure, one-shot lease
    state, content-addressed command/policy artifacts and two-phase effect gate.
    Keep wish recording and all real capture disabled.
  - Verify: `make verify-panel-media`

- [ ] **Step 7: Boot Debian and exercise real guest services**
  - Prepare a pinned Debian arm64 guest with native application builds, real
    DRM page-flip completion, evdev, supervisor lifecycle sockets, simulated
    backlight class device and uinput PIR. Add dry-run-first QEMU/QMP scripts.
    Use suspend disabled unless a measured simulated wake trial succeeds.
  - Verify: `make verify-panel-vm`

- [ ] **Step 8: Verify and publish simulation contact sheets**
  - Capture every scenario in both visible tiers, machine-readable transcripts,
    reproducible launch commands, exact completed work and remaining gates.
  - Verify: `make verify`

## Done criteria

All three tiers reproduce their scenarios, the native and QEMU windows open on
this Mac, the QMP control can tap/toggle PIR/capture, both contact sheets exist,
and `make verify` passes including headless golden comparison. Small logical
commits remain local. Full roadmap/linked-plan checkboxes remain open unless
their entire existing proof matrix is recorded, including external gates.

**Amended 2026-09-25:** created from the complete user request. The user orders
the useful visual tiers first while permitting the dependent study and touch
work to proceed in parallel. No production or hardware gate is waived.
