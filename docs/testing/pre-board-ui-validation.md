# Pre-board UI validation

**Amended 2026-09-26:** the approved UI milestone is complete.
All results are `simulation_not_hardware_evidence`. The
[machine-readable record](pre-board-ui-validation.json) distinguishes the
completed checks and remaining gates. All 29 scenarios pass in each display
tier. This supersedes no physical hardware or live-provider requirement.

The seven approved changes are implemented in the shared renderer:

1. Larger type: 48 px word/clock, 40 px state headings, 36 px definitions,
   28 px examples/controls and 24 px metadata, without automatic shrinking.
2. A shared grid aligns content, agenda columns and controls; rating targets
   are 212×64 px and use the same geometry for painting and hit testing.
3. One visible flip instruction replaces repeated hints.
4. Routine presentation/reveal bookkeeping and acknowledged/empty queues stay
   quiet. Actual pending ratings/retractions show “Waiting to sync”; a real
   in-flight review transfer shows “Syncing with Lexinet”; rejected evidence
   shows “Not saved”. Provisional voice evidence remains a separate warning.
   Offline, stale and action feedback cannot obscure pending-review status.
5. Product-facing copy replaces implementation labels without inventing dates,
   freshness ages, successful delivery or provider connections.
6. The trusted fixture night screen contains only dim red `HH:MM` on black,
   with no input targets. Untrusted night time, blanking and display failure
   remain black. Time is fixed fixture text and red is uncalibrated; this is
   not a household clock implementation or an AGENDA-011 decision.
7. Pinned Montserrat Medium assets, licenses, hashes and glyph validation are
   committed in `9a3e063`. All tiers use the same generated 24/28/36/40/48 px
   assets. Coverage is the declared Latin/punctuation subset, not all Unicode;
   unsupported glyphs and text overflow fail explicitly.

The [before/after review](../../panel_ui/tests/golden/review/pre-board-before-after.png)
and [row manifest](../../panel_ui/tests/golden/review/pre-board-before-after.json)
cover all 29 reviewed scenarios, with `effffd4` on the left and the updated
baselines on the right. Exact comparisons retain zero channel/pixel tolerance.
The native screen set is also packaged as a 29-page PDF at
`.build/pre-board-ui/output/pdf/house-panel-screens.pdf`; all 29 pages and embedded captures passed visual and structural QA.

| Check | Current evidence |
| --- | --- |
| Final `make verify` | Passed; all 41 host CTests, `.build/pre-board-ui/final-repository-verification.log` |
| Final `make verify-docs` | Passed, `.build/pre-board-ui/final-docs-verification.log` |
| Headless / actual native windows | 29/29 exact each, `.build/screenshots/report.json` and `.build/sim/native/report.json` |
| Font and layout | Asset integrity, glyph coverage/resolution, supported punctuation, explicit unsupported/overflow rejection and rating target edges passed |
| Study status / isolation | Shell and real fixture HTTPS tests passed, including acknowledgement, rejection across restart and durable-store protection |
| Type-B startup correction | Committed as `de57201`; targeted tests, review, isolated full verification (40 CTests), and guest touch commits passed |
| Refreshed VM | Passed; 41 Linux CTests and 29/29 scenarios, `.build/pre-board-ui/touch-provision.log` and `.build/sim/vm/report.json` |

`test_durable_delivery_fixture_isolation` rejects queued/saved/rejected fixtures
before durable restore or mutation unless explicitly selected into a memory
store. Tests compare the complete persisted bytes after refused and isolated
fixture loads, retain the pending rating, and replay the exact original sync
request after restart. Synthetic receipts cannot acknowledge a user's outbox.
`panel_display_layout`, `panel_simulation_integration` and
`panel_simulation_https_resume` cover the visible and semantic regressions.

Reproduction commands and runtime boundaries remain in
[virtual-panel operations](../../scripts/sim/README.md):
`make verify-panel-goldens`, `make verify-panel-simulator`,
`make verify-panel-vm`, `make verify` and `make verify-docs`.
The isolated touch logs are `.build/pre-board-touch-verify.log` and
`.build/pre-board-touch-docs.log`. The combined guest build passed all 41
Linux CTests in `.build/pre-board-ui/touch-provision.log`.

The host run explicitly skipped GNU AArch64 cross compilation because
`aarch64-linux-gnu-g++` was unavailable, kernel-module compilation because
`KERNEL_DIR` was unset, and Buildroot defconfig evaluation because
`BHP_BUILDROOT_DIR` was unset. Those skips are not passes. Native guest builds
passed as recorded above. Agenda, contacts, recording and delivery are
synthetic; voice has no acoustic qualification. Real Lexinet/Tailnet, providers,
production enablement and every FRDM display/touch/audio/PIR/power/wake gate
remain outside this milestone. QEMU cannot establish physical calibration or
current consumption; guest suspend remains disabled.

The first refreshed VM capture run passed both touch protocols, presence
blanking and 18 screens before a guest-wide scheduling stall interrupted SSH.
Serial output showed RCU warnings and simultaneous soft lockups across all
four virtual CPUs; the cause remains unestablished. A graceful reboot restored
normal control and DRM commits. The failed report and serial evidence remain
in `.build/pre-board-ui/vm-control-timeout-report.json` and
`.build/pre-board-ui/guest-stall-serial.log`. The rerun passed all 29 scenarios, both touch protocols on their first
attempt, and presence blanking with unchanged CPU configuration, deadlines and
comparison tolerances. It is recorded in
`.build/pre-board-ui/vm-after-reboot-verification.log`; this is not an endurance
qualification.

The guest has 27 exact image matches and the two previously declared semantic
checks: real 70%→60% grace brightness and fixture-clock unavailability alongside
the supervisor’s trusted NTP lifecycle. The PDF uses the exact native captures.
The Mac windows were left running with a fresh synthetic vocabulary session
and the guest agenda preview. No production services or providers were enabled.
