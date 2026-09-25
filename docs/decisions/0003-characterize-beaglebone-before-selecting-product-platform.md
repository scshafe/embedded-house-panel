# ADR 0003: Characterize BeagleBone Before Selecting Product Platform

**Status:** accepted; the characterize-before-purchase decision is superseded by
[ADR 0008](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)

**Date:** 2026-08-05

## Context

The already-owned BeagleBone Black can prove the kernel input/wakeup path,
display behavior, supervisor policy, and measurement method. The eventual
product now also targets cordless power, onboard Wi-Fi, microphone input, local
STT, and lightweight gesture recognition. Those requirements may justify a
different application processor, but no candidate has been measured as a
complete system.

## Decision

- Execute the BeagleBone hardware baseline before buying or selecting a
  replacement platform.
- Treat the Black as the current characterization platform, not an implicit
  final product selection.
- Do not prefer BeagleBone AI-64 for the cordless role; it adds compute without
  resolving onboard Wi-Fi or documented product-level low-power wake.
- Use i.MX 8M Plus as the practical comparison baseline, BeagleY-AI as the
  open-hardware AI challenger, BeaglePlay as the lower-compute open option, and
  QCS6490 only after its sleep/wake claim passes a documentation gate.
- Preserve platform variation behind the existing policy seams and the
  proposed `PowerController`, media, and inference seams.
- Defer the final platform ADR until battery-boundary power, wake reliability,
  wake-to-frame latency, STT, gesture, thermal, storage, and software-stack
  evidence exist.

## Alternatives considered

- Buy AI-64 immediately: familiar headers and strong accelerators, but weak fit
  for the battery/Wi-Fi priority.
- Restart directly on i.MX 8M Plus: likely product direction, but discards a
  zero-cost way to establish requirements and preserve the old eMMC/cape facts.
- Freeze the design on Black: fastest reuse, but forecloses credible local STT
  and vision and makes external Wi-Fi permanent.
- Compare many boards before touching hardware: produces more specifications,
  not the missing whole-system evidence.

## Consequences

- BeagleBone bring-up work proceeds now and remains useful even if the final
  board changes.
- The 4D cape may support the first working panel but is not assumed portable.
- The current Buildroot profile remains a Black-specific candidate; other
  boards begin with their supported Yocto/TI/Qualcomm images.
- Procurement waits for explicit runtime, wake-latency, listening-mode, and
  screen-duty-cycle requirements plus a common test contract.
- A later ADR must link measured evidence rather than repeat vendor claims.

## References

- [Compute platform assessment](../research/compute-platform-assessment.md)
- [Proposed power-domain architecture](../architecture/power-domain-architecture.md)
- [AM335x sleep/wake strategies](../research/sleep-wake-strategies.md)
- [Hardware assumptions](../hardware/hardware-assumptions.md)

**Amended 2026-09-24:** the owned BeagleBone Black was lost, which removes the
zero-cost reason for characterizing it first. [ADR 0008](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
adopts the FRDM i.MX 8M Plus board and Waveshare 7inch DSI LCD (C) as the
development platform. The platform ranking, the AI-64 rejection, the deferred
final-product ADR, and the evidence requirements above remain in force.
