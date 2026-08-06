#!/bin/sh
# @canonical shell_structured_logging -- all repository shell scripts source this file.

bhp_json_escape()
{
  # Script fields are intentionally single-line. Replace control newlines before escaping.
  printf '%s' "$1" | tr '\r\n' '  ' | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g'
}

bhp_new_trace_id()
{
  printf 'shell-%s-%s' "$(date -u +%Y%m%dT%H%M%SZ)" "$$"
}

bhp_log()
{
  level=$1
  message=$2
  trace_id=$3
  shift 3

  timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  printf '{"timestamp":"%s","level":"%s","message":"%s","service":"%s","trace_id":"%s"' \
    "$(bhp_json_escape "$timestamp")" \
    "$(bhp_json_escape "$level")" \
    "$(bhp_json_escape "$message")" \
    "$(bhp_json_escape "${BHP_SCRIPT_SERVICE_NAME:-beaglebone_house_panel.script}")" \
    "$(bhp_json_escape "$trace_id")"

  while [ "$#" -ge 2 ]; do
    key=$1
    value=$2
    shift 2
    printf ',"%s":"%s"' "$(bhp_json_escape "$key")" "$(bhp_json_escape "$value")"
  done
  printf '}\n'
}

bhp_fail()
{
  error_code=$1
  error_message=$2
  trace_id=$3
  bhp_log error operation_failed "$trace_id" \
    error_code "$error_code" \
    error_message "$error_message" \
    stack_trace unavailable
  return 1
}

