#!/bin/sh
# Validates the AM335x suspend-stack prerequisites in an explicit kernel config.
# Inputs: --config FILE --mode fragment|effective.
# Side effects: none. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.check_suspend_kernel_config
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

config_path=
validation_mode=

usage()
{
  printf '%s\n' "Usage: $0 --config FILE --mode fragment|effective"
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

suspend_prerequisite_symbols="
CONFIG_AMX3_PM
CONFIG_WKUP_M3_IPC
CONFIG_WKUP_M3_RPROC
CONFIG_TI_EMIF_SRAM
"

for suspend_prerequisite_symbol in $suspend_prerequisite_symbols; do
  symbol_lines=$(grep -E \
    "^(${suspend_prerequisite_symbol}=[ym]|# ${suspend_prerequisite_symbol} is not set)$" \
    "$config_path" || true)
  case "$validation_mode:$symbol_lines" in
    "fragment:${suspend_prerequisite_symbol}=m"|\
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
  config_path "$config_path" \
  validation_mode "$validation_mode" \
  deepsleep0_evidence not_established
