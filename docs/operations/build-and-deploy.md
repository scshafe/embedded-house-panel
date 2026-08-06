# Build and Deploy Operations

## Build image

```sh
export BHP_BUILDROOT_DIR=/absolute/path/to/buildroot-2026.05.1
export BHP_BUILDROOT_OUTPUT_DIR=/absolute/path/to/bhp-output
mkdir -p "$(dirname "$BHP_BUILDROOT_OUTPUT_DIR")"
scripts/build/configure_buildroot.sh
scripts/build/build_buildroot_image.sh
```

The configuration script writes only inside the explicit output directory. Its
parent must already exist, and the resolved output may not equal, contain, or
sit below either the Buildroot source tree or project root. It never flashes
media. An existing nonempty output must carry the same project profile stamp;
production and bring-up can never share or switch an output because Buildroot
does not reliably uninstall packages deselected by a later defconfig. This
incremental path is for development, not release evidence: local
Buildroot package stamps do not automatically notice every outer-repository
change.

## Cross-build applications

```sh
export BHP_BUILDROOT_HOST_DIR="$BHP_BUILDROOT_OUTPUT_DIR/host"
export BHP_BUILDROOT_CROSS_PREFIX=arm-buildroot-linux-gnueabihf
scripts/build/cross_build_applications.sh
```

Buildroot package builds are the release path. Manual SDK cross-builds are for
iteration. The toolchain file pins the compiler, archiver, sysroot, and
`pkg-config` to `output/host`; CMake target packages, headers, and libraries
cannot fall back to the workstation. If an existing CMake cache names another
SDK, move that explicit build directory aside and configure again.

Each run installs into a fresh temporary stage, creates `MANIFEST.sha256`, and
publishes it only after proving the manifest is the exact regular-file set.
The previous stage is retired by removing only its verified manifest entries;
the script never recursively deletes an unknown directory.

## Upload iteration bundle

```sh
export BHP_BOARD_SSH_TARGET=debian@board-name
export BHP_BOARD_UPLOAD_DIR=/home/debian/beaglebone-house-panel-staging
scripts/deploy/upload_application_bundle.sh --dry-run
scripts/deploy/upload_application_bundle.sh --execute
```

Upload stages files under an unprivileged explicit directory; it does not
install system services, load a module, modify the bootloader, or flash
storage. The uploader first proves that no unmanifested local file exists,
then writes to
`BHP_BOARD_UPLOAD_DIR/bundles/<manifest-sha256>/`. Synchronization may remove
stale files only inside that content-addressed leaf; it never deletes sibling
bundles or other staging paths. Promotion is a separate on-target operation so
a failed transfer cannot replace the running application.

The pinned Buildroot base deliberately has no SSH server and disables password
root login. The uploader therefore targets the initial BeagleBoard/Debian
characterization system or the explicitly non-production root-key-only bring-up
image; it is not a hidden remote-management path in the production baseline.
For the bring-up profile, use `root@BOARD_ADDRESS` with a staging leaf below
`/tmp`; root authority is intentional there so the operator can provision
root-owned adapters and run hardware diagnostics. A production update/rollback
mechanism is still an explicit open decision.

## Build a board bring-up image

The separate profile enables Dropbear, rsync, and inventory tools without
changing production:

```sh
export BHP_BUILDROOT_DIR=/absolute/path/to/buildroot-2026.05.1
export BHP_BUILDROOT_OUTPUT_DIR=/absolute/path/to/bhp-bringup-output
export BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/absolute/outside-repo/id_ed25519.pub
scripts/build/build_bringup_image.sh
```

It retains a locked root password and disables all SSH password authentication;
only the caller-supplied public key can authenticate as root. No private key is
copied. Rebuilds atomically converge or rotate that exact authorization. The
profile is visibly marked and cannot pass the production release gate. Follow
[Board Bring-up and Provisioning](board-bringup-provisioning.md) for the full
operational boundary and touch-device provisioning.

## Image deployment

No automated flashing script exists yet. Until the eMMC is imaged and a recovery path is proven, write Buildroot images only to removable test media using a separately reviewed command with an explicit block-device identity. Never infer a device from “the newest disk” or a broad glob.

## Release-candidate build evidence

On the supported Linux x86_64 host, provide the verified archive and create a
dedicated external parent. Do not set `BHP_BUILDROOT_DIR` or
`BHP_BUILDROOT_OUTPUT_DIR`; the verifier refuses caller-provided source and
incremental output:

```sh
export BHP_BUILDROOT_ARCHIVE=/absolute/path/to/buildroot-2026.05.1.tar.xz
export BHP_RELEASE_OUTPUT_PARENT=/absolute/path/to/release-outputs
mkdir -p "$BHP_RELEASE_OUTPUT_PARENT"
unset BHP_BUILDROOT_DIR BHP_BUILDROOT_OUTPUT_DIR BHP_BUILDROOT_PROFILE
make verify-release-candidate
```

The gate hashes the exact Git-visible project content and verifies the pinned
Buildroot archive SHA-256. It atomically creates a new
`bhp-release-<content-id>.<unique>` run, extracts a fresh Buildroot source,
verifies the complete canonical source-tree identity, and creates a disjoint
fresh output beside it. Only then does it configure the SSH-free production
defconfig and build. It re-hashes both project and Buildroot sources afterward;
therefore neither a modified same-version Buildroot tree nor an old
`SITE_METHOD=local` package stamp can be certified. A successful output
contains `release-evidence.env` with project content ID, Buildroot archive and
source IDs, and SD-card SHA-256. Failed runs remain for diagnosis but contain no
evidence file.

The gate requires the SD card image, supervisor, vocabulary terminal and LVGL
renderer, PIR module, and both experiment overlays. It is slower than `make
verify` and is not included there. Passing it is build evidence only: it does
not establish boot, display, GPIO, suspend/wake, cape compatibility, or measured
power behavior.

## Target diagnostics

Run read-only discovery first:

```sh
hardware_tests/scripts/collect_read_only_board_inventory.sh ./hardware_tests/results/manual
```

Suspend tests require an explicit acknowledgement flag and a local recovery path. See `hardware_tests/README.md`.
