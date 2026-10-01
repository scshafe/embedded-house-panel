# Development environment

This domain owns read-only dependency checks and explicit host bootstraps.
`check_host_dependencies.sh` supports host, Buildroot and deployment profiles.
`bootstrap_debian_build_host.sh` and `bootstrap_arch_build_host.sh` install
packages only with `--execute`; their default is a dry run. The Arch dry run is
portable so a migration can be reviewed before reaching the destination.

The Arch bootstrap installs the compiler, shared renderer/SDL, verification and
Buildroot prerequisite packages using interactive `pacman -Syu --needed`. It
requires Arch Linux x86_64 for execution. It does not configure Tailnet, enable
services, select a board serial device, install a cross SDK or flash hardware.

See [Arch workstation setup](../../docs/operations/arch-development.md) and
[the general environment contract](../../docs/operations/development-environment.md).
SDK creation remains in `build_aarch64_verification_sysroot.sh`; do not copy
Mac build trees or reconstruct a target sysroot from a running board.
