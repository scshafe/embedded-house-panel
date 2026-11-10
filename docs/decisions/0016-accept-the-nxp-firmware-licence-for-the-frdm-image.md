<!-- @canonical nxp-firmware-eula-acceptance -->

# ADR 0016: Accept the NXP Firmware Licence for the FRDM Image

**Status:** accepted. The owner accepted the NXP Software License Agreement
on 2026-11-10, relayed by the oversight session. This closes the
`NXP-FIRMWARE-EULA` human decision (ARRIVAL-15 in
`docs/operations/frdm-bringup.md`) for the parts listed under "What it
covers". The board profile now says
`BHP_BOARD_NXP_FIRMWARE_EULA=accepted-adr-0016`.

**Date:** 2026-11-10

## Context

The project image cannot boot the FRDM i.MX 8M Plus without NXP's LPDDR4
training firmware (`ddr_fw.bin`, from `firmware-imx`). The board's Wi-Fi
needs NXP's IW612 firmware. Both are under NXP's proprietary licence
(`LA_OPT_NXP_Software_License`), not an open-source one.

Buildroot downloads `firmware-imx` as a self-extracting archive and runs it
with `--auto-accept` on every build. Since ADR 0008, the project has
selected neither package until a human decided.

## What it covers

1. **`firmware-imx` 8.27, as Buildroot 2026.05.1 pins it, under
   `LA_OPT_NXP_Software_License` v58 (November 2024).**
   - The archive's `COPYING` hash matches v58.
   - Its `EULA` and `SCR.txt` were not read, because reading them means
     running the self-extracting archive.
2. **The IW612 firmware from `nxp-imx/imx-firmware` tag `lf-6.6.52_2.2.0`,
   under v57 (July 2024).** Its SCR grants Production Use, Section 2.3.

**Not covered:** the IW612 firmware this repository pins. The
`bhp-nxp-iw612-firmware` package fetches tag `lf-6.12.49-2.2.0`, whose
`LICENSE.txt` is **v63 (May 2025)**, read from GitHub on 2026-11-10. So the
fragment that this acceptance turns on selects `firmware-imx` only. The
IW612 firmware stays out of every build until the owner either accepts v63
for `lf-6.12.49-2.2.0` or has the package re-pinned to `lf-6.6.52_2.2.0`.
Until then the image boots and has Ethernet, but no Wi-Fi.

## Conditions (accepted with the licence)

1. **No firmware or build output in Git.**
   - `.gitignore` keeps out the Buildroot trees, images and the NXP
     firmware file names.
   - `scripts/verification/tests/test_panel_nxp_firmware_guards.py` fails if
     a tracked file matches them.
2. **Images holding NXP firmware are never published.** That means any
   image with `ddr_fw.bin` or the IW612 firmware: `sdcard.img`,
   `imx8-boot-sd.bin`, the root file system.
   - No releases and no CI artifacts.
   - The same test fails if a workflow uploads any path other than the
     enclosure's printable parts, or creates a release.
3. **The README says that building the image accepts NXP's licence terms,**
   and links to them. It does not copy the licence text.
4. **NXP's notices stay in the image.** `post-build.sh` copies each NXP
   package's `COPYING`, `LICENSE.txt`, `EULA*` and `SCR*` into
   `/usr/share/licenses/nxp/<package>/`, and fails the build if a package
   that was built has none.

## What else the licence means here

- **No reverse engineering, and no reports about the firmware.** The licence
  bans reverse engineering the firmware and publishing reports or
  benchmarks about it. Project documents describe how the board behaves,
  never the firmware's internals.
- **Each builder accepts for themselves.** Buildroot accepts on every build
  (`--auto-accept`). Anyone who builds this image accepts NXP's terms; this
  ADR is the owner's acceptance only.
- **No flashing.** Building the image on the PC (ARRIVAL-16) is allowed.
  Writing it to the board (ARRIVAL-17, `uuu`) still needs the owner's
  go-ahead, as every flash does.

## Links

- NXP's licence terms: https://www.nxp.com/docs/en/disclaimer/LA_OPT_NXP_SW.html
- The IW612 firmware's licence at the pinned tag:
  https://github.com/nxp-imx/imx-firmware/blob/lf-6.12.49-2.2.0/LICENSE.txt
