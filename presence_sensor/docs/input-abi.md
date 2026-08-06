# Presence input ABI and firmware interface

Status: proposed ABI for an unverified hardware experiment.

## Linux input ABI

Both experiments intentionally expose the same semantic event:

| Field | Value | Meaning |
| --- | --- | --- |
| Event type | `EV_SW` (`0x05`) | Persistent switch-like state, not a pulse journal |
| Event code | `SW_FRONT_PROXIMITY` (`0x0b`) | Presence sensor active/inactive |
| Value | `1` | Logical GPIO asserted; presence active |
| Value | `0` | Logical GPIO deasserted; presence inactive |
| Synchronization | `SYN_REPORT` after each sample | Completes the state report |

The GPIO descriptor applies Device Tree polarity before the event is emitted.
The supplied overlays assume `GPIO_ACTIVE_HIGH`; that assumption must be
checked against the actual PIR module and any level-shifting circuitry.

The custom driver samples once after input registration and after every rising
or falling IRQ. Linux input may suppress an unchanged switch value. A pulse
that begins and ends before the threaded IRQ samples the line is not guaranteed
to be preserved; this ABI represents current state rather than every edge.

No `/dev/input/eventN` number is stable. Discover the device by input name:

- Custom: `Beagle House PIR custom experiment`
- Baseline: `Beagle House gpio-keys wake baseline`

There are no custom ioctls or custom sysfs attributes. Standard evdev and input
interfaces are the userspace contract.

## Custom Device Tree interface

The experimental schema is
`../experiments/custom_pir_input_driver/Documentation/devicetree/bindings/input/beagle,house-pir.yaml`.

- `compatible = "beagle,house-pir"` selects the custom platform driver.
- `presence-gpios` is mandatory and contains exactly one GPIO descriptor.
- `label` is an optional input-device name; malformed values fail probe.
- `wakeup-source` is optional. When present, probe requires system-sleep PM,
  creates the standard `power/wakeup` control, and attaches the GPIO IRQ as the
  device wake IRQ. Wake-IRQ setup failure fails probe rather than degrading to
  a runtime-only input.

The wake policy remains controllable through the standard device power sysfs
attribute (`.../power/wakeup`). The exact platform-device path is not an ABI;
locate it from sysfs or the kernel log.

## Baseline Device Tree interface

The baseline follows the upstream `gpio-keys` binding. It sets
`linux,input-type = <EV_SW>`, `linux,code = <SW_FRONT_PROXIMITY>`,
`wakeup-source`, and `wakeup-event-action = <EV_ACT_ANY>` on its child node.
Do not apply the baseline and custom overlays simultaneously because both claim
the same GPIO and pad.
