# Plan: Build the Initial BeagleBone House Panel Scaffold

**Goal:** Produce a new, buildable, documented project skeleton with working host-tested slices for presence/wake, panel policy, Lexinet flash cards, embedded-image integration, deployment tooling, and hardware experiments.
**Out of scope:** Declaring any GPIO/cape wiring production-safe before physical inspection; finalizing the unknown Lexinet API; claiming measured DeepSleep0 behavior without the board; designing a production-ready enclosure CAD model.

## Context

- The new repository is nested inside the recovery workspace so the four legacy repositories remain independent and unmodified.
- The evidence and restart reasoning live in `../docs/legacy-system-audit.md` and `../docs/restart-direction.md`.
- Target assumptions are provisional: AM335x BeagleBone Black, 4D 480x272 cape, one PIR state signal, and P9_21/GPIO0_3 as a wake candidate.
- Build all hardware boundaries behind explicit interfaces and provide host fakes. Hardware results may select a different implementation without changing policy code.
- Treat the tailnet as private transport, not application authorization. Never put Tailscale or Lexinet credentials in Git, command lines, URLs, or logs.

## Steps

- [x] **Step 1: Establish repository contracts and domain layout**
  - Verify: `test -d .git && test -f README.md && find . -mindepth 2 -name README.md | sort`

- [x] **Step 2: Research GUI and sleep/wake alternatives**
  - Verify: human review of `docs/research/gui-strategies.md` and `docs/research/sleep-wake-strategies.md`, with dated primary-source links and explicit experiment choices

- [x] **Step 3: Implement presence and wake experiment variants**
  - Verify: `scripts/verification/verify_presence_sensor_sources.sh`

- [x] **Step 4: Implement and host-test the panel policy supervisor**
  - ~~Verify: `cmake --workflow --preset host-debug && ctest --preset host-debug --output-on-failure`~~ (CMake is not installed on the audit host.)
  - Verify: `scripts/verification/verify_host_cpp.sh panel_supervisor`; CMake files remain required for the Buildroot SDK path

- [x] **Step 5: Implement and host-test the Lexinet flash-card slice**
  - ~~Verify: `cmake --workflow --preset host-debug && ctest --preset host-debug --output-on-failure`~~ (CMake is not installed on the audit host.)
  - Verify: `scripts/verification/verify_host_cpp.sh vocabulary_panel`; CMake files remain required for the Buildroot SDK path

- [x] **Step 6: Add the Buildroot external tree and target services**
  - Verify: `scripts/verification/verify_buildroot_external_tree.sh`

- [x] **Step 7: Add reproducible host, cross-build, upload, and diagnostic scripts**
  - Verify: `scripts/verification/verify_shell_scripts.sh`

- [x] **Step 8: Document architecture, behavior, security, hardware testing, and enclosure intent**
  - Verify: `scripts/verification/verify_documentation.sh`

- [x] **Step 9: Run the complete repository verification suite**
  - Verify: `scripts/verification/verify_repository.sh`

## Done criteria

- `git status` shows only intentional scaffold files in the new repository and all four legacy repositories remain clean.
- Host builds and tests prove the presence/solar/power state machine and flash-card interaction without requiring BeagleBone hardware.
- Both custom-module and stock-kernel wake experiments are represented and have explicit on-target procedures; neither is labeled proven.
- Cross-build/deploy scripts fail safely, require explicit configuration, support dry-run where side effects exist, and never embed credentials.
- Mermaid diagrams cover system architecture, process interconnectivity, supervisor logic, flash-card logic, and UX navigation.
- The README identifies hardware-dependent blockers and the single next on-device experiment.

**Amended 2026-08-05:** Added compiler-direct host verification because the available macOS host has Clang/G++ but no CMake/Ninja; this does not remove CMake from the target build contract.

**Completed 2026-08-05:** The fast repository suite passes on macOS, the full
supervisor suite also passes on native Linux, and the pinned Buildroot external
production and bring-up configurations evaluate successfully. Final audit
hardening added forced-zero startup, typed clock-readiness retry, fresh
content-identified release outputs, a key-only non-production provisioning
profile, and unique touch/PIR permission separation. The complete release image
build and all GPIO, cape, suspend/wake, and power measurements remain explicit
on-target evidence gates; completing this scaffold does not claim those results.
