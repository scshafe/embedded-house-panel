#!/bin/sh
# Captures read-only board/kernel/display/GPIO/PM discovery into a new directory.
# Inputs: one output directory that must not already contain files.
# Side effects: creates text files only in the output directory. Does not load modules or write sysfs.
# Idempotent: no; use a new timestamped directory for each observation.

set -u

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../../scripts/structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../../scripts/file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
BHP_SCRIPT_SERVICE_NAME=hardware_tests.inventory
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ] || [ "$#" -ne 1 ]; then
  printf '%s\n' "Usage: $0 OUTPUT_DIRECTORY"
  exit "$([ "$#" -eq 1 ] && printf 0 || printf 2)"
fi

output_directory=$1
case "$output_directory" in
  /|/etc|/usr|/var|/home|.)
    bhp_fail HARDWARE_TEST.OUTPUT_UNSAFE "Unsafe output directory: $output_directory" "$TRACE_ID" >&2
    exit 1
    ;;
esac
if [ -d "$output_directory" ] && [ -n "$(find "$output_directory" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
  bhp_fail HARDWARE_TEST.OUTPUT_NOT_EMPTY "Output directory is not empty" "$TRACE_ID" >&2
  exit 1
fi
mkdir -p "$output_directory"

capture()
{
  capture_name=$1
  shift
  {
    printf 'command:'
    printf ' %s' "$@"
    printf '\nstarted_at=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    "$@"
    capture_status=$?
    printf '\nexit_status=%s\n' "$capture_status"
  } >"$output_directory/$capture_name.txt" 2>&1
}

capture uname uname -a
capture os_release sh -c 'test -r /etc/os-release && sed -n "1,160p" /etc/os-release'
capture command_line sh -c 'test -r /proc/cmdline && sed -n "1p" /proc/cmdline'
capture device_tree_model sh -c 'test -r /proc/device-tree/model && tr "\000" "\n" </proc/device-tree/model'
capture device_tree_compatible sh -c 'test -r /proc/device-tree/compatible && tr "\000" "\n" </proc/device-tree/compatible'
capture block_devices lsblk -o NAME,PATH,SIZE,TYPE,FSTYPE,MOUNTPOINTS,RO,MODEL
capture mounts findmnt
capture kernel_modules lsmod
capture power_states sh -c 'test -r /sys/power/state && sed -n "1p" /sys/power/state'
capture wakeup_sources sh -c 'test -r /sys/kernel/debug/wakeup_sources && sed -n "1,240p" /sys/kernel/debug/wakeup_sources'
capture backlights sh -c 'find /sys/class/backlight -maxdepth 2 -type f \( -name brightness -o -name actual_brightness -o -name max_brightness \) -print -exec sed -n "1p" {} \; 2>/dev/null'
capture drm sh -c 'find /sys/class/drm -maxdepth 2 -type f \( -name status -o -name modes -o -name enabled \) -print -exec sed -n "1,40p" {} \; 2>/dev/null'
capture input_devices sh -c 'test -r /proc/bus/input/devices && sed -n "1,320p" /proc/bus/input/devices'
capture gpio_chips sh -c 'command -v gpioinfo >/dev/null 2>&1 && gpioinfo'
capture kernel_messages sh -c 'dmesg 2>/dev/null | tail -n 500'

for captured_file in "$output_directory"/*.txt; do
  printf '%s  %s\n' "$(bhp_sha256_file "$captured_file")" "$(basename "$captured_file")"
done >"$output_directory/SHA256SUMS"
bhp_log info inventory_capture_complete "$TRACE_ID" output_directory "$output_directory"
