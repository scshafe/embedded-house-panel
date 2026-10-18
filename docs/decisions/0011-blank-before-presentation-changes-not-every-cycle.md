<!-- @canonical supervisor-presentation-blank-rule -->

# ADR 0011: Blank Before Presentation Changes, Not Every Cycle

**Status:** accepted

**Date:** 2026-10-18

## Context

`panel_supervisor` wrote exact logical brightness zero at the start of every
cycle. It then published the complete lifecycle snapshot, awaited the UI's
presentation outcome, and wrote the requested brightness only after an exact
`committed` result. The policy publishes a new snapshot every poll, so the
panel went dark once per poll for the length of the wait.

On the FRDM board with the Waveshare panel, the owner saw this as flicker: a
split-second black screen every few seconds. The demo run
(`hardware_tests/results/frdm/panel-demo-20261018T171850Z`) logged 210 cycles,
each awaiting the UI for 12–44 ms (median 24 ms). In the simulator the
backlight is a file, so the defect was invisible.

The blank exists so the panel is never lit while it shows a display mode the
UI has not committed. The supervisor derives `display_mode` from `ui_mode`
alone (`day_active`, `night_red`, `suspended`). When the mode does not change,
the UI repaints a mode it already committed. Between cycles it already redraws
locally (taps, flash cards, the clock) while lit, with no gate. Blanking every
cycle with an unchanged mode adds no protection, and real hardware shows it.

## Decision

A cycle keeps the applied backlight while it publishes and awaits only when
all of these hold:

1. the applied brightness is nonzero;
2. the last committed presentation's `display_mode` equals the display mode of
   the snapshot about to be published; and
3. the new `ui_mode` is not `suspended`.

Every other cycle writes exact logical brightness zero first, as before.

After the await:
- an exact `committed` outcome permits the requested brightness write;
- every other outcome (not committed, failed, timeout, disconnect,
  acknowledgement unavailable, contract violation) writes exact zero, emits
  `presentation_degraded`, completes the cycle and still makes any
  policy-requested suspend request.

Only a committed presentation may set a nonzero brightness. Every cycle ends
with zero unless its presentation was committed.

The supervisor still publishes one complete snapshot every poll. Publishing
only on change was considered and rejected, because it would touch the
protocol-v2 contract (ADR 0006, ADR 0007) and the UI's rule that a contact
may not span a lifecycle acknowledgement.

## Consequences

- A lit panel with an unchanged mode no longer flickers.
- Every mode change still goes dark until the UI commits the new mode.
- **The safety window grows when the mode is unchanged.**
  - If the presentation then fails, the panel stays lit, showing the last
    committed mode, for at most the configured presentation timeout before
    the zero write.
  - Previously the zero came before the wait.
  - A UI crash between cycles already left the panel lit until the next
    cycle, so the window grows by at most one timeout.
- The supervisor still writes the requested brightness after every committed
  steady-state cycle. Whether a same-value write blinks the panel is checked
  on target. Skipping such writes would be a separate decision.
- This amends ADR 0004's "brightness stays zero" for non-committed outcomes.
  Brightness is zero by the end of such a cycle, but a cycle that keeps the
  panel lit writes the zero after the wait, not before it.

## References

- [Plan](../../.claude/plans/2026-10-18-supervisor-steady-state-blank.md)
- [FRDM panel UI demo plan](../../.claude/plans/2026-10-18-frdm-panel-ui-demo.md)
- [ADR 0004](0004-freeze-review-gate-integration-contracts.md)
- [Panel policy](../behavior/panel-policy.md)
- [panel_supervisor README](../../panel_supervisor/README.md)
