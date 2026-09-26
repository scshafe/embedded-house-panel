<!-- @canonical virtual-panel-simulation-boundary -->

# ADR 0009: Add an Isolated Virtual Panel Target

**Status:** accepted for simulation under the user's virtual-panel task

**Date:** 2026-09-25

## Decision

Use the canonical application surfaces and reducers in three explicitly named
evidence environments: deterministic headless LVGL screenshots, an opt-in SDL2
Mac simulator, and a Debian AArch64 guest on QEMU's generic `virt` machine.
The guest is an architecture-compatible software integration target, not an
emulation of the FRDM carrier, i.MX8MP peripherals, or Waveshare panel.

The native simulator uses scripted in-process lifecycle inputs and simulated
display completion. The guest runs the real supervisor and neutral UI binaries,
the authenticated version-2 lifecycle socket, supervisor-owned evdev touch,
normalized touch forwarding, and real DRM/KMS completion on virtio-gpu.
Simulator input and test credentials cannot authorize production effects.

The simulation profile has status `simulation`, can never be the active board,
and cannot satisfy any `FRDM-GATE-*`. All results carry
`simulation_not_hardware_evidence`. Backlight is a guest-only Linux backlight
class device; its value is reported separately because it cannot physically dim
the host monitor. PIR is a named guest uinput switch. Guest time remains NTP
synchronized; solar scenarios select explicit simulation coordinates.
Suspend stays disabled until an observed host-triggered simulated wake works.

The physical fail-dark backend remains the production default. Enabling the
guest's explicit DRM backend does not enable FRDM services or provide
`FRDM-GATE-DISPLAY` evidence. UI processes never open raw touch devices, even
when the device is virtual. Lifecycle reverse traffic remains acknowledgement
only; scenario control is a separate simulation-only boundary.

Fixture HTTPS uses an isolated local certificate authority and ignored
credentials. Production TLS verification, Tailnet authorization, exact peer
authentication and release binding remain unchanged. Agenda fixtures use
explicit synthetic civil dates without deciding AGENDA-011. Recording,
providers, personal data and Messages remain disabled.

## Evidence limits

The guest can prove Linux process integration, exact protocol commitments,
evdev parsing and forwarding, KMS page-flip observation, sysfs writes, network
failure behavior and deterministic simulated interactions. It cannot prove
FRDM boot, DSI scanout, GT911 wiring/timing, brightness calibration, physical
darkness, PIR electrical behavior, suspend power, wake attribution on the
board, audio, battery, Wi-Fi or thermal properties.

## Alternatives

A separate mock website would not exercise the application renderer. Running
only QEMU would slow routine visual iteration. Sharing one renderer across
the three tiers gives fast checks and a progressively wider integration
boundary without changing the hardware evidence contract.

## References

- [Virtual-panel plan](../../.claude/plans/2026-09-25-virtual-panel.md)
- [Capability ownership](../architecture/application-capability-ownership.md)
- [Touch provenance](0005-validate-touch-provenance-at-capability-holders.md)
- [Protocol commitments](0007-validate-complete-protocol-v2-commitments.md)
- [FRDM platform](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
