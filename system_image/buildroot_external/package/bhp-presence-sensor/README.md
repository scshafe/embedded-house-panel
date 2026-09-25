# Buildroot package: presence sensor

This directory is the canonical Buildroot glue for the presence/wake work
stream. It combines the standard `kernel-module` infrastructure with a small
generic-package step that compiles and installs both Device Tree experiments.

Public Buildroot interface:

- Kconfig symbol: `BR2_PACKAGE_BHP_PRESENCE_SENSOR`
- package target: `bhp-presence-sensor`
- installed module: the kernel-module infrastructure's `extra` directory
- inert overlay artifacts:
  `/usr/lib/beaglebone-house-panel/device-tree-overlays/`

The package deliberately does not copy either overlay into the boot partition,
edit `uEnv.txt`, or autoload the module. The two overlays claim the same
candidate pin and are mutually exclusive. `S31bhp-presence-sensor` is an
explicit opt-in readiness check/load skeleton for a separately configured boot
experiment.

Overlays are per board (`BHP_BOARD_PRESENCE_OVERLAYS` in the selected profile):

- `beaglebone-black` (history): both BBB overlays from `presence_sensor/overlays/`.
- `frdm-imx8mp`: only `bhp-frdm-pir-custom.dtbo`, rendered from
  `BHP_BOARD_PRESENCE_CUSTOM_OVERLAY_TEMPLATE`; the stock gpio-keys PIR node is
  merged into the boot DTB by `bhp-board-device-tree` instead. Rendering fails
  while any `FRDM-GATE-PIR` value is `UNMEASURED`.

The source was originally reviewed against Linux 6.12 APIs. Buildroot compiles
it against the image-selected kernel (NXP `linux-imx` `lf-6.12.49-2.2.0` on the
FRDM; 6.18.21 on the BeagleBone history image), so a successful build
proves API compatibility only—not electrical safety, interrupt delivery, or
suspend wake.

**Amended 2026-09-25:** documented the per-board overlay sets and the FRDM
custom-overlay rendering gate.
