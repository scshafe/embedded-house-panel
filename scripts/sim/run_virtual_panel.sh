#!/bin/sh
# Debian AArch64 QEMU panel. Default prints the launch command as JSON only.
# --execute prepares ignored VM files and opens Cocoa; --prepare-only skips boot.
# No host installs, destructive disk writes, NXP firmware or production changes.
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_directory/virtual_panel_vm.py" "$@"
