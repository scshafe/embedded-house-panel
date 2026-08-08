#!/bin/sh
# Configures the pinned Buildroot external defconfig in an explicit output directory.
# Inputs: BHP_BUILDROOT_DIR, BHP_BUILDROOT_OUTPUT_DIR, optional profile/key.
# Side effects: writes generated configuration only below BHP_BUILDROOT_OUTPUT_DIR.
# Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.buildroot_configure
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' \
    "Usage: BHP_BUILDROOT_DIR=/abs/source BHP_BUILDROOT_OUTPUT_DIR=/abs/output [BHP_BUILDROOT_PROFILE=production|bringup] [BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/abs/id.pub] $0"
  exit 0
fi

: "${BHP_BUILDROOT_DIR:?BHP_BUILDROOT_DIR is required}"
: "${BHP_BUILDROOT_OUTPUT_DIR:?BHP_BUILDROOT_OUTPUT_DIR is required}"
buildroot_profile=${BHP_BUILDROOT_PROFILE:-production}
case "$buildroot_profile" in
  production) defconfig_target=beaglebone_house_panel_defconfig ;;
  bringup) defconfig_target=beaglebone_house_panel_bringup_defconfig ;;
  *)
    bhp_fail BUILDROOT.PROFILE_INVALID "$buildroot_profile" "$TRACE_ID"
    exit 1
    ;;
esac

for path_value in "$BHP_BUILDROOT_DIR" "$BHP_BUILDROOT_OUTPUT_DIR"; do
  case "$path_value" in
    /*) ;;
    *) bhp_fail BUILDROOT.PATH_NOT_ABSOLUTE "$path_value" "$TRACE_ID"; exit 1 ;;
  esac
  case "$path_value" in
    /|/Users|/home|/root) bhp_fail BUILDROOT.PATH_UNSAFE "$path_value" "$TRACE_ID"; exit 1 ;;
  esac
done

test -f "$BHP_BUILDROOT_DIR/Makefile" || {
  bhp_fail BUILDROOT.SOURCE_INVALID "Buildroot Makefile not found" "$TRACE_ID"
  exit 1
}
actual_buildroot_version=$(
  bhp_buildroot_read_source_version "$BHP_BUILDROOT_DIR"
) || {
  bhp_fail BUILDROOT.VERSION_UNREADABLE \
    "Buildroot Makefile did not contain one canonical version declaration" "$TRACE_ID"
  exit 1
}
if [ "$actual_buildroot_version" != "$BHP_PINNED_BUILDROOT_VERSION" ]; then
  bhp_fail BUILDROOT.VERSION_MISMATCH \
    "Expected $BHP_PINNED_BUILDROOT_VERSION; found $actual_buildroot_version" \
    "$TRACE_ID"
  exit 1
fi
if ! bhp_buildroot_output_is_separate "$BHP_BUILDROOT_DIR" \
  "$BHP_BUILDROOT_OUTPUT_DIR" "$PROJECT_ROOT"; then
  bhp_fail BUILDROOT.OUTPUT_OVERLAP_OR_UNRESOLVED \
    "Output must have an existing parent and be separate from Buildroot source and project trees" \
    "$TRACE_ID"
  exit 1
fi
canonical_project_root=$(bhp_buildroot_canonical_existing_directory "$PROJECT_ROOT")
canonical_buildroot_source=$(
  bhp_buildroot_canonical_existing_directory "$BHP_BUILDROOT_DIR"
)
canonical_buildroot_output=$(
  bhp_buildroot_canonical_output_path "$BHP_BUILDROOT_OUTPUT_DIR"
)
if [ -d "$BHP_BUILDROOT_OUTPUT_DIR" ]; then
  existing_profile_file=$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile
  if [ -e "$existing_profile_file" ] || [ -L "$existing_profile_file" ]; then
    if [ ! -f "$existing_profile_file" ] || [ -L "$existing_profile_file" ]; then
      bhp_fail BUILDROOT.EXISTING_PROFILE_STAMP_INVALID \
        "$existing_profile_file" "$TRACE_ID"
      exit 1
    fi
    existing_profile=$(sed -n '1p' "$existing_profile_file")
    test "$(wc -l <"$existing_profile_file" | tr -d ' ')" -eq 1 || {
      bhp_fail BUILDROOT.EXISTING_PROFILE_STAMP_INVALID \
        "$existing_profile_file" "$TRACE_ID"
      exit 1
    }
    if [ "$existing_profile" != "$buildroot_profile" ]; then
      bhp_fail BUILDROOT.PROFILE_SWITCH_FORBIDDEN \
        "Use a distinct empty output when switching production/bringup profiles" \
        "$TRACE_ID"
      exit 1
    fi
  elif [ -n "$(find "$BHP_BUILDROOT_OUTPUT_DIR" ! -path "$BHP_BUILDROOT_OUTPUT_DIR" -print -quit)" ]; then
    bhp_fail BUILDROOT.UNSTAMPED_OUTPUT_NOT_EMPTY \
      "Refusing to configure an unknown nonempty Buildroot output" "$TRACE_ID"
    exit 1
  fi
fi

case "$buildroot_profile" in
  production)
    if [ -n "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:-}" ]; then
      bhp_fail BUILDROOT.PRODUCTION_KEY_INPUT_FORBIDDEN \
        "Production configuration never consumes SSH authorization material" \
        "$TRACE_ID"
      exit 1
    fi
    ;;
  bringup)
    : "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:?Bring-up profile requires BHP_BRINGUP_AUTHORIZED_KEYS_FILE}"
    if ! bhp_bringup_authorized_key_is_valid \
      "$BHP_BRINGUP_AUTHORIZED_KEYS_FILE"; then
      bhp_fail BUILDROOT.BRINGUP_KEY_INVALID \
        "Expected one valid Ed25519 public key in an absolute regular file" \
        "$TRACE_ID"
      exit 1
    fi
    canonical_bringup_key=$(bhp_buildroot_canonical_existing_file \
      "$BHP_BRINGUP_AUTHORIZED_KEYS_FILE")
    case "$canonical_bringup_key/" in
      "$canonical_project_root/"*|"$canonical_buildroot_source/"*|"$canonical_buildroot_output/"*)
        bhp_fail BUILDROOT.BRINGUP_KEY_LOCATION_UNSAFE \
          "Authorization input must remain outside source and output trees" \
          "$TRACE_ID"
        exit 1
        ;;
    esac
    ;;
esac
mkdir -p "$BHP_BUILDROOT_OUTPUT_DIR"
make -C "$BHP_BUILDROOT_DIR" \
  O="$BHP_BUILDROOT_OUTPUT_DIR" \
  BR2_DL_DIR="$BHP_BUILDROOT_OUTPUT_DIR/downloads" \
  BR2_EXTERNAL="$PROJECT_ROOT/system_image/buildroot_external" \
  "$defconfig_target"
printf '%s\n' "$buildroot_profile" >"$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile"

bhp_log info buildroot_configure_complete "$TRACE_ID" \
  output_directory "$BHP_BUILDROOT_OUTPUT_DIR" \
  profile "$buildroot_profile" \
  buildroot_version "$actual_buildroot_version"
