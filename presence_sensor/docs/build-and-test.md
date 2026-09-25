# Build and hardware test procedure

Nothing in this directory has been target-compiled or proven to wake the
BeagleBone Black. Treat every result below as a validation gate, not as an
expected success claim.

**FRDM i.MX 8M Plus (ADR 0008).** The procedure below is BeagleBone history.
On the FRDM, Buildroot builds the module (`bhp-presence-sensor`) against NXP
`linux-imx` `lf-6.12.49-2.2.0` for arm64, the gpio-keys path is part of the boot
DTB, and the evidence procedure is `docs/operations/frdm-bringup.md` with
`hardware_tests/frdm/frdm_pir_edge_capture.sh` and `frdm_suspend_wake.sh`. For a
manual host compile use `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-` against a
prepared `linux-imx` tree in place of the `arm` values below.

## Prepare a Linux v6.12 target tree

Use the exact configured source/output tree that produced the target kernel.
For a separate output directory, point `KERNEL_SRC` at that prepared output and
ensure its generated headers are present. A typical source-tree preparation is:

```sh
make -C /path/to/linux-6.12 \
  ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  olddefconfig modules_prepare
```

The custom path needs GPIO descriptors, the input subsystem, system sleep, and
evdev for userspace observation. The stock baseline additionally needs
`CONFIG_KEYBOARD_GPIO`. AM335x DeepSleep0 has further platform and firmware
requirements documented in `../../docs/research/sleep-wake-strategies.md`.

## Build the custom module

```sh
make -C presence_sensor modules \
  KERNEL_SRC=/path/to/prepared/linux-6.12 \
  ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
```

The expected artifact name is `presence_sensor/beagle_house_pir_input_driver.ko`.
Confirm its `vermagic` against the running target before loading it.

## Build both experiment overlays

The overlay build needs the kernel's `include/dt-bindings` tree plus `cpp` and
an overlay-capable `dtc`:

```sh
make -C presence_sensor overlays \
  KERNEL_SRC=/path/to/linux-6.12
```

Expected artifacts:

- `presence_sensor/overlays/beagle-house-custom-pir.dtbo`
- `presence_sensor/overlays/beagle-house-gpio-keys-wake-baseline.dtbo`

Compilation proves syntax only. Before boot deployment, use the target base DTB
and `fdtoverlay` to check symbol resolution. Bootloader overlay configuration is
image-specific, so inspect the target image's documented U-Boot mechanism.

## Safe experiment order

1. Verify the PIR output never exceeds the BeagleBone Black GPIO electrical
   limits. Do not assume a module powered from 5 V has a 3.3 V-safe output.
2. Confirm P9.21 is free, maps to GPIO0_3 on the exact board, and is not claimed
   by another cape, UART configuration, or overlay.
3. Apply only the stock gpio-keys baseline overlay. Do not load the custom
   overlay or module for this run.
4. Find the named input device with `/proc/bus/input/devices` or `evtest`, then
   verify both `0` and `1` transitions while fully awake.
5. Locate the baseline platform device's `power/wakeup` attribute and confirm it
   is `enabled`. Verify `/sys/power/mem_sleep`, running WKUP-M3 firmware, and PM
   logs before attempting suspend.
6. Arrange an independent recovery path and preserve logs, then perform one
   controlled `mem` suspend/wake attempt. A runtime input event is not evidence
   of DeepSleep0 wake.
7. Only after the baseline is understood, repeat with the custom overlay and
   `beagle_house_pir_input_driver.ko` instead.

Useful evidence to capture on each run includes the exact kernel release and
config, loaded DT/overlay list, pinctrl ownership, `/proc/interrupts` before and
after activity, input events, `power/wakeup`, `/sys/power/mem_sleep`, firmware
load messages, suspend/resume log, and the WKUP-M3 reported wake source.

## Interpretation

| Result | Narrow conclusion only |
| --- | --- |
| Neither experiment reports awake-state changes | Investigate wiring, voltage, polarity, pinmux, GPIO ownership, and IRQ mapping first |
| Baseline reports changes but does not wake | Investigate wake enablement, selected sleep state, GPIO0 parent wake, WKUP-M3, firmware, and board PM |
| Baseline wakes but custom does not | Compare custom probe logs, wake-IRQ attachment, sysfs wake policy, and interrupt state |
| Both wake once | Repeat across asserted/deasserted starting states and many cycles before treating the path as reliable |

Any failed GPIO read, IRQ request, input registration, or custom wake setup is a
hard probe error or explicit kernel error; there is no silent runtime-only
fallback when `wakeup-source` was requested.

**Amended 2026-09-25:** marked this procedure as BeagleBone history and pointed
the FRDM build and evidence path at Buildroot and `hardware_tests/frdm/`.
