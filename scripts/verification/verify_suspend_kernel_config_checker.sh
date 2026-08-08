#!/bin/sh
# Proves the suspend-config checker rejects each disabled or missing prerequisite.
# Inputs: none.
# Side effects: creates and removes one temporary fixture directory. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_suspend_kernel_config_checker
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

temporary_parent=${TMPDIR:-/tmp}
case "$temporary_parent" in
  /*) ;;
  *)
    bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.TMPDIR_INVALID \
      "TMPDIR must be absolute" "$TRACE_ID"
    exit 1
    ;;
esac
temporary_root=$(mktemp -d "$temporary_parent/bhp-suspend-kconfig.XXXXXX")
cleanup()
{
  find "$temporary_root" -type f -exec rm -- {} \; 2>/dev/null || true
  find "$temporary_root" -depth -type d -exec rmdir -- {} \; 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

valid_config=$temporary_root/valid.config
printf '%s\n' \
  'CONFIG_AMX3_PM=y' \
  'CONFIG_WKUP_M3_IPC=m' \
  'CONFIG_WKUP_M3_RPROC=m' \
  'CONFIG_TI_EMIF_SRAM=y' >"$valid_config"
"$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh" \
  --config "$valid_config" --mode effective >/dev/null

suspend_prerequisite_symbols="
CONFIG_AMX3_PM
CONFIG_WKUP_M3_IPC
CONFIG_WKUP_M3_RPROC
CONFIG_TI_EMIF_SRAM
"
negative_case_count=0
for suspend_prerequisite_symbol in $suspend_prerequisite_symbols; do
  disabled_config=$temporary_root/${suspend_prerequisite_symbol}.disabled.config
  sed "s/^${suspend_prerequisite_symbol}=[ym]$/# ${suspend_prerequisite_symbol} is not set/" \
    "$valid_config" >"$disabled_config"
  disabled_log=$temporary_root/${suspend_prerequisite_symbol}.disabled.log
  if "$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh" \
    --config "$disabled_config" --mode effective >"$disabled_log" 2>&1; then
    bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.DISABLED_SYMBOL_ACCEPTED \
      "$suspend_prerequisite_symbol" "$TRACE_ID"
    exit 1
  fi
  grep -F 'VERIFY_SUSPEND_KERNEL_CONFIG.SYMBOL_DISABLED_MISSING_OR_AMBIGUOUS' \
    "$disabled_log" >/dev/null || {
      bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.WRONG_FAILURE \
        "$suspend_prerequisite_symbol" "$TRACE_ID"
      exit 1
    }
  negative_case_count=$((negative_case_count + 1))
done

missing_config=$temporary_root/missing.config
grep -F -v 'CONFIG_TI_EMIF_SRAM=' "$valid_config" >"$missing_config"
missing_log=$temporary_root/missing.log
if "$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh" \
  --config "$missing_config" --mode effective >"$missing_log" 2>&1; then
  bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.MISSING_SYMBOL_ACCEPTED \
    CONFIG_TI_EMIF_SRAM "$TRACE_ID"
  exit 1
fi
grep -F 'VERIFY_SUSPEND_KERNEL_CONFIG.SYMBOL_DISABLED_MISSING_OR_AMBIGUOUS' \
  "$missing_log" >/dev/null || {
    bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.WRONG_FAILURE \
      CONFIG_TI_EMIF_SRAM "$TRACE_ID"
    exit 1
  }
negative_case_count=$((negative_case_count + 1))

bhp_log info suspend_kernel_config_checker_verified "$TRACE_ID" \
  positive_cases 1 \
  negative_cases "$negative_case_count" \
  hardware_evidence not_performed
