# Plan: Establish the FRDM i.MX 8M Plus Hardware Baseline

<!-- @canonical frdm-hardware-baseline-plan -->

**Status (2026-09-25):** ready to execute when the hardware arrives; nothing
below has run on a board. Replaces the blocked
[BeagleBone hardware baseline plan](2026-08-05-beaglebone-hardware-baseline.md)
for all hardware work and keeps its evidence discipline.

**Goal:** establish the exact FRDM-IMX8MPLUS, Waveshare 7inch DSI LCD (C), PIR,
microphone, and board-input power facts, close every `FRDM-GATE-*` row of
[ADR 0008](../../docs/decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
with linked evidence, and replace every `UNMEASURED:` value in
`config/boards/frdm-imx8mp.env`.

**Out of scope:** selecting the final product board or battery (roadmap Step
14); CAD or enclosure changes; accepting any NXP license on the user's behalf;
declaring a pin, voltage, sleep state, or brightness safe from documentation
alone.

## Context

- Board facts live only in `config/boards/frdm-imx8mp.env`. A placeholder is
  replaced only by a reviewed commit that links JSON evidence under ignored
  `hardware_tests/results/frdm/`.
- Evidence comes from `hardware_tests/frdm/` (one script per gate, read-only by
  default, `--execute*` for any system change) following the ordered
  board-arrival checklist in `docs/operations/frdm-bringup.md`.
- Two blockers gate the project image: the `NXP-FIRMWARE-EULA` human decision
  (LPDDR4 training and IW612 firmware) and the measured `FRDM-GATE-PIR` values
  (the project DTB refuses to render without them). The NXP vendor image is the
  baseline until both clear.
- Evidence discipline carried over from the BeagleBone plan: preserve storage
  before mutation; measure electrical levels before connecting the PIR; raw
  results stay in ignored `hardware_tests/results/`; commit only summaries,
  schemas, scripts, and decisions; every result names exact hardware, supply
  boundary, software revision, test count, and failures.
- Observation contract: `docs/research/compute-platform-assessment.md` "Common
  bake-off contract". Suspend trials are classified `entered_and_attributed`,
  `aborted`, `wrong_source`, `timeout`, or `unobserved`; only the first counts.
  `wakeup_sources` counters are supporting evidence, never the oracle.
- Power is measured at the named `board_input` boundary (J2 USB-C PD) with an
  external meter; the board cannot measure it.

## Steps

- [ ] **Step 1: Record physical inventory and board identity**
  - Photograph both sides, labels, the J15 cable, the panel, the PIR, the
    charger, and every connection; record markings and revisions.
  - Run `frdm_board_identity.sh --board-marking` on the vendor image.
  - Output: `docs/hardware/frdm-hardware-inventory.md` with a pin/wiring table
    marking every value observed, datasheet-derived, or assumed, plus the
    IDENTITY evidence record.
  - Verify: the IDENTITY record has no `fail` check and `ram_capacity`,
    `emmc_capacity`, and `wifi_sdio_card` pass; another agent can identify every
    component and reconstruct wiring from the inventory.

- [ ] **Step 2: Preserve factory storage and capture the vendor baseline**
  - Back up the factory eMMC over SSH before any flash; record its SHA-256 and
    the vendor release name.
  - Capture the vendor kernel, DTB, `/proc/cmdline`, modules, and
    `/sys/power` state with the read-only inventory and gate scripts.
  - Output: `docs/hardware/frdm-vendor-baseline.md` and results under
    `hardware_tests/results/frdm/`.
  - Verify: the eMMC backup hash is recorded and a `uuu -b emmc_all` restore
    path is documented; every vendor-baseline claim links an evidence record.

- [ ] **Step 3: Select and characterize the PIR pin (FRDM-GATE-PIR)**
  - Choose a native `GPIO1`–`GPIO5` J18 pad from the schematic header table;
    never a PCAL6416A expander line.
  - Measure the J18 I/O voltage with a meter before wiring; connect the PIR
    output only to a 3.3 V bank.
  - Capture raw edges with `frdm_pir_edge_capture.sh --mode gpio` across
    enough windows for at least 100 cycles; record warm-up, hold, and retrigger.
  - Copy the J18 and edge-capture candidates into the profile.
  - Verify: `frdm_j18_gpio_inventory.sh` reports `pass` for candidate format,
    pad owner, GPIO consumer, and header voltage; at least 100 captured cycles
    show the expected polarity without unexplained transitions;
    `verify_frdm_device_tree_overlay.sh` logs `committed_pir_state` `measured`.

- [ ] **Step 4: Characterize display, touch, and backlight**
  - Run discovery on the vendor image, then on the project image; show a
    modetest pattern; run the backlight-off trial.
  - Repeat blank, dim, and full brightness only within the discovered range,
    with a camera or photodiode trace for boot, mode change, suspend, and
    resume so a bright flash cannot escape inspection.
  - Record the GT911 axis ranges with `evtest` before any calibration.
  - Output: `docs/hardware/frdm-display-baseline.md` and profile updates for the
    DRM, fbdev, touch, and backlight keys.
  - Verify: DISPLAY, TOUCH, and BACKLIGHT records have `first_frame_visible`,
    `touch_evdev`, `backlight_node`, and `backlight_zero_is_off` passing; 100
    on/off/dim cycles complete without kernel errors, a stuck display, or an
    unaccounted bright trace.

- [ ] **Step 5: Build and boot the project bring-up image**
  - Only after the `NXP-FIRMWARE-EULA` decision is recorded in an accepted ADR
    and Step 3 is committed.
  - Build with `scripts/build/build_bringup_image.sh` for `frdm-imx8mp`, flash
    with uuu, and boot the project DTB.
  - Verify: `/etc/beaglebone-house-panel/image-version` reports
    `board_id=frdm-imx8mp` and the build commit; the evdev PIR record passes
    `pir_input_device`, `gpio_keys_wakeup`, `pir_irq_label`, and
    `sw_front_proximity_events`.

- [ ] **Step 6: Prove suspend-to-RAM with attributed PIR wake (FRDM-GATE-SUSPEND)**
  - Smoke-test with `frdm_suspend_wake.sh --execute` for `deep`, then `s2idle`,
    with the RTC recovery alarm armed and the serial console attached.
  - Then run `hardware_tests/scripts/run_suspend_wake_trials.sh` on the
    bring-up image (which carries standard-library `python3` for the
    classifier) with FRDM expectations (PIR wake label, selected `mem_sleep`),
    the external observation record, and the independent negative control for
    every enumerated state × starting-level × wake-edge cell.
  - Output: `docs/hardware/frdm-suspend-wake-baseline.md`; `MEM_SLEEP_STATE` in
    the profile.
  - Verify: every cell claimed as supported has at least 50 consecutive
    `entered_and_attributed` trials; unsupported cells have explicit falsifying
    evidence; a 24–72-hour masked-PIR soak classifies every spontaneous wake.

- [ ] **Step 7: Measure network recovery after resume (FRDM-GATE-WIFI)**
  - Provision Wi-Fi and Tailscale on the board only; credentials never enter
    the repository or the image.
  - Pair `frdm_wifi_resume_timing.sh --after-next-resume` with each Step 6
    trial; include the tailnet probe and the Lexinet endpoint.
  - Verify: at least 50 post-resume samples record link, IPv4, route, tailnet,
    and Lexinet timings with median and p95; failed recoveries are counted, not
    dropped.

- [ ] **Step 8: Characterize microphone capture (FRDM-GATE-AUDIO)**
  - Capture from the CTIA headset microphone with the unmodified mixer, then
    record any operator-chosen routing change as a separate evidence record.
  - Verify: an AUDIO record passes `alsa_card`, `codec_i2c`, and
    `capture_non_silent`; the routing used is recorded with its amixer artifact.

- [ ] **Step 9: Measure board-input power (FRDM-GATE-POWER)**
  - With an inline USB-C PD meter at J2, record `active_day`, `active_night`,
    `backlight_zero`, `idle`, `freeze`, and `mem` (per `mem_sleep` variant) using
    `frdm_power_input_log.sh`.
  - Use independent state entries, fixed settling and analysis windows, and
    repeats across cold boots.
  - Output: `docs/hardware/frdm-power-baseline.md` and schema-valid v2
    power records.
  - Verify: each stable state has a declared independent sample population;
    every summary reproduces from its raw meter trace; the PD contract voltage
    check passes for every record.

- [ ] **Step 10: Publish the FRDM hardware-baseline decision**
  - Synthesize the PIR pin, display stack, kernel/BSP line, deepest reliable
    sleep state, wake latency, network recovery, power, and remaining blockers;
    compare with the common gates in `docs/research/compute-platform-assessment.md`.
  - Output: an ADR whose number is chosen by the collision-safe allocation in
    `docs/decisions/README.md`, and an amended ADR 0008 gate table.
  - Verify: every accepted claim links captured evidence;
    `scripts/boards/show_board_profile.sh` reports zero unmeasured values or
    each remaining one names a falsifiable next experiment; the factory eMMC
    backup remains restorable.

## Done criteria

- Every `FRDM-GATE-*` row is closed by linked evidence or explicitly blocked
  with a reason.
- `config/boards/frdm-imx8mp.env` contains no `UNMEASURED:` value without a
  named next experiment.
- PIR wake meets the agreed repeat count with diagnosable failures.
- Power and recovery latency are measured at the `board_input` boundary.
- `make verify` passes with all committed documentation and tooling.
