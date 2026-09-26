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
