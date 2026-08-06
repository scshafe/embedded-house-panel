# AM335x PIR sleep/wake strategies

Research snapshot: 2026-08-05. Implementation target: upstream Linux v6.12.

## Conclusion

Use the in-tree `gpio-keys` strategy as the first wake experiment and the custom
PIR input driver as the second. Both should use the same sensor, candidate pad,
logical input code, and requested sleep state. This makes the comparison useful:
the baseline removes local C code without changing the intended electrical and
platform wake path.

There is credible primary-source support for GPIO0 as an AM335x DeepSleep0 wake
source, but there is not yet evidence that this panel's P9.21/GPIO0_3 wiring,
running Linux image, firmware set, and board power path work together. Neither a
successful module probe nor awake-state interrupts would close that gap.

## What the sources establish

### Linux-side behavior

- Linux v6.12's [GPIO descriptor consumer documentation](https://docs.kernel.org/6.12/driver-api/gpio/consumer.html)
  requires descriptor-based acquisition and explicit input direction, and
  distinguishes sleepable GPIO reads. The custom driver therefore uses
  `devm_gpiod_get(..., GPIOD_IN)`, checks `IS_ERR`, checks `gpiod_to_irq`, and
  samples from a threaded IRQ with `gpiod_get_value_cansleep`.
- The v6.12 [`dev_pm_set_wake_irq` implementation](https://github.com/torvalds/linux/blob/v6.12/drivers/base/power/wakeirq.c#L32-L68)
  says a normal device I/O interrupt can be attached as a wake IRQ after
  `device_init_wakeup`; the PM core then arms it according to the standard
  device wakeup policy. The custom driver uses this path and tears it down on
  detach or probe rollback.
- The v6.12 [`gpio-keys` driver](https://github.com/torvalds/linux/blob/v6.12/drivers/input/keyboard/gpio_keys.c)
  is the relevant stock baseline: GPIO buttons use rising and falling IRQs,
  state is read through a GPIO descriptor, `wakeup-source` makes the device
  wake-capable, and suspend calls `enable_irq_wake`. Its binding defines
  `wakeup-event-action`; the baseline requests `EV_ACT_ANY`. See the primary
  [v6.12 gpio-keys binding](https://github.com/torvalds/linux/blob/v6.12/Documentation/devicetree/bindings/input/gpio-keys.yaml).
- The v6.12 [OMAP GPIO irqchip](https://github.com/torvalds/linux/blob/v6.12/drivers/gpio/gpio-omap.c#L542-L548)
  implements child GPIO `irq_set_wake` by delegating to the GPIO bank's parent
  IRQ. An `enable_irq_wake` return value confirms software acceptance at that
  layer; it does not by itself prove a full SoC DeepSleep0 wake cycle.

### AM335x low-power behavior

- TI's [AM335x Processor SDK power-management guide](https://software-dl.ti.com/processor-sdk-linux/esd/docs/latest/linux/Foundational_Components/Kernel/Kernel_Drivers/Power_Management.html)
  states that suspend depends on Linux/WKUP-M3 interaction and firmware, maps
  `mem` to DeepSleep0 for that SDK, and lists UART, GPIO0, and touchscreen as
  DeepSleep0 wake sources. It also lists the AMx3 PM, mailbox, remoteproc, IPC,
  EMIF SRAM, and firmware prerequisites.
- TI's [AM335x Low Power Design Guide, SPRAC74A](https://www.ti.com/lit/an/sprac74a/sprac74a.pdf)
  says GPIO0 remains on during system suspend and lists GPIO0 as a valid
  DeepSleep0 wake source on the AM335x GP EVM. This is SoC/EVM guidance, not a
  promise for every header pad and Linux distribution image.
- Upstream v6.12's [WKUP-M3 IPC source](https://github.com/torvalds/linux/blob/v6.12/drivers/soc/ti/wkup_m3_ipc.c#L70-L86)
  maps M3 wake-source interrupt 46 to `GPIO0`. Its
  [AM33xx PM core](https://github.com/torvalds/linux/blob/v6.12/arch/arm/mach-omap2/pm33xx-core.c#L323-L333)
  initially blocks suspend until PM initialization, WKUP-M3 IPC, and
  `am335x-pm-firmware.elf` are available. The v6.12
  [BeagleBone common Device Tree](https://github.com/torvalds/linux/blob/v6.12/arch/arm/boot/dts/ti/omap/am335x-bone-common.dtsi#L442-L444)
  also names `am335x-bone-scale-data.bin` for WKUP-M3 IPC.
- TI's [AM335x GPIO guide](https://software-dl.ti.com/processor-sdk-linux/esd/AM335X/09_03_05_02/exports/docs/linux/Foundational_Components/Kernel/Kernel_Drivers/GPIO.html)
  explicitly recommends a `gpio-keys` wake-source test and gives a GPIO0 line
  as its example. This strongly motivates the baseline, but the example does
  not validate this project's different GPIO0_3 candidate.
- BeagleBoard.org's [BeagleBone Black connector documentation](https://docs.beagleboard.org/boards/beaglebone/black/ch07.html)
  and [cape interface mapping](https://docs.beagleboard.org/boards/capes/cape-interface-spec.html)
  identify P9.21 as GPIO0_3 on the Black. Pin occupancy, cape conflicts,
  voltage, and actual installed wiring still require board inspection.
- The definitive SoC register reference remains TI's
  [AM335x Technical Reference Manual, SPRUH73Q](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf),
  especially its power-management and GPIO interrupt/wakeup chapters. Board
  validation must reconcile the exact silicon revision, pad mux, GPIO bank,
  parent interrupt, and selected low-power mode with that manual.

## What remains uncertain

1. The house-panel sensor output voltage and polarity have not been measured.
   A PIR module powered at 5 V is not automatically safe for a 3.3 V GPIO.
2. P9.21/GPIO0_3 is a documented Black mapping, but its availability on the
   exact board revision and deployed cape/overlay set has not been checked.
3. It is not known whether the deployed kernel is upstream v6.12, a TI/Beagle
   derivative, or configured with all AMx3 PM components needed by its firmware.
4. Presence of `am335x-pm-firmware.elf` and `am335x-bone-scale-data.bin`, a
   successful WKUP-M3 boot, and a reported firmware version have not been seen.
5. The selected value in `/sys/power/mem_sleep` and the actual state reached by
   `echo mem > /sys/power/state` have not been observed. TI SDK documentation
   cannot be assumed to describe every mainline or distribution configuration.
6. Both-edge runtime IRQ delivery and both-edge wake acceptance are separate
   questions. The M3 reports the bank-level GPIO0 wake source; it does not prove
   which transition or header pin caused it without correlated evidence.
7. A PIR can remain asserted for seconds. Tests must cover suspend entered while
   inactive and while already active, then both assertion and deassertion. An
   edge-triggered design cannot wake merely because the line is statically high.

## Experiment decision table

| Strategy | Strength | Limitation | Decision |
| --- | --- | --- | --- |
| Stock `gpio-keys` | Mature in-tree GPIO/input/wakeup implementation; TI recommends it for GPIO wake | Generic naming and behavior; still depends on the same unproven platform path | Run first as `stock_gpio_keys_wake_baseline` |
| Custom platform/input driver | Explicit `EV_SW` presence contract, initial state, hard wake-setup failures, concise diagnostics | Adds local code and maintenance; cannot solve SoC/firmware/pad limitations | Run second as `custom_pir_input_driver` |
| Userspace GPIO polling | Simple awake-state prototype | Cannot arm a reliable kernel wake IRQ and stops when the CPU sleeps | Do not use for system wake validation |
| Non-GPIO wake input | May be appropriate if GPIO0 proves unavailable | Requires different wiring/hardware and changes experiment scope | Revisit only after GPIO0 evidence is collected |

## Power-state experiment ladder

“Lowest power” is a measured product result, not the name of a Linux state.
The cape, backlight circuit, PIR, regulators, network state, kernel, firmware,
and supply boundary all contribute. Test in this order and retain the first
shallower mode as a recovery/control case when advancing:

| Mode | Expected behavior | Network | Wake route | Decision gate |
| --- | --- | --- | --- | --- |
| Awake, backlight zero | CPU and services run; display illumination is commanded off | Tailnet remains available | Normal input event | Establish the electrical baseline and prove “brightness 0” really extinguishes the cape |
| `freeze` | Tasks freeze; platform remains comparatively shallow | Unavailable during freeze, must recover after resume | Normal Linux wakeup source | Measure current and wake reliability before deeper suspend |
| `mem` / target DeepSleep0 | AM335x platform firmware coordinates the deep state | Unavailable; Tailscale must reconnect after resume | GPIO0 + IRQ hierarchy + WKUP-M3 | Require repeated GPIO-attributed wake and measured current/latency |
| Power-off/load removal | Potentially lower than SoC suspend | Unavailable; cold boot required | External always-on controller or power circuitry | Out of current scope: the PIR cannot cold-boot an unpowered board without additional hardware |

Also measure active idle with CPU frequency/idle policy enabled, but do not
confuse runtime idle tuning with a verified system suspend state. The supervisor
must request exactly the configured state and must never silently substitute a
shallower mode.

## Kernel and implementation matrix

Run both presence implementations against two named software environments:

1. A verified non-flasher BeagleBoard/Debian 6.12 characterization image. This
   is the already source-validated driver API baseline and the safest place to
   inventory the unknown cape.
2. The pinned Buildroot Linux 6.18.21 candidate. Rebuild both overlays and the
   custom module against that exact kernel, then repeat awake IRQ, `freeze`, and
   `mem` trials.

If the environments differ, preserve both result sets before changing either
kernel. A successful 6.12 trial does not prove the Buildroot kernel, and a
successful 6.18 build does not prove the cape or wake path.

## Evidence required before calling wake supported

A support claim requires, at minimum, exact hardware/pin documentation,
measured safe electrical levels, a reproducible target kernel/config/firmware
set, successful base-plus-overlay resolution, awake-state events on both edges,
confirmed standard wake policy, confirmed DeepSleep0 entry, M3/PM logs naming
GPIO0 as the wake source, and repeated wake cycles from both relevant starting
states. None of that hardware evidence exists in this repository yet.
