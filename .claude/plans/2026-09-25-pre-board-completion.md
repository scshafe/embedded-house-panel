# Plan: Pre-board Completion

**Goal:** complete the approved UI review milestone and present every updated
screen from the shared renderer, native Mac simulator and Linux VM.

**Out of scope for this milestone:** Phase 2 onward, personal-data providers,
AGENDA-011, physical FRDM evidence, system-wide installation and pushes.

## Context

Start at `virtual-panel@effffd4` on `pre-board`. Phase 0 read-only orientation
and both repository baseline commands passed. On 2026-09-25 the user approved
all seven Phase 1 proposals: larger type, a shared layout grid, one visible flip
instruction, meaningful sync status, product copy, a dim red time-only night
screen and a pinned project font. The next requested milestone is seeing every
updated screen before continuing the broader pre-board work.

Use 48 px word/clock, 40 px state headings, 36 px definitions, 28 px examples
and controls, and 24 px metadata. Review wrapping without automatic shrinking.
Night RGB remains configurable and uncalibrated; untrusted time is hidden.
Agenda remains a clearly labelled synthetic preview. Keep semantic state,
touch provenance, lifecycle authority, fail-dark and durable study guarantees.

The remaining pre-board phases retain the user's original order and gates.
Amend this plan before Phase 2, using the end-to-end roadmap and Lexinet plan,
once this visual milestone has been shown. No further UI approval is needed
to implement the seven already accepted changes.

## Steps

- [x] **Step 1: Pin the project font and glyph coverage**
  - Commit licensed source inputs, deterministic generation metadata, generated
    LVGL fonts and explicit glyph coverage validation. Normal builds consume
    committed outputs without requiring a generator or network.
  - Verify: `make verify`

- [x] **Step 1b: Restore complete multitouch kernel state**
  - Preserve idle slot coordinates and the kernel's selected slot when the
    supervisor opens the Type-B device. Retain held-contact cancellation and
    validate the first new contact after reopening at unchanged coordinates.
  - Verify: `make verify`

- [x] **Step 2: Apply the approved shared screen design**
  - Apply typography, grid, copy, quiet truthful status and the night clock.
    Preserve before/after images in the same commit as changed goldens.
  - Verify: `make verify-panel-goldens`

- [x] **Step 3: Capture and present every display tier**
  - Rebuild the native and guest applications, capture all scenarios, inspect
    fitting and consistency, and publish an individually viewable screen set.
    Every result is `simulation_not_hardware_evidence`.
  - Verify: `make verify-panel-simulator`
  - Verify: `make verify-panel-vm`
  - Verify: `make verify`
  - Verify: `make verify-docs`

- [x] **Step 4: Record the Phase 2–7 decisions and execution split**
  - Register ADR 0010: node-capability panel authentication and the
    SHA-256-pinned official Tailscale `1.102.2` arm64 archive. Record that
    Lexinet's long-term host is deferred and that steps needing this Mac itself
    run in a later Mac-local session.
  - Verify: `make verify-docs`

- [x] **Step 5: Repin study.v1 and replace the obsolete configuration**
  - Repin the unchanged contract bundle to Lexinet `ce42272`. Replace both
    next-card env examples with `study.v1` configuration: HTTPS base URL,
    profile and topic references, `tailnet_node` authentication and an empty
    credential path. Retire the remaining next-card documentation; the adapter
    code was removed on 2026-09-25.
  - Verify: `python3 vocabulary_panel/tools/export_study_v1_contract_fixtures.py --lexinet-source-dir <lexinet> --check`
  - Verify: `make verify`

- [x] **Step 6: Run the panel client against a disposable real Lexinet**
  - Build Lexinet from an explicit checkout, serve it on loopback with a
    disposable PostgreSQL 16 cluster and bearer under ignored `.build/lexinet/`,
    seed a synthetic corpus through its JSON CLI, and drive the C++ client and
    durable reducer through create/resume, idempotent sync retry, atomic
    rejection, idempotency and state conflicts, authentication failure, server
    restart, offline recovery and successor linking. The bearer never enters
    panel configuration, images or the virtual panel.
  - Verify: `make verify-lexinet-live BHP_LEXINET_SOURCE_DIR=<lexinet>`
  - Verify: `make verify`

- [ ] **Step 7: Prove the real Tailnet path (Mac-local, human-assisted)**
  - Enable Lexinet's native `tsnet` listener with disposable state, enroll the
    Debian guest with pinned Tailscale `1.102.2` as the tagged panel node, run
    the Lexinet Step 5 evidence matrix, then run the panel end to end over the
    Tailnet. The user makes every admin-console change.
  - Verify: sanitized `real_tailnet_not_panel_hardware` results cover the tagged, untagged, wrong-tag, live-revocation and certificate/hostname/TLS cases; the virtual panel studies real Lexinet cards over the Tailnet

- [x] **Step 8: Record the Step 8 version pins**
  - Recommend Buildroot stable versus LTS, the kernel point-release pins and the
    Tailscale override with evidence. The user confirms each before any image
    build.
  - Verify: the user's confirmations are recorded in this plan; `make verify-docs`

- [ ] **Step 9: Package the production service layout**
  - Give each service a distinct principal, socket, state path and restart
    limit; revoke raw touch from the UI at the atomic cutover; apply the
    least-privilege device matrix; install the pinned Tailscale archive. Services
    stay disabled and fail-dark by default.
  - Verify: `make verify-docs`
  - Verify: `make verify`

- [ ] **Step 10: Boot the Buildroot QEMU image (Mac-local)**
  - Build `qemu_aarch64_virt` with the external tree in a Linux build
    environment on this Mac and boot it with the existing launcher.
  - Verify: BusyBox init starts every packaged service; mdev classifies touch and denies PIR; service principals, sockets, permissions and rendered env files match the packaging matrix; the default is fail-dark and a simulation-only mode enables the panel

- [ ] **Step 11: Build the FRDM packages up to the gates (Mac-local)**
  - Build the kernel, U-Boot, ATF, mwifiex, the applications and the Waveshare
    backlight patch, and record exactly where the build stops. Ask for the NXP
    firmware license once; the PIR device-tree gate stays until measured.
  - Verify: build logs record each package result and the named stopping gate; results are labelled build evidence only

- [ ] **Step 12: Close media capture through the broker**
  - Implement the production holder-claim/media IPC with the fake recognizer.
    The supervisor verifies kernel-visible capture closure after blanking and
    before any suspend write, with the bounded termination path. Microphone
    rail-off stays an FRDM gate.
  - Verify: `make verify`
  - Verify: the Mac-local guest run proves closure against a real `snd-aloop` capture device

- [ ] **Step 13: Run the suspend and fault matrix**
  - Cover every roadmap Step 8 fault row with host tests, using real Lexinet
    where relevant; the Mac-local VM run repeats them and records the s2idle
    attempt.
  - Verify: `make verify`
  - Verify: the Mac-local VM run records every fault row and the s2idle result

- [ ] **Step 14: Prepare the board documents**
  - List candidate J18 native GPIO pads from UM12340 and the schematic (the
    profile stays `UNMEASURED`) and update the FRDM bring-up document for the
    virtual-panel and pre-board changes.
  - Verify: `make verify-docs`

- [ ] **Step 15: Prepare the agenda Step 1 safety review packet**
  - Prepare the packet and stop for the user's decision. No EventKit,
    Contacts, Messages, companion or AGENDA-011 work.
  - Verify: the packet exists and the user's decision is recorded

## Done criteria

All approved changes appear in the three tiers; all scenarios pass their
declared pixel/semantic comparisons; local commits include before/after review
images; the user can view every screen. Both verification commands pass.

**Amended 2026-09-25:** created after explicit approval of all seven UI findings.
The plan stops at the requested visual milestone before the next phase.

**Amended 2026-09-26:** VM verification exposed incomplete Type-B startup state
restoration after a device switch. Step 1b fixes the observer's raw kernel-state
initialization without changing touch authority or weakening lifecycle checks.
Step 1 passed isolated `make verify` and `make verify-docs`; logs are under
`.build/pre-board-font-{verify,docs}.log`. Generated font integrity and
byte-for-byte regeneration checks also passed.

Step 1b passed isolated `make verify` (40 CTests) and `make verify-docs`,
the combined guest's 41 CTests, and fresh single-touch/Type-B lifecycle commits
after reopening at the same coordinates. The isolated logs are
`.build/pre-board-touch-{verify,docs}.log`; guest evidence is recorded with the
UI validation milestone below.

**Amended 2026-09-26 (completion):** all seven approved changes are implemented.
The three tiers passed all 29 scenarios; host and guest each passed 41 CTests.
`make verify` and `make verify-docs` passed. The individually viewable 29-page
PDF passed visual QA, and before/after images accompany the golden changes.
The [UI validation record](../../docs/testing/pre-board-ui-validation.md)
records proof paths, the recovered guest scheduling stall and remaining gates.

**Amended 2026-09-26 (Phases 2–7):** the user resumed the original Phases 2–7
in a cloud session linked to this Mac. Linux-only work runs there and returns as
local commits on `pre-board`; steps that need this Mac itself (the QEMU guest,
Tailnet enrollment, and Buildroot and FRDM image builds) are prepared there and
run in a later Mac-local session. The user chose node-capability Tailnet
authentication and the SHA-256-pinned official Tailscale `1.102.2` archive
(ADR 0010), and deferred where Lexinet runs long-term. Steps 4–15 map Phases
2–7. Results stay `simulation_not_hardware_evidence`, except real-Tailnet
results (`real_tailnet_not_panel_hardware`) and FRDM package builds, which are
build evidence only. The Phase 2–7 done criteria are the user's original ones:
the panel client passes against a disposable real Lexinet and over the real
Tailnet, or names the blocking human step; the Buildroot QEMU image boots with
the production layout; FRDM package builds are recorded up to the named gates;
broker capture closure and the Step 8 fault matrix pass in the VM; and
`make verify` and `make verify-docs` pass.

**Amended 2026-09-26 (Step 8 decisions, confirmed by the user):**

- Buildroot: pin `2026.08`, supported until `2026.11.1` ships. It is the
  smallest move from the verified `2026.05.1`, whose support ends when
  `2026.08.1` ships. Adopt the `2027.02` LTS at its first point release.
  `2025.02.x` (LTS until March 2028) was not selected because it steps back
  about 15 months of packages and toolchains. Evidence: the buildroot.org news
  and LTS pages and the `2026.08` announcement.
- FRDM kernel: keep NXP `lf-6.12.49-2.2.0`, NXP's last 6.12 BSP, which the
  defconfig and the Waveshare backlight patch target, through bring-up.
  Evaluate `lf-6.18.37-2.1.0` after the hardware gates pass.
- QEMU simulation kernel: mainline longterm `6.18.54` (kernel.org,
  2026-09-25).
- Tailscale override: the official static arm64 `1.102.2` archive (ADR 0010).

The Buildroot archive digest and source identity, and the QEMU kernel pin, are
applied in the Mac-local session, where the archives can be downloaded and
verified. A newer release is never adopted implicitly.
