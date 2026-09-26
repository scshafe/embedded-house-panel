# Virtual panel validation

**Amended 2026-09-25:** the `virtual-panel` branch adds three executable display
tiers under [ADR 0009](../decisions/0009-add-an-isolated-virtual-panel-target.md).
All evidence is `simulation_not_hardware_evidence`. The
[machine-readable validation record](virtual-panel-validation.json) summarizes
the executed checks. `make verify` and `make verify-docs` passed; the guest
passed all 37 Linux CTests.

The host is local Apple Silicon, macOS 26.6.1, with 24 GiB RAM. QEMU 11.1.1 was
built and signed inside the ignored checkout-local `.build/qemu/`; no QEMU
Homebrew installation or VM manager is required. The Debian 13 arm64 guest uses
HVF, four virtual CPUs, 4 GiB RAM, a sparse disk, Cocoa, virtio-gpu at 1024×600,
virtio tablet and multi-touch devices, and the real Linux 6.12.107 DRM/evdev APIs.
Guest chrony reports synchronized time. Suspend is disabled.

Use the exact commands in [virtual-panel operations](../../scripts/sim/README.md).
The native SDL and QEMU Cocoa windows were both opened on the Mac. A native
Cocoa click flipped the displayed card and produced a fresh supervisor-observed
touch commitment. QMP separately exercised single-touch and Type-B contacts.
Turning off the uinput PIR reduced the real simulated-backlight sysfs value to
zero; returning presence restored the day surface.

| Evidence | Location under the checkout | Scope |
| --- | --- | --- |
| Headless renders and exact-pixel comparison | `.build/screenshots/report.json` and `contact-sheet.html` | 29 deterministic shared LVGL states; zero pixel tolerance |
| Actual native windows | `.build/sim/native/report.json` and `contact-sheet.html` | All 29 scenarios captured through SDL2 and matched exactly |
| Real guest integration | `.build/sim/vm/report.json`, `contact-sheet.html`, `guest-services.jsonl` | 29 scenarios, authenticated lifecycle, DRM completion, both touch protocols, PIR blanking |
| Native Mac pointer | `.build/sim/native-pointer-proof.json` | Click in the signed native app flipped the card and synchronized over verified fixture HTTPS |
| Physical Mac pointer routed by Cocoa | `.build/sim/cocoa-pointer-proof.json` | Fresh accepted contact and card-face change in the running guest |
| Repeated runtime replacement | `.build/sim/runtime-refresh-test.json` | Two reload cycles, changed service PIDs, re-created kernel backlight |
| Guest build and tests | `.build/sim/provision.log` | Native aarch64 build and Linux socket credential tests |

The 27 ordinary guest captures match the headless goldens exactly. Two declared
scenario differences have semantic assertions: grace uses the actual simulated
backlight policy (70%→60%), and clock-untrusted visibly labels unavailable fixture
clock data while preserving the supervisor's NTP-backed lifecycle. The native
fixtures use scripted lifecycle snapshots and 80%→40% grace.

Initial golden review images are committed in
[the baseline review](../../panel_ui/tests/golden/README.md). Native and guest
contact sheets are generated artifacts, with scenario names and full-size links
in their HTML companions. The JSON reports retain each outcome and its evidence
label. TLS keys, VM disks, SSH identity, study cache and logs remain ignored.

The scoped [virtual-panel plan](../../.claude/plans/2026-09-25-virtual-panel.md)
records completion. Broader original roadmap milestones remain open where they
require real Lexinet deployment, product decisions or hardware. The implemented
host paths include durable initial-session creation and retry identities, atomic
cache/outbox recovery, a cancellable HTTPS worker, the canonical study reducer,
shared surfaces, normalized supervisor touch, and fake-only command admission.
Agenda is synthetic presentation only. The fake broker captures no microphone.

Remaining gates are the actual Lexinet/Tailnet deployment and real service
credentials; production holder-claim/media IPC and independent microphone closure;
AGENDA-011 and agenda feasibility Step 1; reviewed provider/companion integration;
and every physical FRDM display, GT911, backlight calibration, audio, PIR, power,
suspend and wake gate. QEMU does not model the FRDM wiring or current consumption.
The production display backend still fails dark and production services remain
disabled. The optional Buildroot QEMU image was not built.
