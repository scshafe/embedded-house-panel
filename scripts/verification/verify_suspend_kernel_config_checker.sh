#!/bin/sh
# Proves the suspend-config checker accepts each board profile's symbol set and
# rejects every disabled or missing prerequisite.
# Inputs: none. Every config/boards/<id>.env is exercised.
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

checker=$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh
positive_case_count=0
negative_case_count=0
checked_boards=

expect_rejection()
{
  rejection_board=$1
  rejection_config=$2
  rejection_label=$3
  rejection_mode=$4
  rejection_log=$temporary_root/$rejection_board.$rejection_label.log
  if "$checker" --board "$rejection_board" --config "$rejection_config" \
    --mode "$rejection_mode" >"$rejection_log" 2>&1; then
    bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.INVALID_CONFIG_ACCEPTED \
      "$rejection_board $rejection_label" "$TRACE_ID"
    exit 1
  fi
  grep -F 'VERIFY_SUSPEND_KERNEL_CONFIG.SYMBOL_DISABLED_MISSING_OR_AMBIGUOUS' \
    "$rejection_log" >/dev/null || {
      bhp_fail VERIFY_SUSPEND_KERNEL_CONFIG_CHECKER.WRONG_FAILURE \
        "$rejection_board $rejection_label" "$TRACE_ID"
      exit 1
    }
  negative_case_count=$((negative_case_count + 1))
}

for board_profile in "$PROJECT_ROOT"/config/boards/*.env; do
  board_id=${board_profile##*/}
  board_id=${board_id%.env}
  if grep -q "^BHP_BOARD_STATUS=simulation$" "$board_profile"; then continue; fi
  symbol_list=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" \
    --board "$board_id" --value BHP_BOARD_SUSPEND_KCONFIG_SYMBOLS)
  fragment_value=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" \
    --board "$board_id" --value BHP_BOARD_SUSPEND_KCONFIG_FRAGMENT_VALUE)
  symbols=$(printf '%s\n' "$symbol_list" | tr ',' ' ')

  valid_effective=$temporary_root/$board_id.effective.config
  valid_fragment=$temporary_root/$board_id.fragment.config
  : >"$valid_effective"
  : >"$valid_fragment"
  for symbol in $symbols; do
    printf '%s=y\n' "$symbol" >>"$valid_effective"
    printf '%s=%s\n' "$symbol" "$fragment_value" >>"$valid_fragment"
  done
  "$checker" --board "$board_id" --config "$valid_effective" --mode effective >/dev/null
  "$checker" --board "$board_id" --config "$valid_fragment" --mode fragment >/dev/null
  positive_case_count=$((positive_case_count + 2))

  for symbol in $symbols; do
    disabled_config=$temporary_root/$board_id.$symbol.disabled.config
    sed "s/^${symbol}=[ym]$/# ${symbol} is not set/" \
      "$valid_effective" >"$disabled_config"
    expect_rejection "$board_id" "$disabled_config" "$symbol.disabled" effective
    missing_config=$temporary_root/$board_id.$symbol.missing.config
    grep -F -v "${symbol}=" "$valid_effective" >"$missing_config" || true
    expect_rejection "$board_id" "$missing_config" "$symbol.missing" effective
  done
  wrong_fragment_value=m
  [ "$fragment_value" = m ] && wrong_fragment_value=y
  first_symbol=${symbols%% *}
  wrong_fragment=$temporary_root/$board_id.wrong-fragment.config
  sed "s/^${first_symbol}=${fragment_value}$/${first_symbol}=${wrong_fragment_value}/" \
    "$valid_fragment" >"$wrong_fragment"
  expect_rejection "$board_id" "$wrong_fragment" wrong_fragment_value fragment
  checked_boards="$checked_boards $board_id"
done

bhp_log info suspend_kernel_config_checker_verified "$TRACE_ID" \
  boards "${checked_boards# }" \
  positive_cases "$positive_case_count" \
  negative_cases "$negative_case_count" \
  hardware_evidence not_performed
