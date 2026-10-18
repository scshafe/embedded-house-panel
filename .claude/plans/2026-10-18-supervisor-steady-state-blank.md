# Plan: Stop the Supervisor Blanking the Panel Every Cycle

<!-- @canonical supervisor-steady-state-blank-plan -->

**Status (2026-10-18):** Proposed. The owner asked for this plan after seeing
the flicker in the FRDM demo
([demo plan](2026-10-18-frdm-panel-ui-demo.md), Step 4).

**Goal:** a lit panel whose display mode is unchanged stays lit through each
supervisor cycle. The supervisor still blanks before any change of display
mode, and it still leaves the panel dark unless the UI commits the frame.

**Out of scope:**
- publishing fewer snapshots (Option B below);
- calibrated brightness values;
- the demo's fixed-noon drift into night;
- suspend.

## The problem

- **Flicker on the board.** The owner saw the panel "entirely go black for a
  split second" every 2–3 seconds (`hardware_tests/results/frdm/panel-demo-20261018T171850Z/notes.txt`).
- **Log evidence.** In the supervisor log, every cycle writes brightness 0,
  publishes the snapshot, waits for the UI to commit it (12 ms min, 24 ms
  median, 44 ms max over 210 cycles), and then writes the requested brightness.
- **Why every cycle.** The policy publishes a new snapshot on every poll
  (`panel_supervisor/README.md`), so each cycle blanks the panel for the
  length of that wait. One reading of the `2-0045` brightness node, taken at
  steady 70%, was 0.
- **This is the documented design.**
  - `panel_supervisor/README.md`: "Every cycle writes exact logical brightness
    zero, publishes the complete UI mode snapshot, and awaits an explicit
    presentation outcome."
  - `docs/behavior/panel-policy.md`, "Presentation degradation and fatal
    faults", says the same.
  - In the simulator the backlight is a file, so nobody saw it.
  - The code is `PanelSupervisor`'s cycle (`panel_supervisor/src/panel_supervisor.cpp`):
    `apply_backlight(0)`, then `publish_and_await_presentation()`, then
    `apply_backlight(decision.brightness_percent)` after a commit.

## What the blank protects, and what it does not

- **It protects mode changes.** The blank keeps the panel from being lit while
  it shows a frame the UI has not confirmed for the new snapshot. The case
  that matters is a change of display mode (`day_active`, `night_red`,
  `suspended`): for example, day content must never be lit at night.
- **It does nothing when the mode is unchanged.**
  - `display_mode` depends only on `ui_mode`
    (`unix_socket_ui_mode_notifier.cpp`, `display_mode()`).
  - So in a steady cycle the UI repaints the same mode it already committed.
    A presence-grace change (70% → 60%) keeps the same `ui_mode`.
  - Between cycles the UI already redraws locally (taps, card flips, the
    clock) while the panel is lit, with no supervisor gate.
  - A one-poll blank adds no protection there.

## Approach (Option A, recommended)

**Rule.** Blank first only when the cycle could light a mode the UI has not
committed. Concretely, the cycle **keeps the current backlight while it
publishes and awaits** only when all of these hold:
1. the applied brightness is nonzero (the panel is lit);
2. the last presentation was committed, and its `display_mode` equals the
   display mode of the snapshot about to be published;
3. the new `ui_mode` is not `suspended`.

Otherwise the cycle writes 0 first, exactly as it does today.

**After the await:**
- **Committed:** write the requested brightness (lower or higher; same
  mode).
- **Any other outcome:** write 0. This covers not committed, failed,
  timeout, disconnect and contract violation. The cycle then logs
  `presentation_degraded` and still makes any suspend request, as today.

**What changes for safety:** with an unchanged mode, a failed presentation
leaves the panel lit for at most the presentation timeout
(`BHP_PANEL_SUPERVISOR_PRESENTATION_TIMEOUT_MILLISECONDS`) before the 0 write,
instead of 0 before the wait. The frame on screen is still the last committed
mode. A UI crash between cycles already leaves the panel lit until the next
cycle today, so the window grows by at most one timeout. The ADR in Step 1
records this.

**Option B (not chosen): publish only when something changed.** The README
says each poll publishes a new snapshot, and touch binding depends on it ("A
contact may not span that boundary"). Changing that touches the protocol-v2
contract (ADR 0006/0007) and the UI's binding rules. Option A changes only the
supervisor's output order.

## Steps

- [ ] **Step 1: Record the decision.**
  - Add ADR 0011 "Blank before presentation changes, not every cycle", with
    the rule, the safety window and Option B. Register it in
    `docs/decisions/README.md`.
  - Amend, with dated notes, the per-cycle zero wording in:
    - `panel_supervisor/README.md` ("Output order is direction-safe");
    - `docs/behavior/panel-policy.md` ("Presentation degradation and fatal
      faults");
    - ADR 0004 ("brightness stays zero" becomes "brightness is zero by the end
      of the cycle").
  - Verify:
    - `make verify` exit 0.
- [ ] **Step 2: A backlight probe, and the baseline.**
  - Add `backlight-samples N` to `scripts/deploy/frdm_panel_demo.sh` and the
    board script. It reads `/sys/class/backlight/2-0045/brightness` N times,
    10 ms apart, and reports the count of each value. It only reads, and the
    unit tests cover the dry run.
  - With the current supervisor, run the demo (`start 180`), take
    `backlight-samples 3000` (30 s) at steady presence, then `stop`. The
    owner has already approved demo runs that stop weston.
  - Verify:
    - `python3 -m unittest discover -s scripts/deploy/tests -p test_*.py` exit 0;
    - the baseline record under `hardware_tests/results/frdm/panel-demo-<UTC>/`
      shows zero samples at steady 70%.
- [ ] **Step 3: Change the cycle.**
  - Implement the rule in `PanelSupervisor`'s cycle. The keep-lit decision
    uses `state_.applied_brightness_percent` and the display mode of
    `last_committed_presentation_` before publishing.
  - Tests in `panel_supervisor/tests/panel_supervisor_test.cpp`:
    - a steady same-mode poll calls `publish`, `await_presentation`,
      `backlight` and writes only the requested brightness;
    - a same-mode poll whose presentation times out or fails writes only 0;
    - present → grace (70 → 60, same mode) writes only 60;
    - day → night still writes 0 and then the night brightness (the existing
      `active_day_to_night_change_blanks_before_exact_presentation_then_lights`);
    - absent → suspended still blanks first;
    - an unlit start still blanks first (the existing
      `initial_active_mode_is_notified_before_nonzero_backlight`);
    - a commit whose `display_mode` differs from the expected one is still
      rejected, and the cycle writes 0.
  - Verify:
    - `ctest --test-dir .build/host-dev -R panel_supervisor` exit 0;
    - `make verify` exit 0.
- [ ] **Step 4: Check it on the panel with the owner.**
  - Rebuild the demo bundle, `start 180`, and repeat `backlight-samples 3000`
    at steady presence.
  - Run `presence 0` and then `presence 1`, as in the demo plan.
  - The owner watches for flicker for a minute.
  - Verify:
    - no zero samples at steady presence;
    - `presence 0` goes 70 → 60 → 0 and `presence 1` returns to 70;
    - the owner's yes or no: no flicker at steady state;
    - the record under `hardware_tests/results/frdm/panel-demo-<UTC>/`.

## Risks

- **A repeated same-value write might itself blink the panel.** The
  `ws_dsi2dpi` driver forwards each write to the panel's MCU over I2C, and
  nobody has checked a write of the same value. Step 4's samples and the
  owner's eye will show it. If it does blink, skip writes whose value equals
  the applied brightness; that is a separate, reviewed change.
- **10 ms sampling can miss a blank shorter than that.** The observed waits
  are 12–44 ms. The owner's eye is the second check.
- **The sysfs node reports the value last written, not the light output.**
  This is fine here, because the bug is in the writes.
