# System Image

Owns the reproducible bootloader, kernel, device tree, root filesystem, packages, init lifecycle, users/permissions, and image provenance.

## Baseline

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
- All evdev nodes begin as `root:root` mode `0600`. Since static `mdev` rules
  cannot safely classify a device by evdev identity, an add-event helper grants
  only a provisioned exact touch `name` plus `phys` pair to `bhp-touch` mode
  `0660`; both known PIR identities are hard-denied. `vocabulary-panel` belongs
  to `bhp-touch` and `video`, never the generic `input` group. If the cape
  driver has no unique nonempty `phys`, touch remains root-only until a tested
  udev-like classifier or narrower broker exists. DRM/fbdev nodes remain
  `root:video` mode `0660`. Device removal remains fail-closed; run `mdev -s`
  after removing a duplicate because target removal-event ordering is unproven.
- Tailscale runs in normal kernel TUN mode from an init script; provisioning is a separate operator action.
- Buildroot 2026.05.1 currently packages Tailscale 1.78.1. That is sufficient to exercise image integration, not an approval for a networked product; confirm control-plane support and security status, then pin a reviewed version in the external tree before deployment.
- Root filesystem read-only mode is deferred until a persistent-data partition and recovery/update design are tested.
- No Tailscale auth key, node state, Lexinet credential, Wi-Fi secret, or SSH private key is present in the overlay.
- Application init remains gated until real hardware configuration exists; an invalid adapter path must not create a suspend loop at boot.
- `/etc/beaglebone-house-panel/image-version` records the project version,
  commit (when available), source content state (`clean`, `dirty`,
  `uncommitted`, or `source-archive`), exact project content ID (when built from
  Git), actual Buildroot version, observed Buildroot tree ID, and archive
  identity status. A dirty marker warns that the commit alone cannot reproduce
  the image. `image-profile` is exactly `production` in the canonical image.

## Bring-up profile

`beaglebone_house_panel_bringup_defconfig` is deliberately non-production. It
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
