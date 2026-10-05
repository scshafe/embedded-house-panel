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

The board is declared in `dev.toml [hardware]`. Its hardware gate, `make verify-board`, needs `BHP_BOARD_SSH_TARGET`, `BHP_BOARD_CONSOLE` and `BHP_FRDM_FACTORY_BACKUP_DIR` exported from the ignored `config/device.env` (`set -a; . ./config/device.env; set +a`). Current board status: `docs/operations/frdm-bringup.md`, "Arrival status".


<!-- scshafe-dev:begin landing -->
## Verify and landing

Managed by scshafe-dev: `dev adopt` and `dev update` refresh this section from `dev.toml`; change `dev.toml`, not these lines.

Before finishing, both of these must pass:

```sh
make verify
dev check .
```

How a change lands:

1. Work on a branch and open a PR.
2. Run the two commands above. If the repository is private, GitHub Actions does not run for it: verify locally and say in the PR what you ran. If it is public, wait for CI to be green.
3. Merge your own PR with a merge commit, one change at a time: `gh pr merge <N> --merge --subject "Merge #<N>: <title>"`. Never squash or rebase (both are off on the repository), and pass `--subject`: `gh pr merge` does not make the `Merge #N: <title>` subject by itself.

The project's agent may merge its own PR and push `main`; there is no approval gate.
<!-- scshafe-dev:end landing -->
