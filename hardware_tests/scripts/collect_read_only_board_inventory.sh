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

capture_bounded_file()
{
  source_path=$1
  maximum_lines=$2
  stop_line=$((maximum_lines + 1))

  printf 'source_path=%s\ncapture_limit_lines=%s\n' "$source_path" "$maximum_lines"
  if [ ! -e "$source_path" ]; then
    printf 'source_status=missing\n'
    return 1
  fi
  if [ ! -r "$source_path" ]; then
    printf 'source_status=unreadable\n'
    return 1
  fi
  if ! sed -n "1,${maximum_lines}p;${stop_line}q" "$source_path"; then
    printf 'source_status=read_failed\n'
    return 1
  fi
  printf 'source_status=readable\n'
}

capture_suspend_stats()
{
  suspend_stats_root=/sys/power/suspend_stats
  maximum_files=32
  maximum_lines_per_file=64
  observed_files=0
  aggregate_status=0

  printf 'source_root=%s\n' "$suspend_stats_root"
  printf 'capture_limit_files=%s\ncapture_limit_lines_per_file=%s\n' \
    "$maximum_files" "$maximum_lines_per_file"
  if [ ! -d "$suspend_stats_root" ]; then
    printf 'source_status=missing\n'
    return 1
  fi

  for source_path in "$suspend_stats_root"/*; do
    [ -e "$source_path" ] || continue
    [ -d "$source_path" ] && continue
    observed_files=$((observed_files + 1))
    if [ "$observed_files" -gt "$maximum_files" ]; then
      printf 'source_status=truncated_at_file_limit\n'
      break
    fi
    printf '\nsource_path=%s\n' "$source_path"
    if [ ! -r "$source_path" ]; then
      printf 'source_status=unreadable\n'
      aggregate_status=1
      continue
    fi
    if capture_bounded_file "$source_path" "$maximum_lines_per_file"; then
      :
    else
      aggregate_status=1
    fi
  done

  if [ "$observed_files" -eq 0 ]; then
    printf 'source_status=no_stat_files\n'
    return 1
  fi
  return "$aggregate_status"
}

capture_pinctrl_files()
{
  pinctrl_filename=$1
  pinctrl_root=/sys/kernel/debug/pinctrl
  maximum_controllers=16
  maximum_lines_per_file=512
  observed_controllers=0
  aggregate_status=0

  printf 'source_root=%s\nsource_filename=%s\n' "$pinctrl_root" "$pinctrl_filename"
  printf 'capture_limit_controllers=%s\ncapture_limit_lines_per_file=%s\n' \
    "$maximum_controllers" "$maximum_lines_per_file"
  if [ ! -d "$pinctrl_root" ]; then
    printf 'source_status=missing\n'
    return 1
  fi

  for source_path in "$pinctrl_root"/*/"$pinctrl_filename"; do
    [ -e "$source_path" ] || continue
    observed_controllers=$((observed_controllers + 1))
    if [ "$observed_controllers" -gt "$maximum_controllers" ]; then
      printf 'source_status=truncated_at_controller_limit\n'
      break
    fi
    printf '\ncontroller_source=%s\n' "$source_path"
    if [ ! -r "$source_path" ]; then
      printf 'source_status=unreadable\n'
      aggregate_status=1
      continue
    fi
    if capture_bounded_file "$source_path" "$maximum_lines_per_file"; then
      :
    else
      aggregate_status=1
    fi
  done

  if [ "$observed_controllers" -eq 0 ]; then
    printf 'source_status=no_matching_controller_files\n'
    return 1
  fi
  return "$aggregate_status"
}

capture_cape_eeprom_candidates()
{
  i2c_devices_root=/sys/bus/i2c/devices
  maximum_candidates=16
  maximum_preview_bytes=256
  observed_candidates=0
  readable_candidates=0
  aggregate_status=0

  printf 'source_root=%s\n' "$i2c_devices_root"
  printf 'capture_limit_candidates=%s\ncapture_limit_bytes_per_candidate=%s\n' \
    "$maximum_candidates" "$maximum_preview_bytes"
  printf '%s\n' 'identity_policy=eeprom paths are candidates only; no board or cape identity is inferred'
  if [ ! -d "$i2c_devices_root" ]; then
    printf 'source_status=missing\n'
    return 1
  fi

  for source_path in "$i2c_devices_root"/*/eeprom; do
    [ -e "$source_path" ] || continue
    observed_candidates=$((observed_candidates + 1))
    if [ "$observed_candidates" -gt "$maximum_candidates" ]; then
      printf 'source_status=truncated_at_candidate_limit\n'
      break
    fi
    printf '\ncandidate_path=%s\n' "$source_path"
    if [ ! -r "$source_path" ]; then
      printf 'source_status=unreadable\n'
      aggregate_status=1
      continue
    fi
    if od -An -tx1 -N "$maximum_preview_bytes" "$source_path"; then
      printf 'source_status=readable\n'
      readable_candidates=$((readable_candidates + 1))
    else
      printf 'source_status=read_failed\n'
      aggregate_status=1
    fi
  done

  if [ "$observed_candidates" -eq 0 ]; then
    printf 'source_status=no_eeprom_candidates\n'
    return 1
  fi
  if [ "$readable_candidates" -eq 0 ]; then
    return 1
  fi
  return "$aggregate_status"
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
capture interrupts capture_bounded_file /proc/interrupts 640
capture power_mem_sleep capture_bounded_file /sys/power/mem_sleep 4
capture power_suspend_stats capture_suspend_stats
capture wakeup_sources sh -c 'test -r /sys/kernel/debug/wakeup_sources && sed -n "1,240p" /sys/kernel/debug/wakeup_sources'
capture pinctrl_pins capture_pinctrl_files pins
capture pinctrl_pinmux capture_pinctrl_files pinmux-pins
capture pinctrl_pinconf capture_pinctrl_files pinconf-pins
capture cape_eeprom_candidates capture_cape_eeprom_candidates
capture backlights sh -c 'find /sys/class/backlight -maxdepth 2 -type f \( -name brightness -o -name actual_brightness -o -name max_brightness \) -print -exec sed -n "1p" {} \; 2>/dev/null'
capture drm sh -c 'find /sys/class/drm -maxdepth 2 -type f \( -name status -o -name modes -o -name enabled \) -print -exec sed -n "1,40p" {} \; 2>/dev/null'
capture input_devices sh -c 'test -r /proc/bus/input/devices && sed -n "1,320p" /proc/bus/input/devices'
capture gpio_chips sh -c 'command -v gpioinfo >/dev/null 2>&1 && gpioinfo'
capture kernel_messages sh -c 'dmesg 2>/dev/null | tail -n 500'

for captured_file in "$output_directory"/*.txt; do
  printf '%s  %s\n' "$(bhp_sha256_file "$captured_file")" "$(basename "$captured_file")"
done >"$output_directory/SHA256SUMS"
bhp_log info inventory_capture_complete "$TRACE_ID" output_directory "$output_directory"
