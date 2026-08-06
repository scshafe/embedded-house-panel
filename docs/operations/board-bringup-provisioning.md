# Board Bring-up and Provisioning

This route exists only for characterization and recovery. It creates a visibly
marked Buildroot image with root public-key SSH so an operator can provision
root-owned service configuration, load test modules, inspect overlays/sysfs,
and run the hardware procedures. The production defconfig remains SSH-free and
cannot consume the bring-up key input.

This Buildroot recovery route is separate from the first Tailscale
compatibility experiment. Use the
[Debian Tailnet bring-up runbook](debian-tailnet-bringup.md) on the preserved,
official-Debian microSD baseline; do not copy its apt commands or live node
state into a Buildroot image.

## Security boundary

- Root's password stays locked (`BR2_TARGET_ENABLE_ROOT_LOGIN` is unset).
- Dropbear receives `-s`, disabling password authentication for every account.
- Exactly one caller-supplied Ed25519 public key is installed. Private-key
  material never enters this repository, Buildroot output, image, arguments,
  or logs.
- Root public-key SSH is intentionally powerful. Use removable media on a
  controlled wired network, do not expose the image beyond the test LAN, and
  retire it after bring-up.
- The image is marked `bringup-root-public-key-only` in
  `/etc/beaglebone-house-panel/image-profile` and at login. It is ineligible for
  release-candidate evidence.

## Build the profile

Create a dedicated key outside the project and Buildroot trees. Do not reuse a
personal or production key.

```sh
install -d -m 0700 /absolute/private/bhp-bringup-key
ssh-keygen -t ed25519 \
  -f /absolute/private/bhp-bringup-key/id_ed25519 \
  -C beaglebone-house-panel-bringup

export BHP_BUILDROOT_DIR=/absolute/buildroot/buildroot-2026.05.1
export BHP_BUILDROOT_OUTPUT_DIR=/absolute/outputs/bhp-bringup
export BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/absolute/private/bhp-bringup-key/id_ed25519.pub
scripts/build/build_bringup_image.sh
```

Obtain `BHP_BUILDROOT_DIR` with `download_verified_buildroot.sh`; a version-named
directory is not sufficient. The image provenance records the observed
Buildroot tree ID and only associates the pinned archive hash with the exact
canonical extraction.

The output must be disjoint from both source trees and its parent must already
exist. It must never be reused for production; the profile stamp makes such a
switch fail. The wrapper selects only `beaglebone_house_panel_bringup_defconfig`.
Re-running with the same key converges. Supplying a different valid public key
atomically replaces only the image's `authorized_keys` and recorded public
fingerprint; the old key will not authenticate after the rebuilt image boots.

Write `images/sdcard.img` only to positively identified removable media using a
separately reviewed command. No repository script guesses or flashes a block
device. Retain the known-good official Debian card and do not overwrite eMMC
until boot and recovery have been proven.

## Connect and verify

Boot with wired `eth0`, find the DHCP lease through a local recovery console or
router, and pin the dedicated identity at the client:

```sh
ssh -o IdentitiesOnly=yes \
  -i /absolute/private/bhp-bringup-key/id_ed25519 \
  root@BOARD_ADDRESS
```

Before changing the board, verify:

```sh
cat /etc/beaglebone-house-panel/image-profile
cat /etc/beaglebone-house-panel/bringup-authorized-key-fingerprint
```

The first command must print `bringup-root-public-key-only`; compare the second
value with `ssh-keygen -l -E sha256 -f .../id_ed25519.pub`. A mismatch means stop
and re-establish image/media identity.

Root can now copy the three `.env.example` files to their non-example names,
replace every placeholder with observed hardware values, set the documented
owners/modes, load one presence experiment, and run `hardware_tests/`. Service
enablement and device-tree overlay activation remain deliberate on-target
steps; the image does not guess a cape, GPIO, backlight, or wake configuration.

## Provision touch without exposing PIR input

Production and bring-up use the same fail-closed input policy. Every evdev node
starts `root:root` mode `0600`; the vocabulary process is not in the generic
`input` group. Inventory the actual touch event and record both identity fields:

```sh
cat /sys/class/input/eventX/device/name
cat /sys/class/input/eventX/device/phys
```

Provision those exact one-line values as root-owned, non-writable identity
files, then ask mdev to rescan:

```sh
install -o root -g root -m 0644 /tmp/verified-touch.name \
  /etc/beaglebone-house-panel/touch-input.name
install -o root -g root -m 0644 /tmp/verified-touch.phys \
  /etc/beaglebone-house-panel/touch-input.phys
mdev -s
```

Only a device matching both lines becomes `root:bhp-touch` mode `0660`. The two
project PIR identities are denied even if accidentally configured as touch;
unknown, duplicate-by-phys, malformed, or unconfigured devices remain root-only.
Every input-add event first revokes all earlier grants and recomputes the one
global match, so adding an identically reported device fails shut. The mdev hook
is deliberately add-only: removal-uevent/sysfs ordering has not been proven on
the target. After removing a duplicate, run `mdev -s` to restore the unique
touch grant; until that rescan, it safely remains root-only.

Static mdev cannot safely identify touch by friendly name alone. If the cape's
driver exposes an empty or non-unique `phys`, do not weaken the rule or add the
UI to `input`; record that result and implement/test a udev-like path-based
classifier or narrow privileged input broker. Until then, touch is deliberately
unavailable.

## Recovery and retirement

Loss of the key requires rebuilding/reflashing the removable bring-up image;
there is no password fallback. Root changes can make this development rootfs
unbootable, so keep inventory/results off-device and preserve known-good media.
After the hardware matrix is captured, shut down, remove the bring-up card, and
boot only an SSH-free production candidate. Never treat the bring-up image as
an update or remote-management mechanism.
