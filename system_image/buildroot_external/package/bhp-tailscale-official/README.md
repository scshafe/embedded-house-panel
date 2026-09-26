# Buildroot package: pinned official Tailscale

ADR 0010 installs Tailscale `1.102.2` on AArch64 panel images from Tailscale's
official static `arm64` archive. This keeps the panel at the same version as
Lexinet without upgrading Buildroot's host Go.

Public Buildroot interface:

- Kconfig symbol: `BR2_PACKAGE_BHP_TAILSCALE_OFFICIAL`. It excludes Buildroot's
  source-built `BR2_PACKAGE_TAILSCALE` and depends on `BR2_aarch64`.
- package target: `bhp-tailscale-official`
- installed files: `/usr/bin/tailscale`, `/usr/bin/tailscaled` and the
  `/usr/sbin/tailscaled` link, the same layout as Buildroot's package, so
  `S50tailscaled` is unchanged.
- kernel fixups: the IPv6, nftables and TUN options that Buildroot's package enables.

`bhp-tailscale-official.hash` pins the archive's SHA-256, taken from the official
`.sha256` file. `BR2_DOWNLOAD_FORCE_CHECK_HASHES` fails the build on any
mismatch, and `make verify-docs` fails if the version, the hash line and ADR 0010
disagree. Changing the version is one reviewed change that updates Lexinet's pin,
this package and ADR 0010 together.

Node state lives in `/var/lib/tailscale` (mode `0700`), provisioned outside the
image. Auth keys, node state and certificates never enter Git, images, logs or
command arguments. The BeagleBone history defconfigs keep Buildroot's package.
