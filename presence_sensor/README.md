# Presence sensor

Status: initial Linux v6.12-oriented prototypes. No target compilation,
electrical validation, overlay load, IRQ delivery, suspend entry, or wake cycle
has been demonstrated on the house-panel hardware.

## Domain ownership

This domain owns the Linux-facing PIR presence signal, its input-event ABI, and
two controlled wake experiments. Its public interface is the persistent
`EV_SW/SW_FRONT_PROXIMITY` state described in `docs/input-abi.md`.

Dependencies are Linux gpiolib, the input subsystem, generic device wakeup and
wake-IRQ support, Device Tree/pinctrl, the AM335x OMAP GPIO irqchip, and—for
DeepSleep0—the TI WKUP-M3/AMx3 PM stack and matching firmware.

## Experiments

| Experiment | Implementation | Purpose |
| --- | --- | --- |
| `stock_gpio_keys_wake_baseline` | In-tree Linux `gpio-keys` plus a local overlay | Establish whether the candidate GPIO/platform wake path works without custom C code |
| `custom_pir_input_driver` | Local platform/input module plus a local overlay | Provide a narrowly defined presence-state device and explicit wake-setup errors |

Both use candidate BeagleBone Black P9.21 / GPIO0_3 and assume an active-high,
3.3 V-safe PIR output. Those are test hypotheses, not verified wiring facts for
the installed panel. Never apply both overlays at once; they intentionally
compete for the same pad and GPIO descriptor.

Start with `docs/build-and-test.md`. The hardware and mainline/TI evidence,
including unresolved DeepSleep0 assumptions, is in
`../docs/research/sleep-wake-strategies.md`.

## Source map

- `Kbuild` and `Makefile`: external-module and combined build entrypoints.
- `experiments/custom_pir_input_driver/`: custom driver, binding, overlay, and
  hypothesis.
- `experiments/stock_gpio_keys_wake_baseline/`: stock-driver baseline overlay.
- `overlays/Makefile`: builds both separately named `.dtbo` files.
- `docs/input-abi.md`: stable userspace semantics and Device Tree interface.
- `docs/build-and-test.md`: target build and evidence-gathering procedure.
