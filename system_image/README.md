# System Image

Owns the reproducible bootloader, kernel, device tree, root filesystem, packages, init lifecycle, users/permissions, and image provenance.

## Baseline

The board profile selects the defconfig (`BR2_BHP_BOARD_ID`,
`config/boards/<id>.env`; `BHP_BOARD` on the scripts, default the active
profile).

**FRDM i.MX 8M Plus (active, ADR 0008).** `frdm_imx8mp_house_panel_defconfig`
and `frdm_imx8mp_house_panel_bringup_defconfig` derive from Buildroot
2026.05.1 `imx8mp-evk_defconfig`: Cortex-A53, Bootlin AArch64 glibc stable
toolchain, NXP `linux-imx`, `uboot-imx` (`imx8mp_frdm`), and `imx-atf` pinned at
`lf-6.12.49-2.2.0` with hashes, the in-tree
`imx8mp-frdm-waveshare-7inch-c-panel` DTB plus the project overlay merged into
`imx8mp-frdm-bhp-panel.dtb` (`bhp-board-device-tree`), the out-of-tree IW612
`moal` driver, alsa-utils, libgpiod, evtest, libdrm tests, iw, wpa_supplicant,
Tailscale, and LVGL. `board/beaglebone-house-panel/frdm-imx8mp/` holds the
genimage layout (`imx8-boot-sd.bin` at 32 KiB, `boot.vfat`, ext4 root by
PARTUUID), extlinux, the suspend Kconfig fragment, and the kernel patch.

Two gates are deliberate build failures, not defaults:

- `NXP-FIRMWARE-EULA`: `firmware-imx` (LPDDR4 training) and the IW612 firmware
  are NXP-licensed. They are merged only through
  `nxp-firmware-eula.fragment` when the profile's `BHP_BOARD_NXP_FIRMWARE_EULA`
  names an accepted ADR carrying `<!-- @canonical nxp-firmware-eula-acceptance -->`.
  Until then post-image stops with `BHP_IMAGE.NXP_FIRMWARE_EULA_NOT_ACCEPTED`.
  No script accepts this license.
- `FRDM-GATE-PIR`: the project DTB refuses to render while any PIR pin value is
  `UNMEASURED`.

**BeagleBone Black (history).** The BBB defconfigs are marked deprecated and
still evaluate; the text below describes that history baseline.

The scaffold pins Buildroot 2026.05.1 and starts from its official `beaglebone_defconfig`. That upstream baseline selects Cortex-A8, the x86_64-hosted Bootlin ARMv7 glibc toolchain, AM33x CM3 firmware, Linux 6.18.21, U-Boot 2026.04, and an ext4 image. Project packages and a rootfs overlay are supplied through `buildroot_external/`.

This is a buildable experiment baseline, not a declaration that Linux 6.18 or its standard device tree drives the unknown 4D cape. The official BeagleBoard Debian 6.12 image remains the first hardware-characterization environment.

The kernel is already project-built and pinned, but it intentionally starts from the broad upstream `omap2plus` defconfig. Trimming before discovery could remove the unknown cape, touch, PM, or storage dependency. After characterization, capture the live driver/config evidence, create a project-owned minimal config or fragment, and compare boot, device inventory, suspend, and image-size results against this broad recovery baseline. Keep both configurations buildable until the reduced kernel passes the full hardware matrix.

## Build

```sh
scripts/build/download_verified_buildroot.sh --destination /absolute/buildroot-parent
export BHP_BUILDROOT_DIR=/absolute/buildroot-parent/buildroot-2026.05.1
export BHP_BUILDROOT_OUTPUT_DIR=/absolute/bhp-output
mkdir -p "$(dirname "$BHP_BUILDROOT_OUTPUT_DIR")"
scripts/build/configure_buildroot.sh
scripts/build/build_buildroot_image.sh
```

Buildroot source/output/downloads stay outside this repository. The build
scripts never flash media. Configuration reads the canonical, non-mutating
Makefile version declaration and refuses anything other than Buildroot 2026.05.1 before
writing output. The output must be disjoint from both the Buildroot source and
project trees. The post-build hook independently checks Buildroot's exported
version before stamping the image.

The downloader also authenticates the complete extracted source against the
canonical identity derived from the pinned official archive. A pre-existing
same-name or same-version directory is re-hashed, not trusted; modified and
partial trees are rejected in place. Canonical development builds should use
that verified tree. Images record the observed Buildroot source ID and either
the pinned archive SHA-256 or `unverified` for a modified development source.

Incremental output is useful for development but is never release evidence.
The release verifier accepts only the pinned archive, creates a fresh extracted
Buildroot source and a disjoint output named by the project content ID, verifies
both source identities before and after the build, and configures production
from scratch. Consequently, a modified same-version Buildroot tree and
`SITE_METHOD=local` stamps from an older build cannot be certified.

## Image policy

- BusyBox init and `mdev` remain the lightweight default.
- The wired prototype interface requests DHCP on `eth0`; no Wi-Fi profile or
  credential is included. Chrony supplies UTC synchronization before solar/TLS
  behavior can be trusted. Loss of time/network is visible to the application,
  not a reason to guess daytime.
- Password-based root login is disabled and no SSH server is selected in the
  base image. The serial console remains useful for boot logs but is not an
  authentication bypass. The separate, visibly non-production bring-up profile
  below is the only Buildroot SSH iteration route.
- All evdev nodes remain `root:root` mode `0600`, including an exactly
  provisioned touch `name` plus `phys` pair. ADR 0005 reserves raw touch
  observation for the supervisor; the add-event helper revokes legacy group
  grants. The neutral UI has no raw-device group and consumes only normalized
  observations. DRM/fbdev nodes remain `root:video` mode `0660` with no member
  until `panel-ui` joins `video` at the `FRDM-GATE-DISPLAY` cutover. Sound nodes
  are `root:root` mode `0600`; no principal uses the generic `audio` group, and
  the media broker gets its own `bhp-mic` group only when it is packaged. The
  canonical matrix is [service-principals.json](service-principals.json),
  checked by `scripts/verification/verify_service_principals.py`.
- Tailscale runs in normal kernel TUN mode from an init script; provisioning is a separate operator action.
- ADR 0010: FRDM images install Tailscale `1.102.2` from the SHA-256-pinned
  official static archive (`bhp-tailscale-official`), matching Lexinet's pin.
  Buildroot's source-built 1.78.1 package remains only in the BeagleBone history
  defconfigs.
- Root filesystem read-only mode is deferred until a persistent-data partition and recovery/update design are tested.
- No Tailscale auth key, node state, Lexinet credential, Wi-Fi secret, or SSH private key is present in the overlay.
- Application init remains gated until real hardware configuration exists; an
  invalid adapter path must not create a suspend loop at boot. Once explicitly
  enabled, the panel supervisor daemon has three five-second-backoff restart
  attempts before supervision stops visibly; an intentional stop is never
  restarted.
- `/etc/beaglebone-house-panel/image-version` records the project version,
  commit (when available), source content state (`clean`, `dirty`,
  `uncommitted`, or `source-archive`), exact project content ID (when built from
  Git), actual Buildroot version, observed Buildroot tree ID, and archive
  identity status. A dirty marker warns that the commit alone cannot reproduce
  the image. `image-profile` is exactly `production` in the canonical image.

## Bring-up profile

`frdm_imx8mp_house_panel_bringup_defconfig` (and the history
`beaglebone_house_panel_bringup_defconfig`) is deliberately non-production. It
adds Dropbear, rsync, and board-characterization tools while retaining a locked
root password. Dropbear disables every password authentication method; its only
login is root with one caller-supplied Ed25519 public key. This gives initial
bring-up the authority to replace root-owned environment files, load modules,
select overlays, and run hardware diagnostics without adding any SSH server or
authorization material to the production profile.

```sh
export BHP_BUILDROOT_DIR=/absolute/buildroot-parent/buildroot-2026.05.1
export BHP_BUILDROOT_OUTPUT_DIR=/absolute/bhp-bringup-output
export BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/absolute/outside-repo/id_ed25519.pub
scripts/build/build_bringup_image.sh
```

The public key must be a single regular Ed25519 key outside all source/output
trees, and the output must never be shared with production. No private key or
password is copied. Rebuilding with a different public
key atomically replaces only `root/.ssh/authorized_keys` and its recorded public
fingerprint. Use the image on removable media and a controlled wired network,
then retire it after characterization. See
`docs/operations/board-bringup-provisioning.md`.

## Extension points

- Project packages: `buildroot_external/package/`
- Root filesystem additions: `buildroot_external/board/beaglebone-house-panel/rootfs-overlay/`
- Kernel/device-tree changes: keep them as named, test-specific patches or packages until an ADR promotes one.
- Board facts: `config/boards/<id>.env` only; `post-build.sh` installs the
  profile at `/usr/share/beaglebone-house-panel/board-profile.env` and renders
  board-specific service environment examples from
  `board/beaglebone-house-panel/service-environment/`.

**Amended 2026-09-25:** added the FRDM i.MX 8M Plus defconfigs, board-profile
selection, the NXP firmware license gate, and the PIR build gate (ADR 0008).
The BeagleBone baseline is kept as deprecated history. No image has been built
or booted on the FRDM.

**Amended 2026-09-26:** FRDM images use the pinned official Tailscale archive
(ADR 0010). The standalone `vocabulary-panel` principal and its S80 service are
retired, because the vocabulary feature runs beneath `panel_ui`. Sound nodes are
root-only. `service-principals.json` records every installed, reserved and
retired principal and each device holder. This is static packaging evidence; the
Mac-local QEMU image boot proves the running layout.
