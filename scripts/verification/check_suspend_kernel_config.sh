#!/bin/sh
# Validates a board's suspend-stack kernel prerequisites in an explicit config.
# Inputs: --config FILE --mode fragment|effective [--board ID]. The symbol set
#   and fragment value come from config/boards/<ID>.env
#   (BHP_BOARD_SUSPEND_KCONFIG_SYMBOLS, BHP_BOARD_SUSPEND_KCONFIG_FRAGMENT_VALUE);
#   the default board is BHP_BOARD, else the single active profile.
# Side effects: none. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../boards/board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.check_suspend_kernel_config
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

config_path=
validation_mode=

usage()
{
  printf '%s\n' "Usage: $0 --config FILE --mode fragment|effective [--board ID]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --config)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      config_path=$2
      shift 2
      ;;
    --mode)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      validation_mode=$2
      shift 2
      ;;
    --board)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      BHP_BOARD=$2
      export BHP_BOARD
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

case "$validation_mode" in
  fragment|effective) ;;
  *) usage >&2; exit 2 ;;
esac
test -f "$config_path" || {
  bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG.CONFIG_MISSING "$config_path" "$TRACE_ID"
  exit 1
}

suspend_symbol_list=
fragment_value=
if ! bhp_board_profile_load_selected "$PROJECT_ROOT" ||
   ! bhp_board_profile_require_into suspend_symbol_list BHP_BOARD_SUSPEND_KCONFIG_SYMBOLS ||
   ! bhp_board_profile_require_into fragment_value BHP_BOARD_SUSPEND_KCONFIG_FRAGMENT_VALUE; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
case "$fragment_value" in
  y|m) ;;
  *)
    bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG.FRAGMENT_VALUE_INVALID "$fragment_value" "$TRACE_ID"
    exit 1
    ;;
esac
suspend_prerequisite_symbols=$(printf '%s\n' "$suspend_symbol_list" | tr ',' ' ')

for suspend_prerequisite_symbol in $suspend_prerequisite_symbols; do
  symbol_lines=$(grep -E \
    "^(${suspend_prerequisite_symbol}=[ym]|# ${suspend_prerequisite_symbol} is not set)$" \
    "$config_path" || true)
  case "$validation_mode:$symbol_lines" in
    "fragment:${suspend_prerequisite_symbol}=${fragment_value}"|\
    "effective:${suspend_prerequisite_symbol}=y"|\
    "effective:${suspend_prerequisite_symbol}=m") ;;
    *)
      bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG.SYMBOL_DISABLED_MISSING_OR_AMBIGUOUS \
        "$suspend_prerequisite_symbol is invalid in $config_path for $validation_mode validation" \
        "$TRACE_ID"
      exit 1
      ;;
  esac
done

bhp_log info suspend_kernel_config_verified "$TRACE_ID" \
  board_id "$BHP_BOARD_ID" \
  config_path "$config_path" \
  validation_mode "$validation_mode" \
  symbols "$suspend_symbol_list" \
  suspend_wake_evidence not_established
