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

The source was originally reviewed against Linux 6.12 APIs. Buildroot compiles
it against the image-selected kernel, currently 6.18.21, so a successful build
proves API compatibility only—not electrical safety, interrupt delivery, or
suspend wake.
