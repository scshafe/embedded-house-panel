#!/bin/sh
# Runs every non-hardware repository verification gate.
# Inputs: optional BHP_BUILDROOT_DIR and KERNEL_DIR forwarded to specialized checks.
# Side effects: writes host build products below .build.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_repository
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

"$SCRIPT_DIRECTORY/verify_host_cpp.sh" all
"$SCRIPT_DIRECTORY/verify_shell_scripts.sh"
"$SCRIPT_DIRECTORY/verify_documentation.sh"
"$SCRIPT_DIRECTORY/verify_presence_sensor_sources.sh"
"$SCRIPT_DIRECTORY/verify_buildroot_external_tree.sh"
"$SCRIPT_DIRECTORY/verify_build_and_deploy_contracts.sh"

if rg --hidden --line-number \
  --glob '!.git/**' \
  --glob '!.build/**' \
  --glob '!hardware_tests/results/**' \
  '[[:blank:]]+$' "$PROJECT_ROOT"; then
  bhp_fail VERIFY_REPOSITORY.TRAILING_WHITESPACE "Repository source contains trailing whitespace" "$TRACE_ID"
  exit 1
fi
git -C "$PROJECT_ROOT" diff --check
bhp_log info repository_verification_complete "$TRACE_ID" hardware_execution not_performed
