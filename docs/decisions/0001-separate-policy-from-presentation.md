# ADR 0001: Separate Power Policy from Presentation

**Status:** accepted for the scaffold

**Date:** 2026-08-05

## Decision

`panel_supervisor` is the sole runtime authority for presence interpretation, solar state, backlight, and suspend/resume. `vocabulary_panel` renders an instructed mode and accesses Lexinet without power privileges. The kernel presence provider reports sensor state/events and wake capability but contains no product policy.

## Alternatives considered

- Put all behavior in the GUI: simple process count, but a UI crash could strand brightness/power and the kernel boundary would be confused.
- Put backlight policy in the module: proves kernel control but embeds location/time/network/product decisions in privileged code.
- Separate supervisor and UI: one extra local protocol, but independently testable policy and a narrow privilege boundary.

## Consequences

- UI toolkit changes do not alter wake/power behavior.
- The supervisor must have explicit startup, crash, and disconnected-UI behavior.
- The local mode protocol is a versioned interface and therefore needs compatibility tests.
