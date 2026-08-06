#!/bin/sh
# Installs the documented build and verification tools on Debian/Ubuntu.
# Inputs: --dry-run (default) or --execute.
# Side effects: --execute updates apt metadata and installs host packages.
# Idempotent: apt converges already-installed packages.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.bootstrap_debian_host
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

mode=dry-run
case "${1:-}" in
  ''|--dry-run) ;;
  --execute) mode=execute ;;
  --help|-h)
    printf '%s\n' "Usage: $0 [--dry-run|--execute]"
    exit 0
    ;;
  *)
    printf '%s\n' "Usage: $0 [--dry-run|--execute]" >&2
    exit 2
    ;;
esac

test "$(uname -s)" = Linux || {
  bhp_fail BOOTSTRAP.LINUX_REQUIRED "Use a Debian or Ubuntu Linux build host" "$TRACE_ID"
  exit 1
}
test "$(uname -m)" = x86_64 || {
  bhp_fail BOOTSTRAP.X86_64_REQUIRED "The selected Bootlin toolchain requires an x86_64 build host" "$TRACE_ID"
  exit 1
}
test -r /etc/os-release || {
  bhp_fail BOOTSTRAP.OS_RELEASE_MISSING "/etc/os-release is required" "$TRACE_ID"
  exit 1
}
# This distribution-owned file contains identifiers, not credentials.
# shellcheck source=/dev/null
. /etc/os-release
case "${ID:-}" in
  debian|ubuntu) ;;
  *)
    bhp_fail BOOTSTRAP.DISTRIBUTION_UNSUPPORTED "${ID:-unknown}" "$TRACE_ID"
    exit 1
    ;;
esac

packages="build-essential bc bison flex git cmake ninja-build pkg-config rsync openssh-client shellcheck ripgrep device-tree-compiler cpio file patch perl python3 unzip wget curl xz-utils libssl-dev libncurses-dev"

if [ "$mode" = dry-run ]; then
  bhp_log info bootstrap_dry_run "$TRACE_ID" distribution "$ID" packages "$packages"
  exit 0
fi

command -v apt-get >/dev/null 2>&1 || {
  bhp_fail BOOTSTRAP.APT_GET_MISSING "apt-get was not found" "$TRACE_ID"
  exit 1
}
if [ "$(id -u)" -eq 0 ]; then
  privilege_command=""
else
  command -v sudo >/dev/null 2>&1 || {
    bhp_fail BOOTSTRAP.SUDO_MISSING "sudo is required for a non-root operator" "$TRACE_ID"
    exit 1
  }
  privilege_command=sudo
fi

# shellcheck disable=SC2086
$privilege_command apt-get update
# shellcheck disable=SC2086
$privilege_command apt-get install --yes --no-install-recommends $packages
bhp_log info bootstrap_complete "$TRACE_ID" distribution "$ID"
