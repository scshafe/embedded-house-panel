#!/bin/sh
# Prints one board profile as a single JSON document for agents and scripts.
# Inputs: [--board ID] (default: BHP_BOARD, else the single active profile),
#         [--require KEY] (exit 3 unless KEY holds a concrete value; repeatable),
#         [--value KEY] (print only KEY's concrete value, no JSON; exit 3 if
#         unmeasured).
# Output: stdout JSON {"board_id",...,"values",...,"placeholders",...}; errors
#         are structured JSON log lines on stderr.
# Exit: 0 ok, 2 usage, 1 profile invalid, 3 required value unmeasured/undecided.
# Side effects: none. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.show_board_profile
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

usage()
{
  printf '%s\n' "Usage: $0 [--board ID] [--require KEY]... | [--board ID] --value KEY"
}

required_keys=
value_key=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --board)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      BHP_BOARD=$2
      export BHP_BOARD
      shift 2
      ;;
    --require)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      required_keys="$required_keys $2"
      shift 2
      ;;
    --value)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      value_key=$2
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

if ! bhp_board_profile_load_selected "$PROJECT_ROOT"; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" \
    "$TRACE_ID" >&2 || true
  exit 1
fi

if [ -n "$value_key" ]; then
  if ! bhp_board_profile_require "$value_key"; then
    bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" \
      "$TRACE_ID" >&2 || true
    exit 3
  fi
  exit 0
fi

relative_profile_path=${BHP_BOARD_PROFILE_LOADED_PATH#"$PROJECT_ROOT"/}
printf '{"schema_version":1,"board_id":"%s","status":"%s","selection":"%s","profile_path":"%s","values":{' \
  "$(bhp_json_escape "$BHP_BOARD_ID")" \
  "$(bhp_json_escape "$BHP_BOARD_STATUS")" \
  "$(bhp_json_escape "$BHP_BOARD_PROFILE_SELECTION")" \
  "$(bhp_json_escape "$relative_profile_path")"
separator=
for profile_key in $BHP_BOARD_PROFILE_KEYS; do
  profile_value=$(bhp_board_profile_value "$profile_key")
  printf '%s"%s":"%s"' "$separator" "$(bhp_json_escape "$profile_key")" \
    "$(bhp_json_escape "$profile_value")"
  separator=,
done
printf '},"placeholders":['
separator=
unmeasured_count=0
human_decision_count=0
for profile_key in $BHP_BOARD_PROFILE_KEYS; do
  profile_value=$(bhp_board_profile_value "$profile_key")
  case "$profile_value" in
    UNMEASURED:*)
      placeholder_kind=unmeasured
      placeholder_id=${profile_value#UNMEASURED:}
      unmeasured_count=$((unmeasured_count + 1))
      ;;
    HUMAN_DECISION_REQUIRED:*)
      placeholder_kind=human_decision
      placeholder_id=${profile_value#HUMAN_DECISION_REQUIRED:}
      human_decision_count=$((human_decision_count + 1))
      ;;
    *) continue ;;
  esac
  printf '%s{"key":"%s","kind":"%s","id":"%s"}' "$separator" \
    "$(bhp_json_escape "$profile_key")" "$placeholder_kind" \
    "$(bhp_json_escape "$placeholder_id")"
  separator=,
done
printf '],"placeholder_counts":{"unmeasured":%s,"human_decision":%s}' \
  "$unmeasured_count" "$human_decision_count"

required_status=0
printf ',"required":['
separator=
for required_key in $required_keys; do
  if bhp_board_profile_require "$required_key" >/dev/null; then
    printf '%s{"key":"%s","state":"concrete"}' "$separator" \
      "$(bhp_json_escape "$required_key")"
  else
    printf '%s{"key":"%s","state":"blocked","error_code":"%s","detail":"%s"}' \
      "$separator" "$(bhp_json_escape "$required_key")" \
      "$(bhp_json_escape "$BHP_BOARD_PROFILE_ERROR_CODE")" \
      "$(bhp_json_escape "$BHP_BOARD_PROFILE_ERROR_DETAIL")"
    required_status=3
  fi
  separator=,
done
printf ']}\n'
exit "$required_status"
