# Presence sensor

Status: initial Linux v6.12-oriented prototypes. No target compilation,
electrical validation, overlay load, IRQ delivery, suspend entry, or wake cycle
has been demonstrated on the house-panel hardware.

## Domain ownership

This domain owns the Linux-facing PIR presence signal, its input-event ABI, and
two controlled wake experiments. Its public interface is the persistent
`EV_SW/SW_FRONT_PROXIMITY` state described in `docs/input-abi.md`.

Dependencies are Linux gpiolib, the input subsystem, generic device wakeup and
wake-IRQ support, Device Tree/pinctrl, and the SoC GPIO irqchip and suspend
path: on the FRDM i.MX 8M Plus the `gpio-mxc` irqchip, GPC wake routing, and
PSCI system suspend; on the BeagleBone history target the AM335x OMAP GPIO
irqchip and the TI WKUP-M3/AMx3 PM stack.

## Experiments

| Experiment | Implementation | Purpose |
| --- | --- | --- |
| `stock_gpio_keys_wake_baseline` | In-tree Linux `gpio-keys` plus a local overlay | Establish whether the candidate GPIO/platform wake path works without custom C code |
| `custom_pir_input_driver` | Local platform/input module plus a local overlay | Provide a narrowly defined presence-state device and explicit wake-setup errors |

~~Both use candidate BeagleBone Black P9.21 / GPIO0_3 and assume an active-high,
3.3 V-safe PIR output. Those are test hypotheses, not verified wiring facts for
the installed panel.~~ The BeagleBone overlays (P9.21 / GPIO0_3) are history
(ADR 0008). Never apply both paths at once; they intentionally compete for the
same pad and GPIO descriptor.

## FRDM i.MX 8M Plus

- Primary path: the stock `gpio-keys` node (`wakeup-source`,
  `EV_SW/SW_FRONT_PROXIMITY`, input name `bhp-frdm-pir-gpio-keys`) in the
  project overlay
  `system_image/buildroot_external/board/beaglebone-house-panel/frdm-imx8mp/dts/bhp-frdm-imx8mp-panel.dtso.in`,
  merged into the boot DTB. Wake uses the i.MX GPC (`CONFIG_IMX_GPCV2`) on a
  native `GPIO1`–`GPIO5` pad; the PCAL6416A expanders cannot wake.
- Secondary path: the custom driver is not AM335x-specific (it uses gpiolib,
  the input subsystem, and generic wake IRQs), so it is ported with
  `experiments/custom_pir_input_driver/frdm-imx8mp-custom-pir-overlay.dtso.in`
  (input name `bhp-frdm-pir-custom`); it compiles against `linux-imx`
  `lf-6.12.49-2.2.0` for arm64. Apply it only to a DTB without the gpio-keys
  node.
- Both templates fail to render until `FRDM-GATE-PIR` values (pin, pad, GPIO
  line, active level, pad owner) are measured with `hardware_tests/frdm/`; see
  `docs/operations/frdm-bringup.md`.

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

**Amended 2026-09-25:** retargeted to the FRDM i.MX 8M Plus (ADR 0008):
gpio-keys in the project overlay is the primary PIR path, the custom driver is
ported as a secondary overlay, and the BeagleBone pin is history.
