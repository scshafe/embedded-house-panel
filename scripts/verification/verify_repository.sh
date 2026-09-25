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

"$PROJECT_ROOT/scripts/environment/check_host_dependencies.sh" --profile host --strict

python3 "$SCRIPT_DIRECTORY/check_plan_verification_map.py"
python3 "$SCRIPT_DIRECTORY/verify_plan_verification_map.py"
python3 "$SCRIPT_DIRECTORY/verify_board_profiles.py"
(
  cd "$PROJECT_ROOT"
  PYTHONDONTWRITEBYTECODE=1 \
    python3 -m unittest hardware_tests.tests.test_classify_wake_trial \
      hardware_tests.tests.test_frdm_evidence_scripts
  # The FRDM evidence scripts run under BusyBox ash on the target.
  if command -v busybox >/dev/null 2>&1; then
    BHP_FRDM_TEST_SHELL="busybox sh" PYTHONDONTWRITEBYTECODE=1 \
      python3 -m unittest hardware_tests.tests.test_frdm_evidence_scripts
  fi
)

read_first_version_line()
{
  version_output=$(LC_ALL=C "$@" 2>&1) || return 1
  version_first_line=$(printf '%s\n' "$version_output" | sed -n '1p')
  test -n "$version_first_line" || return 1
  printf '%s\n' "$version_first_line"
}

read_shellcheck_version()
{
  version_output=$(LC_ALL=C shellcheck --version 2>&1) || return 1
  shellcheck_version=$(printf '%s\n' "$version_output" |
    sed -n 's/^version:[[:space:]]*//p')
  test -n "$shellcheck_version" || return 1
  printf '%s\n' "$shellcheck_version"
}

cxx=${CXX:-c++}
git_version=$(read_first_version_line git --version) || {
  bhp_fail VERIFY_REPOSITORY.TOOL_VERSION_UNREADABLE git "$TRACE_ID"
  exit 1
}
make_version=$(read_first_version_line make --version) || {
  bhp_fail VERIFY_REPOSITORY.TOOL_VERSION_UNREADABLE make "$TRACE_ID"
  exit 1
}
cxx_version=$(read_first_version_line "$cxx" --version) || {
  bhp_fail VERIFY_REPOSITORY.TOOL_VERSION_UNREADABLE "$cxx" "$TRACE_ID"
  exit 1
}
rg_version=$(read_first_version_line rg --version) || {
  bhp_fail VERIFY_REPOSITORY.TOOL_VERSION_UNREADABLE rg "$TRACE_ID"
  exit 1
}
shellcheck_version=$(read_shellcheck_version) || {
  bhp_fail VERIFY_REPOSITORY.TOOL_VERSION_UNREADABLE shellcheck "$TRACE_ID"
  exit 1
}

"$SCRIPT_DIRECTORY/verify_host_cpp.sh" all
"$SCRIPT_DIRECTORY/verify_aarch64_cross_build.sh"
"$SCRIPT_DIRECTORY/verify_shell_scripts.sh"
"$SCRIPT_DIRECTORY/verify_documentation.sh"
"$SCRIPT_DIRECTORY/verify_presence_sensor_sources.sh"
"$SCRIPT_DIRECTORY/verify_frdm_device_tree_overlay.sh"
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
bhp_log info repository_verification_complete "$TRACE_ID" \
  hardware_execution not_performed \
  git_version "$git_version" \
  make_version "$make_version" \
  cxx_command "$cxx" \
  cxx_version "$cxx_version" \
  rg_version "$rg_version" \
  shellcheck_version "$shellcheck_version"
