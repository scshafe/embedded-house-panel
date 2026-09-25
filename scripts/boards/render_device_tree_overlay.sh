#!/bin/sh
# Renders a device-tree overlay template from a board profile, refusing placeholders.
# Inputs: (--board ID | --profile FILE) --template FILE --output FILE.
#   Template tokens: @BHP_BOARD_<KEY>@ (profile value, must be concrete) and the
#   derived tokens @BHP_DERIVED_PIR_GPIO_FLAGS@ (from BHP_BOARD_PIR_ACTIVE_LEVEL)
#   and @BHP_DERIVED_PIR_PAD_OWNER_FRAGMENT@ (from
#   BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE).
# Output: the rendered .dtso at --output; one JSON result line on stdout.
# Exit: 0 rendered; 3 a referenced value is UNMEASURED or awaits a human
#   decision (every blocked key is listed); 1 invalid input; 2 usage.
# Side effects: writes only --output (via a sibling temporary file).
# Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.render_device_tree_overlay
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

usage()
{
  printf '%s\n' "Usage: $0 (--board ID | --profile FILE) --template FILE --output FILE"
}

board_id=
profile_path=
template_path=
output_path=
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
  bhp_fail DEVICE_TREE_RENDER.TEMPLATE_MISSING "$template_path" "$TRACE_ID"
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

referenced_keys=$(grep -o '@BHP_BOARD_[A-Z0-9_]*@' "$template_path" | tr -d '@' | sort -u)
case " $(grep -o '@BHP_DERIVED_[A-Z0-9_]*@' "$template_path" | tr '\n' ' ') " in
  *'@BHP_DERIVED_PIR_GPIO_FLAGS@'*)
    referenced_keys="$referenced_keys BHP_BOARD_PIR_ACTIVE_LEVEL" ;;
esac
case " $(grep -o '@BHP_DERIVED_[A-Z0-9_]*@' "$template_path" | tr '\n' ' ') " in
  *'@BHP_DERIVED_PIR_PAD_OWNER_FRAGMENT@'*)
    referenced_keys="$referenced_keys BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE" ;;
esac
unknown_derived=$(grep -o '@BHP_DERIVED_[A-Z0-9_]*@' "$template_path" |
  grep -v -x -e '@BHP_DERIVED_PIR_GPIO_FLAGS@' \
    -e '@BHP_DERIVED_PIR_PAD_OWNER_FRAGMENT@' || true)
[ -z "$unknown_derived" ] || {
  bhp_fail DEVICE_TREE_RENDER.DERIVED_TOKEN_UNKNOWN "$(printf '%s' "$unknown_derived" | tr '\n' ' ')" "$TRACE_ID"
  exit 1
}

blocked_keys=
blocked_details=
substitutions=
referenced_value=
# Word splitting of the token list is intentional.
# shellcheck disable=SC2086
for referenced_key in $(printf '%s\n' $referenced_keys | sort -u); do
  if bhp_board_profile_require_into referenced_value "$referenced_key"; then
    substitutions="$substitutions $referenced_key=$referenced_value"
  else
    blocked_keys="$blocked_keys $referenced_key"
    blocked_details="$blocked_details; $BHP_BOARD_PROFILE_ERROR_DETAIL"
  fi
done
if [ -n "$blocked_keys" ]; then
  bhp_log error operation_failed "$TRACE_ID" \
    error_code BOARD_PROFILE.VALUE_UNMEASURED \
    error_message "Refusing to render $template_path: ${blocked_details#; }" \
    board_id "$BHP_BOARD_ID" \
    blocked_keys "${blocked_keys# }" \
    remedy "record evidence under hardware_tests/results/ and set the values in config/boards/$BHP_BOARD_ID.env" \
    stack_trace unavailable >&2
  exit 3
fi

gpio_flags=
pad_owner_fragment=
for substitution in $substitutions; do
  case "$substitution" in
    BHP_BOARD_PIR_ACTIVE_LEVEL=high) gpio_flags=GPIO_ACTIVE_HIGH ;;
    BHP_BOARD_PIR_ACTIVE_LEVEL=low) gpio_flags=GPIO_ACTIVE_LOW ;;
    BHP_BOARD_PIR_ACTIVE_LEVEL=*)
      bhp_fail DEVICE_TREE_RENDER.ACTIVE_LEVEL_INVALID "$substitution" "$TRACE_ID"
      exit 1
      ;;
    BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE=none) pad_owner_fragment='/* No base-tree node claims the PIR pad (J18 inventory). */' ;;
    BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE=*)
      pad_owner_label=${substitution#*=}
      case "$pad_owner_label" in
        ''|[!a-z]*|*[!a-z0-9_]*)
          bhp_fail DEVICE_TREE_RENDER.PAD_OWNER_INVALID "$pad_owner_label" "$TRACE_ID"
          exit 1
          ;;
      esac
      pad_owner_fragment="/* The J18 inventory shows &$pad_owner_label claims the PIR pad. */
&$pad_owner_label {
	status = \"disabled\";
};"
      ;;
  esac
done

output_directory=$(dirname -- "$output_path")
test -d "$output_directory" || {
  bhp_fail DEVICE_TREE_RENDER.OUTPUT_DIRECTORY_MISSING "$output_directory" "$TRACE_ID"
  exit 1
}
temporary_output=$output_path.tmp.$$
# Values already matched the strict profile grammar, so they contain no awk,
# sed, or shell metacharacters beyond the allowed [A-Za-z0-9_./:,@+=*%-].
BHP_RENDER_SUBSTITUTIONS=$substitutions \
BHP_RENDER_GPIO_FLAGS=$gpio_flags \
BHP_RENDER_PAD_OWNER_FRAGMENT=$pad_owner_fragment \
  awk '
    BEGIN {
      count = split(ENVIRON["BHP_RENDER_SUBSTITUTIONS"], pairs, " ")
      for (index_value = 1; index_value <= count; index_value++) {
        if (pairs[index_value] == "") continue
        separator = index(pairs[index_value], "=")
        token = "@" substr(pairs[index_value], 1, separator - 1) "@"
        values[token] = substr(pairs[index_value], separator + 1)
      }
      values["@BHP_DERIVED_PIR_GPIO_FLAGS@"] = ENVIRON["BHP_RENDER_GPIO_FLAGS"]
      values["@BHP_DERIVED_PIR_PAD_OWNER_FRAGMENT@"] = ENVIRON["BHP_RENDER_PAD_OWNER_FRAGMENT"]
    }
    {
      line = $0
      output = ""
      while (match(line, /@BHP_(BOARD|DERIVED)_[A-Z0-9_]*@/)) {
        token = substr(line, RSTART, RLENGTH)
        output = output substr(line, 1, RSTART - 1)
        if (token in values) {
          output = output values[token]
        } else {
          output = output token
        }
        line = substr(line, RSTART + RLENGTH)
      }
      print output line
    }
  ' "$template_path" >"$temporary_output"
if grep -E -q '@BHP_(BOARD|DERIVED)_[A-Z0-9_]+@' "$temporary_output"; then
  rm -f -- "$temporary_output"
  bhp_fail DEVICE_TREE_RENDER.TOKEN_UNRESOLVED "$template_path" "$TRACE_ID"
  exit 1
fi
mv -- "$temporary_output" "$output_path"
# Word splitting of the key list is intentional.
# shellcheck disable=SC2086
printf '{"status":"rendered","board_id":"%s","template":"%s","output":"%s","substituted_keys":"%s"}\n' \
  "$(bhp_json_escape "$BHP_BOARD_ID")" "$(bhp_json_escape "$template_path")" \
  "$(bhp_json_escape "$output_path")" \
  "$(bhp_json_escape "$(printf '%s\n' $referenced_keys | sort -u | tr '\n' ' ' | sed 's/ $//')")"
