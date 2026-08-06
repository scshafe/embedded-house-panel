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

usage()
{
  printf '%s\n' "Usage: $0 [--profile host|buildroot|deploy] [--strict]"
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
  host) required_commands="git make c++ rg shellcheck" ;;
  buildroot) required_commands="git make gcc g++ cmake tar xz patch rsync bc bison flex perl python3 file wget curl cpio unzip dtc" ;;
  deploy) required_commands="ssh scp rsync shasum" ;;
  *) bhp_fail ENVIRONMENT.PROFILE_INVALID "Unknown profile: $profile" "$TRACE_ID"; exit 1 ;;
esac

missing_commands=""
for command_name in $required_commands; do
  if command -v "$command_name" >/dev/null 2>&1; then
    bhp_log info dependency_available "$TRACE_ID" profile "$profile" command "$command_name"
  else
    bhp_log warn dependency_missing "$TRACE_ID" profile "$profile" command "$command_name"
    missing_commands="$missing_commands $command_name"
  fi
done

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

bhp_log info environment_check_complete "$TRACE_ID" profile "$profile" missing "$missing_commands"
