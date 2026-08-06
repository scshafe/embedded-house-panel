# Agent Working Contract

This repository is organized by product domain. Before editing a domain, read its `README.md` and search for `@canonical`, `@extension-point`, and the capability name.

## Invariants

- Hardware observations are evidence, not defaults. Never promote an unmeasured pin, brightness, power, or wake assumption into production configuration.
- `panel_supervisor` is the only owner of display/power policy.
- `presence_sensor` reports state/events and wake capability; it does not decide brightness, solar mode, networking, or suspension policy.
- `vocabulary_panel` is unprivileged and does not write backlight or power-management interfaces.
- Lexinet remains authoritative. Offline data must be bounded and explicitly marked degraded.
- Secrets never enter Git, command arguments, URLs, logs, image overlays, or diagnostics.
- Expected hardware and network failures return structured, machine-readable errors. Do not silently substitute plausible data.
- Alternative sleep/wake experiments stay isolated under `presence_sensor/experiments/`; only an evidence-backed decision may promote one to the production path.

## Verification

Run `make verify` before handing off. Hardware claims additionally require the on-target procedures under `hardware_tests/` and captured results under the ignored `hardware_tests/results/` directory.

