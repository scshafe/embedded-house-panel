# Plan: Arch development handoff

**Goal:** preserve all project work and provide a cloneable, verified Arch development starting point.
**Out of scope:** flashing the arrived board, hardware claims, Tailnet administration,
implicit system package installation, and pushing without superseding the earlier no-push instruction.

## Context

Continue from `pre-board@2a5cb00`, including the newer cloud-session work.
The root checkout is clean, but historical verification worktrees contain drafts;
audit them and preserve any unique source on archival refs. GitHub currently has
only the old `main`. Use a self-contained Git bundle for transfer. The native SDL
launcher needs a Linux path; the Cocoa/HVF ARM64 VM launcher remains Mac-specific.
The user reports receiving the board; measurements and enablement remain gated.

## Steps

- [x] **Step 1: Preserve work and document the Arch starting point**
  - Audit refs, stash and worktrees; archive unique drafts without replacing the
    current implementation. Add Arch dependency setup and a portable handoff.
  - Verify: `make verify-docs`

- [x] **Step 2: Launch the native panel on Linux**
  - Extend the existing SDL launcher, preserve the Mac bundle path, and cover
    Linux selection, dry-run behavior and bootstrap guards.
  - Verify: `python3 -m unittest discover -s scripts/sim/tests -p 'test_*.py'`
  - Verify: `python3 -m unittest discover -s scripts/environment/tests -p 'test_*.py'`

- [ ] **Step 3: Verify and package committed work**
  - Run repository checks, commit logical changes, verify a fresh bundle clone,
    and provide exact setup/resume commands. Transfer to Arch if accessible.
  - Verify: `make verify`
  - Verify: `git bundle verify .build/arch-migration/house-panel.bundle`

## Done criteria

The current branch is clean, all unique source work is committed or preserved on
archive refs, and the bundle has no prerequisites. A fresh clone contains the
Arch guide and runnable setup commands. Distinguish actual Arch execution from
local/mocked verification, and identify any remaining transfer/access step.

**Amended 2026-09-30:** the first combined Mac verification exposed Linux-only
procfs/pidfd assumptions in the newer cloud-session capture tests. Preserve all
three live checks on Linux, report their skip elsewhere, and test unsupported
termination refusal on non-Linux before rerunning Step 3. No runtime change.

The next run passed all 48 CTests and 43 images, then exposed an eager Python
union annotation in a newer verification test on the Mac's Python 3.9. Defer
that test's annotations, matching the production checker, and retain the same
assertions. Run the remaining Python gates before the final combined rerun.

The fresh bundle clone exposed a fault-matrix checker path filter that excluded
all source CMake files when any ancestor directory was named `.build`. Restrict
the exclusion to paths inside the checkout and cover both source inclusion and
generated-file exclusion before repeating the fresh-clone check.
