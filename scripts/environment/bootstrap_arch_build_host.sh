#!/bin/sh
# @canonical arch_build_host_bootstrap -- Arch development dependencies.
# Inputs: --dry-run (default) or --execute; no arbitrary package arguments.
# Side effects: --execute performs an interactive full pacman upgrade/install.
# Idempotent package selection; never enables services, installs keys or flashes.
set -eu
SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.bootstrap_arch_host
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)
mode=dry-run
usage() { printf '%s\n' "Usage: $0 [--dry-run|--execute]"; }
[ "$#" -le 1 ] || { usage >&2; exit 2; }
case "${1:-}" in
  ''|--dry-run) ;;
  --execute) mode=execute ;;
  --help|-h) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac
# SDL2 compatibility is the official Arch SDL2 provider; no AUR is required.
packages="base-devel git cmake ninja pkgconf python ripgrep shellcheck openssl curl libpng nlohmann-json sdl2-compat libdrm rsync openssh dtc bc bison flex perl file patch cpio unzip wget xz ncurses which picocom usbutils go"
if [ "$mode" = dry-run ]; then
  bhp_log info bootstrap_dry_run "$TRACE_ID" distribution arch packages "$packages" command "sudo pacman -Syu --needed $packages"
  exit 0
fi
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || {
  bhp_fail BOOTSTRAP.ARCH_X86_64_REQUIRED "Execute on an Arch Linux x86_64 host" "$TRACE_ID"; exit 1
}
[ -r /etc/os-release ] || { bhp_fail BOOTSTRAP.OS_RELEASE_MISSING "/etc/os-release is required" "$TRACE_ID"; exit 1; }
# Distribution-owned metadata, never a caller-supplied source file.
# shellcheck source=/dev/null
. /etc/os-release
[ "${ID:-}" = arch ] || { bhp_fail BOOTSTRAP.DISTRIBUTION_UNSUPPORTED "Expected Arch Linux" "$TRACE_ID"; exit 1; }
command -v pacman >/dev/null 2>&1 || { bhp_fail BOOTSTRAP.PACMAN_MISSING "pacman is required" "$TRACE_ID"; exit 1; }
privilege_command=
if [ "$(id -u)" -ne 0 ]; then
  command -v sudo >/dev/null 2>&1 || { bhp_fail BOOTSTRAP.SUDO_MISSING "sudo is required for a non-root operator" "$TRACE_ID"; exit 1; }
  privilege_command=sudo
fi
# Arch does not support partial upgrades. Keep pacman's confirmation interactive.
# shellcheck disable=SC2086
$privilege_command pacman -Syu --needed $packages
bhp_log info bootstrap_complete "$TRACE_ID" distribution arch
