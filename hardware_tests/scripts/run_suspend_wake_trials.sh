#!/bin/sh
# Requests explicit Linux suspend cycles and records before/after evidence.
# Inputs: --mode freeze|mem --count N --output DIRECTORY --execute.
# Side effects: writes /sys/power/state and suspends the board. NOT idempotent.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../../scripts/structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=hardware_tests.suspend_wake
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

mode=""
count=1
output_directory=""
execute=false

usage()
{
  printf '%s\n' "Usage: $0 --mode freeze|mem --count N --output DIRECTORY [--execute]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      mode=$2
      shift 2
      ;;
    --count)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      count=$2
      shift 2
      ;;
    --output)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      output_directory=$2
      shift 2
      ;;
    --execute) execute=true; shift ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

case "$mode" in freeze|mem) ;; *) usage >&2; exit 2 ;; esac
case "$count" in *[!0-9]*|0|'') usage >&2; exit 2 ;; esac
[ -n "$output_directory" ] || { usage >&2; exit 2; }
case "$output_directory" in /|/etc|/usr|/var|/home|.) printf '%s\n' "unsafe output directory" >&2; exit 1 ;; esac
if [ -d "$output_directory" ] &&
   [ -n "$(find "$output_directory" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
  bhp_fail HARDWARE_TEST.OUTPUT_NOT_EMPTY "Output directory is not empty" "$TRACE_ID" >&2
  exit 1
fi

available_states=$(sed -n '1p' /sys/power/state)
printf '%s\n' "$available_states" | grep -Eq "(^| )$mode( |$)" || {
  printf '%s\n' "requested state is not advertised: $mode" >&2
  exit 1
}
if [ "$execute" != true ]; then
  bhp_log info suspend_wake_dry_run "$TRACE_ID" mode "$mode" count "$count"
  exit 0
fi
[ "$(id -u)" -eq 0 ] || { printf '%s\n' "run as root" >&2; exit 1; }

mkdir -p "$output_directory"
trial=1
while [ "$trial" -le "$count" ]; do
  trial_file=$output_directory/trial-$(printf '%04d' "$trial").txt
  {
    printf 'trial=%s\nmode=%s\nbefore_utc=%s\n' "$trial" "$mode" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if [ -r /sys/kernel/debug/wakeup_sources ]; then
      printf '%s\n' before_wakeup_sources
      sed -n '1,240p' /sys/kernel/debug/wakeup_sources
    fi
  } >"$trial_file"

  printf '%s' "$mode" >/sys/power/state

  {
    printf 'after_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if [ -r /sys/kernel/debug/wakeup_sources ]; then
      printf '%s\n' after_wakeup_sources
      sed -n '1,240p' /sys/kernel/debug/wakeup_sources
    fi
  } >>"$trial_file"
  trial=$((trial + 1))
done

bhp_log info suspend_wake_trials_complete "$TRACE_ID" mode "$mode" count "$count" attribution "requires_manual_wakeup_source_review"
