# Pre-board Phases 2–7 validation (cloud session)

The canonical record is
[`pre-board-completion-validation.json`](pre-board-completion-validation.json).
It covers the Linux-side work on plan
[Steps 4–15](../../.claude/plans/2026-09-25-pre-board-completion.md), done on
2026-09-26 in a cloud session linked to the Mac repository. The commits reached
the Mac as git bundles; nothing was pushed. Every result is
`simulation_not_hardware_evidence`. No real-Tailnet or FRDM hardware evidence
exists yet.

## Result

`make verify` and `make verify-docs` passed after every commit. The host-dev
suite now has 48 CTests.

- **Checked steps (4, 5, 6, 8).** The Phase 2–7 decisions and ADR 0010 are
  recorded, and the `study.v1` provenance is repinned. The
  [real-Lexinet contract run](lexinet-live-contract-validation.md) passes 10
  scenario runs and 3 cross-checks. The Step 8 version pins are recorded.
- **Step 9.** Tailscale `1.102.2` is installed from the official archive pinned
  by SHA-256. The standalone vocabulary principal is retired, sound nodes are
  root-only, and [`service-principals.json`](../../system_image/service-principals.json)
  records the principal matrix, which a verifier enforces. Open: bounded
  restart supervision for `tailscaled`.
- **Step 12.** `panel_supervisor` verifies kernel-visible microphone capture
  closure before every enabled suspend write. The termination path is bounded
  and scoped to the broker's UID. A container stand-in run of the guest matrix
  passed all six cases. It exercised holder discovery and termination only, not
  ALSA substream status. Open: the `snd-aloop` guest run and the production
  media IPC.
- **Step 13.** The [fault matrix](fault-matrix.md) ties all 12 roadmap Step 8
  rows to exact tests. Ten rows are covered and two are partial. Open: the VM
  repeat and s2idle.
- **Step 14.** The [J18 PIR pad candidates](../hardware/frdm-j18-pir-candidates.json)
  are ranked and `frdm-bringup.md` is updated. Open: schematic pin numbers and
  rails.
- **Step 15.** The agenda Step 1 packet is ready. Open: the user's decision.

## Repositories and commands

This session changed only `beaglebone-house-panel`, on `pre-board` from
`feba096`. It did not change Lexinet: `make verify-lexinet-live` builds it
read-only at `ce42272`, so no `panel-integration` branch was needed. The JSON
lists every launch and test command. The main ones are:

- `make verify` and `make verify-docs`;
- `make verify-lexinet-live BHP_LEXINET_SOURCE_DIR=/Users/coleshaffer/lexinet`;
- `python3 scripts/verification/check_fault_matrix.py`;
- `sudo python3 scripts/sim/guest/capture_closure_matrix.py --execute --check-binary <guest build>/panel_supervisor/capture_closure_check`.

## What waits on the Mac, the user or the board

The Mac-local session runs Steps 7, 10 and 11, Verify item 2 of Steps 12 and
13, and Step 15. Its prompt is the `claude/pre-board-mac-local-prompt.md` Project
doc. The user decides the NXP firmware license, the five agenda Step 1 clauses,
the weather provider, where Lexinet runs long-term and `AGENDA-011`. Only the
user can make Tailnet admin-console changes and supply the FRDM-IMX8MPLUS
schematic, which needs an NXP account.

The board still owns these gates:

- the PIR pin, header voltage and wake;
- display, touch and backlight;
- suspend with an attributed PIR wake;
- audio and microphone rail-off;
- Wi-Fi and power;
- the NXP firmware license, before the project image can boot.

The JSON's `remaining_for_the_board` list maps each gate to its ARRIVAL steps.
