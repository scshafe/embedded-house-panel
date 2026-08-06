# ADR 0002: Prefer Buildroot After Debian Hardware Bring-Up

**Status:** proposed; acceptance requires the hardware baseline

**Date:** 2026-08-05

## Context

The target likely has 4 GB eMMC and 512 MB RAM. The product is a single-purpose appliance that benefits from one reproducible kernel/rootfs/toolchain definition. Current Buildroot 2026.05.1 provides a [BeagleBone defconfig](https://gitlab.com/buildroot.org/buildroot/-/raw/2026.05.1/configs/beaglebone_defconfig) and a [Tailscale package](https://gitlab.com/buildroot.org/buildroot/-/raw/2026.05.1/package/tailscale/Config.in).

## Proposed decision

Use official BeagleBoard Debian on microSD only for non-destructive discovery. If the cape, PM, and application experiments succeed, use a pinned Buildroot 2026.05.1 external tree as the first appliance baseline.

## Alternatives

- Debian appliance: fastest package iteration, larger and more mutable.
- Yocto/OpenEmbedded: strong fleet/layer model, disproportionate initial complexity.
- Buildroot: compact and direct, but this project owns update/rollback and package integration.

## Evidence gate

Accept only after the hardware decision record confirms display/touch support, a repeatable wake path, measured memory/image size, and a viable update/recovery story.
