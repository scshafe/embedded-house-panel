# Hardware Tests

Owns reversible discovery, wake, display, and power experiments. Test output belongs under ignored `hardware_tests/results/`; curated conclusions belong in `docs/hardware/` or an ADR.

## Safety order

1. Photograph and inventory unpowered hardware and wiring.
2. Verify PIR datasheet and powered output in isolation before connecting it to the BBB.
3. Boot a verified non-flasher microSD and prove eMMC is unmounted/read-only.
4. Image and hash eMMC before booting or modifying its installed OS.
5. Run read-only discovery.
6. Test display and GPIO while awake.
7. Run suspend only with a local console/recovery path and explicit acknowledgement.
8. Measure power at a documented supply boundary.

## Read-only inventory

On the BeagleBone:

```sh
hardware_tests/scripts/collect_read_only_board_inventory.sh \
  hardware_tests/results/board-inventory-YYYYMMDD
```

Review every captured file before sharing it; network addresses, serial numbers, and logs may be private even when no credentials are collected.

## Suspend/wake trials

The suspend script does nothing without `--execute`:

```sh
sudo hardware_tests/scripts/run_suspend_wake_trials.sh \
  --mode mem --count 1 --output hardware_tests/results/wake-smoke --execute
```

Start with one cycle, a serial console, and a known alternate wake/recovery method. Increase to the acceptance counts only after failures are understood. The script records that a process resumed; it does not by itself prove which interrupt caused wake.

The current script is a smoke/characterization harness, not an acceptance
oracle. It lacks externally observed PIR stimulus, confirmed deep-state entry,
suspend-stat and GPIO-IRQ deltas, platform/M3 wake reason, negative controls,
and a shared hardware timing marker. An immediate suspend abort or unrelated
wake can therefore look successful. Before any trial contributes to a support
claim, implement the correlated manifest and result classes specified in
[the hardware baseline plan](../.claude/plans/2026-08-05-beaglebone-hardware-baseline.md#step-6-prove-deepsleep0-and-gpio0-wake).

## Evidence requirements

Each result set names board/cape/sensor revisions, wiring, supply, kernel, device tree, Git commit, command, time, expected outcome, actual outcome, and every failure. Power samples follow `schemas/power_measurement.schema.json`.

The current power schema represents preliminary scalar BeagleBone stable-state
samples only. It cannot encode transition traces, controller/application-off,
media workloads, uncertainty, or cross-platform battery-boundary comparisons.
Do not squeeze those observations into its existing enum. Extend and verify the
schema as Step 7 requires before collecting candidate-comparison evidence.
