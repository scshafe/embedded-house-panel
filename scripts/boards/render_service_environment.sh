#!/bin/sh
# Renders a service environment example from a board profile.
# Inputs: (--board ID | --profile FILE) --template FILE --output FILE [--require-measured].
#   Template tokens: @BHP_BOARD_<KEY>@ only.
# Placeholder mapping (the daemons reject every REPLACE_ME token, so an enabled
#   service with an unresolved value fails closed at startup):
#     UNMEASURED:<gate>            -> REPLACE_ME_UNMEASURED_<gate>
#     HUMAN_DECISION_REQUIRED:<id> -> REPLACE_ME_HUMAN_DECISION_<id>
#     key absent from the profile  -> REPLACE_ME_NOT_IN_BOARD_PROFILE_<KEY>
# Output: the rendered file at --output; one JSON result line on stdout listing
#   substituted and unresolved keys.
# Exit: 0 rendered; 3 --require-measured and at least one key unresolved (no
#   output written); 1 invalid input; 2 usage.
# Side effects: writes only --output (via a sibling temporary file). Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.render_service_environment
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

usage()
{
  printf '%s\n' "Usage: $0 (--board ID | --profile FILE) --template FILE --output FILE [--require-measured]"
}

board_id=
profile_path=
template_path=
output_path=
require_measured=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --board|--profile|--template|--output)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      case "$1" in
        --board) board_id=$2 ;;
        --profile) profile_path=$2 ;;
        --template) template_path=$2 ;;
        --output) output_path=$2 ;;
      esac
      shift 2
      ;;
    --require-measured) require_measured=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
if [ -z "$template_path" ] || [ -z "$output_path" ] ||
   { [ -z "$board_id" ] && [ -z "$profile_path" ]; } ||
   { [ -n "$board_id" ] && [ -n "$profile_path" ]; }; then
  usage >&2
  exit 2
fi
test -f "$template_path" || {
  bhp_fail SERVICE_ENVIRONMENT_RENDER.TEMPLATE_MISSING "$template_path" "$TRACE_ID"
  exit 1
}
if [ -n "$board_id" ]; then
  bhp_board_profile_load "$PROJECT_ROOT" "$board_id" || {
    bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  }
else
  bhp_board_profile_load_file "$profile_path" || {
    bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  }
fi
if grep -E -q '@BHP_DERIVED_[A-Z0-9_]+@' "$template_path"; then
  bhp_fail SERVICE_ENVIRONMENT_RENDER.DERIVED_TOKEN_UNSUPPORTED "$template_path" "$TRACE_ID"
  exit 1
fi

referenced_keys=$(grep -o '@BHP_BOARD_[A-Z0-9_]*@' "$template_path" | tr -d '@' | sort -u)
substitutions=
substituted_keys=
unresolved_keys=
referenced_value=
for referenced_key in $referenced_keys; do
  case "$(bhp_board_profile_value_state "$referenced_key")" in
    measured)
      bhp_board_profile_require_into referenced_value "$referenced_key" || {
        bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
        exit 1
      }
      substituted_keys="$substituted_keys $referenced_key"
      ;;
    unmeasured)
      referenced_value=$(bhp_board_profile_value "$referenced_key")
      referenced_value=REPLACE_ME_UNMEASURED_${referenced_value#UNMEASURED:}
      unresolved_keys="$unresolved_keys $referenced_key"
      ;;
    human_decision)
      referenced_value=$(bhp_board_profile_value "$referenced_key")
      referenced_value=REPLACE_ME_HUMAN_DECISION_${referenced_value#HUMAN_DECISION_REQUIRED:}
      unresolved_keys="$unresolved_keys $referenced_key"
      ;;
    *)
      referenced_value=REPLACE_ME_NOT_IN_BOARD_PROFILE_$referenced_key
      unresolved_keys="$unresolved_keys $referenced_key"
      ;;
  esac
  substitutions="$substitutions $referenced_key=$referenced_value"
done

if [ "$require_measured" -eq 1 ] && [ -n "$unresolved_keys" ]; then
  bhp_log error operation_failed "$TRACE_ID" \
    error_code BOARD_PROFILE.VALUE_UNMEASURED \
    error_message "Refusing to render $template_path with unresolved board values" \
    board_id "$BHP_BOARD_ID" \
    unresolved_keys "${unresolved_keys# }" \
    remedy "record evidence under hardware_tests/results/ and set the values in config/boards/$BHP_BOARD_ID.env" \
    stack_trace unavailable >&2
  exit 3
fi

output_directory=$(dirname -- "$output_path")
test -d "$output_directory" || {
  bhp_fail SERVICE_ENVIRONMENT_RENDER.OUTPUT_DIRECTORY_MISSING "$output_directory" "$TRACE_ID"
  exit 1
}
temporary_output=$output_path.tmp.$$
# Profile values already matched [A-Za-z0-9_./:,@+=*%-]*, so they carry no awk
# or shell metacharacters.
BHP_RENDER_SUBSTITUTIONS=$substitutions awk '
  BEGIN {
    count = split(ENVIRON["BHP_RENDER_SUBSTITUTIONS"], pairs, " ")
    for (index_value = 1; index_value <= count; index_value++) {
      if (pairs[index_value] == "") continue
      separator = index(pairs[index_value], "=")
      values["@" substr(pairs[index_value], 1, separator - 1) "@"] = substr(pairs[index_value], separator + 1)
    }
  }
  {
    line = $0
    output = ""
    while (match(line, /@BHP_BOARD_[A-Z0-9_]*@/)) {
      token = substr(line, RSTART, RLENGTH)
      output = output substr(line, 1, RSTART - 1) ((token in values) ? values[token] : token)
      line = substr(line, RSTART + RLENGTH)
    }
    print output line
  }
' "$template_path" >"$temporary_output"
if grep -E -q '@BHP_BOARD_[A-Z0-9_]+@' "$temporary_output"; then
  rm -f -- "$temporary_output"
  bhp_fail SERVICE_ENVIRONMENT_RENDER.TOKEN_UNRESOLVED "$template_path" "$TRACE_ID"
  exit 1
fi
mv -- "$temporary_output" "$output_path"
printf '{"status":"rendered","board_id":"%s","template":"%s","output":"%s","substituted_keys":"%s","unresolved_keys":"%s"}\n' \
  "$(bhp_json_escape "$BHP_BOARD_ID")" "$(bhp_json_escape "$template_path")" \
  "$(bhp_json_escape "$output_path")" \
  "$(bhp_json_escape "${substituted_keys# }")" "$(bhp_json_escape "${unresolved_keys# }")"
