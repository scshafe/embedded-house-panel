#!/bin/sh
# Syntax-checks and lints all repository-owned shell entry points.
# Inputs: none. Side effects: none. Idempotent.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_shell
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

command -v shellcheck >/dev/null 2>&1 || {
  bhp_fail VERIFY_SHELL.SHELLCHECK_MISSING "Install ShellCheck before verification" "$TRACE_ID"
  exit 1
}

shell_entrypoints=$(find "$PROJECT_ROOT/scripts" "$PROJECT_ROOT/hardware_tests" "$PROJECT_ROOT/system_image" \
  -type f \( -name '*.sh' -o -path '*/etc/init.d/S*' \) -print | sort)
test -n "$shell_entrypoints" || {
  bhp_fail VERIFY_SHELL.NO_ENTRYPOINTS "No shell entry points were found" "$TRACE_ID"
  exit 1
}

# Repository paths deliberately contain no newlines; newline-delimited find output is safe here.
# shellcheck disable=SC2086
sh -n $shell_entrypoints
# shellcheck disable=SC2086
shellcheck -x -e SC1091 $shell_entrypoints

non_executable=$(
  find "$PROJECT_ROOT/scripts" "$PROJECT_ROOT/hardware_tests" \
    -type f -name '*.sh' ! -perm -u+x -print
  find "$PROJECT_ROOT/system_image" -type f \
    \( -name 'post-build.sh' -o -path '*/etc/init.d/S*' \) \
    ! -perm -u+x -print
)
if [ -n "$non_executable" ]; then
  bhp_fail VERIFY_SHELL.NOT_EXECUTABLE "$non_executable" "$TRACE_ID"
  exit 1
fi

bhp_log info shell_verification_complete "$TRACE_ID" entrypoint_count "$(printf '%s\n' "$shell_entrypoints" | wc -l | tr -d ' ')"
