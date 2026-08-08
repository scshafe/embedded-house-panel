#!/bin/sh
# Requests explicit Linux suspend cycles and records correlation-scoped evidence.
# Inputs: requested state, matrix cell, expected wake sources, count, output, and --execute.
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
implementation=""
starting_level=""
wake_edge=""
expected_wake_source=""
expected_interrupt_label=""
execute=false

usage()
{
  printf '%s\n' "Usage: $0 --mode freeze|mem --implementation NAME --starting-level asserted|deasserted --wake-edge rising|falling|both|level --expected-wake-source NAME --expected-interrupt-label LABEL --count N --output DIRECTORY [--execute]"
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
    --implementation)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      implementation=$2
      shift 2
      ;;
    --starting-level)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      starting_level=$2
      shift 2
      ;;
    --wake-edge)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      wake_edge=$2
      shift 2
      ;;
    --expected-wake-source)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      expected_wake_source=$2
      shift 2
      ;;
    --expected-interrupt-label)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      expected_interrupt_label=$2
      shift 2
      ;;
    --execute) execute=true; shift ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

case "$mode" in freeze|mem) ;; *) usage >&2; exit 2 ;; esac
case "$count" in *[!0-9]*|0|'') usage >&2; exit 2 ;; esac
case "$starting_level" in asserted|deasserted) ;; *) usage >&2; exit 2 ;; esac
case "$wake_edge" in rising|falling|both|level) ;; *) usage >&2; exit 2 ;; esac
[ -n "$implementation" ] || { usage >&2; exit 2; }
[ -n "$expected_wake_source" ] || { usage >&2; exit 2; }
[ -n "$expected_interrupt_label" ] || { usage >&2; exit 2; }
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
  bhp_log info suspend_wake_dry_run "$TRACE_ID" \
    mode "$mode" \
    implementation "$implementation" \
    starting_level "$starting_level" \
    wake_edge "$wake_edge" \
    expected_wake_source "$expected_wake_source" \
    expected_interrupt_label "$expected_interrupt_label" \
    count "$count"
  exit 0
fi
[ "$(id -u)" -eq 0 ] || { printf '%s\n' "run as root" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || {
  bhp_fail HARDWARE_TEST.PYTHON3_MISSING "python3 is required to create wake manifests" "$TRACE_ID" >&2
  exit 1
}

capture_raw_phase()
{
  capture_phase=$1
  capture_directory=$2
  capture_timestamp=$3
  mkdir -p "$capture_directory/suspend_stats"

  for suspend_stat_name in success fail failed_freeze failed_prepare failed_suspend \
    failed_suspend_late failed_suspend_noirq last_failed_dev last_failed_errno \
    last_failed_step; do
    suspend_stat_path=/sys/power/suspend_stats/$suspend_stat_name
    if [ -r "$suspend_stat_path" ]; then
      sed -n '1p' "$suspend_stat_path" >"$capture_directory/suspend_stats/$suspend_stat_name"
    fi
  done
  if [ -r /proc/interrupts ]; then
    sed -n 'p' /proc/interrupts >"$capture_directory/proc_interrupts"
  fi
  if [ -r /sys/power/mem_sleep ]; then
    sed -n '1p' /sys/power/mem_sleep >"$capture_directory/mem_sleep"
  fi
  if [ -r /sys/kernel/debug/wakeup_sources ]; then
    sed -n 'p' /sys/kernel/debug/wakeup_sources >"$capture_directory/wakeup_sources"
  fi
  printf '%s\n' "$capture_timestamp" >"$capture_directory/captured_at_utc"
  bhp_log info wake_trial_raw_phase_captured "$TRACE_ID" \
    phase "$capture_phase" \
    evidence_directory "$capture_directory"
}

selected_mem_sleep=""
if [ -r /sys/power/mem_sleep ]; then
  selected_mem_sleep=$(sed -n 's/.*\[\([^]]*\)\].*/\1/p' /sys/power/mem_sleep)
fi
if [ "$mode" = mem ] && [ -z "$selected_mem_sleep" ]; then
  bhp_fail HARDWARE_TEST.MEM_SLEEP_SELECTION_UNOBSERVED \
    "Cannot execute a mem trial without an observed selected /sys/power/mem_sleep value" \
    "$TRACE_ID" >&2
  exit 1
fi

mkdir -p "$output_directory"
trial=1
classification_error_count=0
while [ "$trial" -le "$count" ]; do
  trial_number=$(printf '%04d' "$trial")
  trial_directory=$output_directory/trial-$trial_number
  correlation_id=$TRACE_ID-trial-$trial_number
  trial_started_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  mkdir -p "$trial_directory/raw/before" "$trial_directory/raw/after"

  if [ "$mode" = mem ] && [ -n "$selected_mem_sleep" ]; then
    python3 "$SCRIPT_DIRECTORY/classify_wake_trial.py" create-template \
      --output "$trial_directory/observation.json" \
      --expectations-output "$trial_directory/expectations.json" \
      --correlation-id "$correlation_id" \
      --implementation "$implementation" \
      --requested-state "$mode" \
      --selected-mem-sleep "$selected_mem_sleep" \
      --starting-level "$starting_level" \
      --wake-edge "$wake_edge" \
      --expected-wake-source "$expected_wake_source" \
      --expected-interrupt-label "$expected_interrupt_label" \
      --trial-started-at-utc "$trial_started_at_utc"
  else
    python3 "$SCRIPT_DIRECTORY/classify_wake_trial.py" create-template \
      --output "$trial_directory/observation.json" \
      --expectations-output "$trial_directory/expectations.json" \
      --correlation-id "$correlation_id" \
      --implementation "$implementation" \
      --requested-state "$mode" \
      --starting-level "$starting_level" \
      --wake-edge "$wake_edge" \
      --expected-wake-source "$expected_wake_source" \
      --expected-interrupt-label "$expected_interrupt_label" \
      --trial-started-at-utc "$trial_started_at_utc"
  fi
  bhp_log info wake_trial_prepared "$correlation_id" \
    expectations_path "$trial_directory/expectations.json" \
    observation_path "$trial_directory/observation.json" \
    negative_control_path "$trial_directory/negative_control.json" \
    external_observer_action "replace_unobserved_fields_with_measured_evidence"

  before_capture_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  capture_raw_phase before "$trial_directory/raw/before" "$before_capture_at_utc"
  if printf '%s' "$mode" >/sys/power/state; then
    printf '%s\n' returned_after_resume >"$trial_directory/raw/suspend_write_result"
  else
    printf '%s\n' rejected >"$trial_directory/raw/suspend_write_result"
  fi
  process_returned_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  printf '%s\n' "$process_returned_at_utc" >"$trial_directory/raw/process_returned_at_utc"
  after_capture_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  capture_raw_phase after "$trial_directory/raw/after" "$after_capture_at_utc"
  bhp_log info wake_trial_classification_expectations "$correlation_id" \
    implementation "$implementation" \
    requested_state "$mode" \
    selected_mem_sleep "${selected_mem_sleep:-not_applicable}" \
    starting_level "$starting_level" \
    wake_edge "$wake_edge" \
    expected_wake_source "$expected_wake_source" \
    expected_interrupt_label "$expected_interrupt_label" \
    trial_started_at_utc "$trial_started_at_utc" \
    before_capture_at_utc "$before_capture_at_utc" \
    process_returned_at_utc "$process_returned_at_utc" \
    after_capture_at_utc "$after_capture_at_utc"

  if [ "$mode" = mem ]; then
    if classification=$(python3 "$SCRIPT_DIRECTORY/classify_wake_trial.py" classify \
        --evidence-directory "$trial_directory" \
        --expected-correlation-id "$correlation_id" \
        --expected-implementation "$implementation" \
        --expected-requested-state "$mode" \
        --expected-selected-mem-sleep "$selected_mem_sleep" \
        --expected-starting-level "$starting_level" \
        --expected-wake-edge "$wake_edge" \
        --expected-wake-source "$expected_wake_source" \
        --expected-interrupt-label "$expected_interrupt_label" \
        --expected-trial-started-at-utc "$trial_started_at_utc" \
        --expected-before-capture-at-utc "$before_capture_at_utc" \
        --expected-process-returned-at-utc "$process_returned_at_utc" \
        --expected-after-capture-at-utc "$after_capture_at_utc"); then
      classification_status=0
    else
      classification_status=$?
    fi
  else
    if classification=$(python3 "$SCRIPT_DIRECTORY/classify_wake_trial.py" classify \
        --evidence-directory "$trial_directory" \
        --expected-correlation-id "$correlation_id" \
        --expected-implementation "$implementation" \
        --expected-requested-state "$mode" \
        --expected-starting-level "$starting_level" \
        --expected-wake-edge "$wake_edge" \
        --expected-wake-source "$expected_wake_source" \
        --expected-interrupt-label "$expected_interrupt_label" \
        --expected-trial-started-at-utc "$trial_started_at_utc" \
        --expected-before-capture-at-utc "$before_capture_at_utc" \
        --expected-process-returned-at-utc "$process_returned_at_utc" \
        --expected-after-capture-at-utc "$after_capture_at_utc"); then
      classification_status=0
    else
      classification_status=$?
    fi
  fi
  if [ "$classification_status" -ne 0 ]; then
    classification_error_count=$((classification_error_count + 1))
    bhp_log error wake_trial_classification_failed "$correlation_id" \
      exit_status "$classification_status" \
      retained_directory "$trial_directory"
    trial=$((trial + 1))
    continue
  fi
  bhp_log info wake_trial_classified "$correlation_id" \
    classification "$classification" \
    manifest_path "$trial_directory/wake_trial_manifest.json"
  trial=$((trial + 1))
done

summary_status=0
python3 "$SCRIPT_DIRECTORY/classify_wake_trial.py" summarize \
  --trials-directory "$output_directory" \
  --expected-count "$count" \
  --classification-error-count "$classification_error_count" || summary_status=$?
exit "$summary_status"
