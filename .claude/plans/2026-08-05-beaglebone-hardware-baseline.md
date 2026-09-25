# Plan: Establish the BeagleBone Hardware Baseline

**Status (2026-09-24):** blocked; the BeagleBone Black was lost.
[ADR 0008](../../docs/decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
moves characterization to the FRDM i.MX 8M Plus board. Reuse this plan's
evidence discipline for an FRDM baseline plan; its Black-specific steps are
not executable.

**Superseded (2026-09-25):** the
[FRDM hardware baseline plan](2026-09-25-frdm-hardware-baseline.md) is the
executable hardware plan. This file remains as history; its Verify items stay
mapped as `planned` and will never be recorded.

**Goal:** Establish the exact board, cape, PIR, pin, kernel, display, suspend,
wake, network-recovery, and power facts without overwriting the existing eMMC,
producing a working BeagleBone proof and quantitative gates for any future
platform.

**Out of scope:** Selecting or purchasing the final product board; implementing
the production Lexinet API; building the final enclosure; declaring any wiring
or sleep mode safe from legacy source alone.

## Context

- This repository is the canonical new implementation; the four sibling legacy
  repositories are read-only evidence and remain independent.
- [ADR 0003](../../docs/decisions/0003-characterize-beaglebone-before-selecting-product-platform.md)
  selects the owned Black for characterization without selecting it as the
  final product platform.
- The durable legacy findings are in
  [recovery summary](../../docs/legacy/recovery-summary.md).
- Never boot the installed eMMC OS before preserving it. Prepare a verified,
  non-flasher microSD, boot explicitly from it with eMMC unmounted/read-only,
  image and hash eMMC, and only then inspect or mutate the target.
- Perform wiring, continuity, and resistance checks unpowered. Measure PIR
  voltage/waveforms while isolated from the Black; connect it only after safe
  logic levels and cape pin availability are established.
- Store raw command/measurement results under ignored `hardware_tests/results/`.
  Commit only redacted summaries, schemas, reusable scripts, and decisions.
- Hardware observations are evidence, not defaults. Every result names exact
  hardware, supply boundary, software revision, test count, and failures.

## Steps

- [ ] **Step 1: Record physical hardware and wiring inventory**
  - Identify board/revision markings, 4D cape model/revision/touch, PIR
    manufacturer/part number, power hardware, storage, and every connection.
  - Photograph both sides and assembled wiring before moving anything.
  - Reconcile why legacy artifacts referenced both P9_21/GPIO0_3 and
    P9_15/GPIO1_16 for motion.
  - Output: `docs/hardware/hardware-inventory.md`, referenced photos, and a pin
    table marking every value observed, datasheet-derived, or assumed.
  - Verify: another agent can identify every component and reconstruct wiring;
    all unresolved electrical characteristics remain explicitly pending.

- [ ] **Step 2: Prepare rescue media and preserve eMMC**
  - Download a current official non-flasher BeagleBoard Debian image and verify
    its published checksum before writing a dedicated microSD.
  - Prove that microSD supplies `/` and eMMC is unmounted/read-only before
    imaging the correct eMMC block device.
  - Hash the captured image before any read-only mount and inspect bootloader,
    device tree, installed `/home/debian/gpio-waker` artifacts, modules,
    overlays, services, and build clues.
  - Inspect shell history only with owner consent and redact credentials.
  - Output: `docs/hardware/legacy-board-state.md`, pinned rescue-image metadata,
    and excluded artifacts under `hardware_tests/results/legacy/`.
  - Verify: `findmnt`/block evidence proves the boot and mount state; rescue and
    eMMC hashes validate; the summary records every recovered or missing clue.

- [ ] **Step 3: Capture current Debian hardware baseline**
  - Record bootloader, kernel, OS, device-tree identity, cape EEPROM,
    overlays/modules, GPIO/backlight/input/DRM/framebuffer discovery, available
    PM states, command line, and relevant kernel/journal output before installs.
  - Output: `docs/hardware/debian-baseline.md` and a timestamped redacted bundle
    under `hardware_tests/results/debian-initial/`.
  - Verify: the collection manifest identifies every command and software
    version; cold-boot evidence and eMMC read-only status are present.

- [ ] **Step 4: Characterize display, touch, and backlight**
  - Identify device-tree nodes, drivers, cape conflicts, resolution, pixel
    format, DRM/KMS versus framebuffer, touch input, and actual backlight range.
  - Build a disposable harness that renders only full-screen black, red
    candidates, and white through the discovered interface.
  - Test blanking and 0/25/100 brightness only within the discovered range;
    capture boot, start/stop, mode change, suspend, and resume with a photodiode
    or calibrated camera trace so a brief bright flash cannot escape visual
    inspection.
  - Output: `docs/hardware/display-baseline.md`, reversible probe, and results.
  - Verify: 100 on/off/dim cycles complete without kernel errors, a stuck
    display, or an unaccounted bright trace; minimum usable red and actual
    backlight-off behavior are recorded.

- [ ] **Step 5: Characterize PIR electrically and through normal GPIO tools**
  - Confirm signal voltage, polarity, rise/fall, hold time, retrigger behavior,
    warm-up, noise, and whether one or two signals exist.
  - Select a provisional non-conflicting GPIO0 candidate and observe it first
    through `libgpiod`, before loading custom code.
  - Output: `docs/hardware/pir-baseline.md`, updated pin table, timestamped edge
    traces, and a reversible GPIO observation script.
  - Verify: at least 100 cycles show expected polarity without unexplained
    transitions; no line is owned by a competing cape/driver; application grace
    is clearly distinct from measured sensor hold time.

- [ ] **Step 6: Prove DeepSleep0 and GPIO0 wake**
  - Run the stock `gpio-keys` baseline first, then the custom input-driver
    experiment with the same sensor, line, logical event, and requested state.
  - Record PM firmware/configuration, `/sys/power/state`, selected
    `/sys/power/mem_sleep`, wakeup-source state, wake reason, asserted-at-suspend
    behavior, and failure paths.
  - Upgrade the trial harness before counting successes. Give every trial one
    correlation ID and capture external PIR stimulus, confirmed deep-state
    entry, suspend-stat delta, M3/platform wake reason, GPIO IRQ delta, Linux
    event, timing marker, and negative-control result.
  - Classify every attempt as `entered_and_attributed`, `aborted`,
    `wrong_source`, `timeout`, or `unobserved`; only the first is success.
  - Enumerate the implementation × kernel/BSP × starting-level × wake-edge
    matrix and record the expected result for every cell rather than selecting
    only successful conditions.
  - Keep the screen black; after resume re-read current sensor state.
  - Output: `docs/hardware/suspend-wake-baseline.md`, isolated proof inputs under
    `hardware_tests/`, and repeatable results.
  - Verify: every cell claimed as supported has at least 50 consecutive
    `entered_and_attributed` trials; unsupported cells have explicit falsifying
    evidence; no aborted or unattributed attempt increments success; a
    24–72-hour masked-PIR/no-command soak classifies every spontaneous wake;
    wake-to-local execution and network/tailnet restoration remain separate.

- [ ] **Step 7: Measure product-relevant power states**
  - Define separate `board_input` diagnostic and `product_battery` comparison
    boundaries. Record converter/charger topology, quiescent loss, battery
    voltage/state-of-charge/temperature, and charging state; never compare
    results across unnamed or different boundaries.
  - Extend `hardware_tests/schemas/power_measurement.schema.json` before capture
    so it represents controller-only, application-off, red LED, STT, gesture,
    simultaneous workload, and transition records. Include raw trace path/hash,
    sample rate, settling and analysis windows, min/mean/max, integrated joules,
    instrument serial/calibration, ambient temperature, and configuration.
  - Measure active daytime, dim-red LCD, dedicated red LED if available,
    backlight zero, idle, `freeze`, `mem`/DeepSleep0, transitions, and any safe
    PMIC-off/controller prototype.
  - ~~Capture a shared GPIO marker with PIR, analyzer, CPU-ready, rendered-frame,
    photodiode/backlight, Wi-Fi, tailnet, and Lexinet milestones under the same
    trial correlation ID.~~
  - Capture a shared GPIO marker with PIR, analyzer, CPU-ready, rendered-frame,
    photodiode/backlight, recovery for the exact configured network transport,
    tailnet, and Lexinet milestones under the same trial correlation ID. Include
    Wi-Fi association/recovery only when Wi-Fi is explicitly selected for the
    measured configuration and backed by adapter, driver, provisioning,
    antenna, suspend/recovery, and power evidence. Wired Ethernet remains the
    prototype baseline; this plan selects no network hardware default.
  - Use independent state entries, fixed settling/window durations, blocked or
    randomized state order, and repeats across cold boots and thermal states.
  - ~~Exercise low-state-of-charge voltage sag and load steps during simultaneous
    display/Wi-Fi/media load, including charger removal/handover, shutdown
    reserve, boot-lockout hysteresis, reset reason, and recovery.~~
  - Exercise low-state-of-charge voltage sag and load steps during simultaneous
    display, exact configured network transport, and media load, including
    charger removal/handover, shutdown reserve, boot-lockout hysteresis, reset
    reason, and recovery.
  - Output: `docs/hardware/power-baseline.md`, updated schema, reusable capture
    tooling, schema-valid summaries, and content-hashed raw traces under the
    ignored results directory.
  - Verify: each stable state has a declared independent sample population;
    accepted wake paths have at least 50 attributed latency samples; calculations
    reproduce from raw traces; median/p95 are reported for characterization and
    p99 only for a 1,000-cycle population; the deepest reliable state and every
    non-comparable boundary are explicit.

- [ ] **Step 8: Publish BeagleBone baseline decision**
  - Synthesize the exact PIR pin, cape/display stack, kernel line, GPIO ABI,
    deepest reliable power state, wake latency, network recovery, image path,
    and remaining blockers.
  - Compare the measured results with the common gates in
    `docs/research/compute-platform-assessment.md` without selecting a new board
    from vendor specifications alone.
  - ~~Output: `docs/decisions/0004-beaglebone-hardware-baseline.md`.~~
  - Output: a BeagleBone hardware-baseline ADR whose four-digit prefix is chosen
    by scanning `docs/decisions/` on the current target branch when the record
    is created. Immediately before merge, re-scan the destination branch and
    renumber the file and references if that prefix now collides.
  - Verify: every accepted claim links to captured evidence; remaining
    assumptions name a falsifiable next experiment; eMMC remains restorable and
    all legacy Git worktrees remain clean.

## Done criteria

- Exact board, cape, PIR, wiring, voltage levels, and occupied pins are known.
- The original eMMC is preserved or the precise reason it could not be is
  recorded before mutation.
- Display, touch, backlight, GPIO, suspend, wake, power, and resume behavior are
  reproducible on a named current kernel/image.
- GPIO0 motion wake meets the agreed repeat count with diagnosable failures.
- Power and interaction latency are measured at documented boundaries.
- The hardware-baseline ADR can justify continuing on Black and define
  candidate-board gates without relying on legacy code or marketing
  specifications.
- `make verify` passes with all committed documentation and reusable tooling.

**Amended 2026-08-05:** Removed the fixed ADR 0004 filename because the Mac
companion feasibility plan may finish while the board is unavailable. Pending
plans select a free number from the current target branch at ADR creation and
repeat the collision check immediately before merge.

**Amended 2026-08-07:** Step 7 now measures recovery and simultaneous load for
the exact configured network transport. Wi-Fi is included only when explicitly
selected and evidence-backed; wired Ethernet remains the prototype baseline,
and no network hardware default was introduced.
