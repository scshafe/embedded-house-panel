#!/bin/sh
# Reports environment support for host tests, Buildroot, or deployment.
# Inputs: optional --profile host|buildroot|deploy and --strict.
# Side effects: none. Idempotent.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.environment_check
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

profile=host
strict=false
host_compiler_command=

usage()
{
  printf '%s\n' "Usage: $0 [--profile host|buildroot|deploy] [--strict]"
}

read_first_version_line()
{
  environment_version_output=$(LC_ALL=C "$@" 2>&1) || return 1
  environment_version_first_line=$(printf '%s\n' \
    "$environment_version_output" | sed -n '1p')
  test -n "$environment_version_first_line" || return 1
  printf '%s\n' "$environment_version_first_line"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --profile)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      profile=$2
      shift 2
      ;;
    --strict)
      strict=true
      shift
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

case "$profile" in
  host)
    required_commands="git make python3 rg shellcheck cc cmake ctest ninja pkg-config"
    host_compiler_command=${CXX:-c++}
    ;;
  buildroot) required_commands="git make gcc g++ cmake tar xz patch rsync bc bison flex perl python3 file wget curl cpio unzip dtc" ;;
  deploy) required_commands="ssh scp rsync shasum" ;;
  *) bhp_fail ENVIRONMENT.PROFILE_INVALID "Unknown profile: $profile" "$TRACE_ID"; exit 1 ;;
esac

missing_commands=""
unreadable_version_commands=""
for command_name in $required_commands; do
  if command -v "$command_name" >/dev/null 2>&1; then
    bhp_log info dependency_available "$TRACE_ID" profile "$profile" command "$command_name"
  else
    bhp_log warn dependency_missing "$TRACE_ID" profile "$profile" command "$command_name"
    missing_commands="$missing_commands $command_name"
  fi
done

if [ -n "$host_compiler_command" ]; then
  if command -v "$host_compiler_command" >/dev/null 2>&1; then
    bhp_log info dependency_available "$TRACE_ID" \
      profile "$profile" \
      command "$host_compiler_command" \
      dependency_role cxx
  else
    bhp_log warn dependency_missing "$TRACE_ID" \
      profile "$profile" \
      command "$host_compiler_command" \
      dependency_role cxx
    missing_commands="$missing_commands $host_compiler_command"
  fi
fi

if [ "$profile" = host ]; then
  for versioned_command in cmake pkg-config; do
    command -v "$versioned_command" >/dev/null 2>&1 || continue
    if command_version=$(read_first_version_line \
      "$versioned_command" --version); then
      bhp_log info dependency_version_available "$TRACE_ID" \
        profile "$profile" \
        command "$versioned_command" \
        version "$command_version"
    else
      bhp_log warn dependency_version_unreadable "$TRACE_ID" \
        profile "$profile" \
        command "$versioned_command"
      unreadable_version_commands="$unreadable_version_commands $versioned_command"
    fi
  done
fi

if [ "$profile" = buildroot ] && [ "$(uname -s)" != Linux ]; then
  bhp_log warn buildroot_linux_host_required "$TRACE_ID" host_os "$(uname -s)"
  missing_commands="$missing_commands linux-host"
fi
if [ "$profile" = buildroot ] && [ "$(uname -m)" != x86_64 ]; then
  bhp_log warn buildroot_x86_64_host_required "$TRACE_ID" host_arch "$(uname -m)"
  missing_commands="$missing_commands x86_64-host"
fi

if ! command -v sha256sum >/dev/null 2>&1 && ! command -v shasum >/dev/null 2>&1; then
  bhp_log warn dependency_missing "$TRACE_ID" profile "$profile" command "sha256sum-or-shasum"
  missing_commands="$missing_commands sha256sum-or-shasum"
fi

if [ -n "$missing_commands" ] && [ "$strict" = true ]; then
  bhp_fail ENVIRONMENT.DEPENDENCIES_MISSING "Missing:$missing_commands" "$TRACE_ID"
  exit 1
fi
if [ -n "$unreadable_version_commands" ] && [ "$strict" = true ]; then
  bhp_fail ENVIRONMENT.TOOL_VERSIONS_UNREADABLE \
    "Unreadable:$unreadable_version_commands" "$TRACE_ID"
  exit 1
fi

bhp_log info environment_check_complete "$TRACE_ID" profile "$profile" missing "$missing_commands"
