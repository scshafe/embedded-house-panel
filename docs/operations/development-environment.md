# Development Environment

## Supported workflow

The final target build uses an x86_64 Linux host or VM. Buildroot requires Linux for the supported build workflow, and the selected official Bootlin ARMv7 toolchain is visible only on an x86_64 host. macOS remains useful for dependency-free policy/UI-core tests, Kconfig syntax checks, and documentation verification, but its resolved `.config` is not evidence that the target toolchain was selected. An ARM64 Linux host requires an explicit switch to a supported toolchain, such as a project-built internal toolchain, and a reviewed defconfig change.

## Host checks

```sh
scripts/environment/check_host_dependencies.sh
```

The script reports required and optional tools without installing anything. A Linux build host needs Git, GNU Make, GCC/G++, CMake, rsync, SSH/SCP/`ssh-keygen`, `dtc`, Python 3, and standard Buildroot host prerequisites.

On a dedicated Debian or Ubuntu build host, review the explicit package set and then opt in to installation:

```sh
scripts/environment/bootstrap_debian_build_host.sh --dry-run
scripts/environment/bootstrap_debian_build_host.sh --execute
scripts/environment/check_host_dependencies.sh --profile buildroot --strict
```

The bootstrap script is never implicit: its default is a dry run, and `--execute` is the only mode that invokes `apt-get`.

## Configuration

Copy values from `config/build-and-deploy.env.example` into your shell or a local ignored `config/device.env`. Scripts do not automatically source arbitrary files; export the required variables explicitly:

```sh
set -a
. ./config/device.env
set +a
```

Review a local file before sourcing it. Never store credentials in it.

## Buildroot checkout

The scaffold pins Buildroot 2026.05.1, the current stable bugfix release as of this document: [official downloads](https://buildroot.org/download.html). Use `scripts/build/download_verified_buildroot.sh` to download and verify the pinned archive into a caller-selected directory. The contract pins both the official archive SHA-256 and a canonical identity of its complete extracted tree.

The downloader never accepts a directory merely because it is named
`buildroot-2026.05.1` or its Makefile prints that version. It hashes every
source path/type, regular-file content and executable bit, empty directory, and
symlink target. An exact existing tree is accepted and receives an adjacent
`.bhp-verified-source` receipt; a modified, partial, or arbitrary tree is
rejected without deletion or replacement. Likewise, an existing archive with
the wrong hash is preserved and rejected. Move conflicting caller-owned paths
aside explicitly before retrying.

Buildroot source, downloads, and output remain outside Git. The external tree under `system_image/buildroot_external/` is the project-owned configuration.

Do not generate `output/`, `dl/`, Python caches, or other files inside the
verified source. Those additions correctly change its identity. Create
Buildroot outputs outside both the project and Buildroot source trees.
The scripts resolve physical parent paths and reject equal, ancestor, descendant,
or symlinked output destinations before creating anything. Incremental outputs
are development conveniences only; the release gate always creates a new
content-addressed output.

For initial target provisioning, use the separate non-production bring-up
profile described in
[Board Bring-up and Provisioning](board-bringup-provisioning.md). It requires a
dedicated Ed25519 public key from outside every source/output tree and never
alters the SSH-free production defconfig.

## Host applications

Without CMake, run the dependency-free compiler verification:

```sh
scripts/verification/verify_host_cpp.sh all
```

With CMake 3.20 or newer:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure
```

## Cross SDK

After a successful Buildroot image build, Buildroot's `output/host` is the
canonical SDK/sysroot. Export its path and detected compiler tuple, then use
the cross-build script. The script requires the SDK's cross compiler tools,
target sysroot, and host `pkg-config` wrapper and clears workstation
CMake/pkg-config hints before configuring. Do not reconstruct a sysroot by
rsyncing `/usr` and `/lib` from the device.
