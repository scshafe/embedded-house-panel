#!/bin/sh
# Builds the configured Buildroot image; it never writes an image to removable media.
# Inputs: BHP_BUILDROOT_DIR, BHP_BUILDROOT_OUTPUT_DIR, optional BHP_BUILD_JOBS.
# Side effects: writes Buildroot downloads/build products below configured paths.
# Idempotent: incremental Buildroot semantics.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.buildroot_build
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' "Usage: BHP_BUILDROOT_DIR=/abs/source BHP_BUILDROOT_OUTPUT_DIR=/abs/output [BHP_BUILD_JOBS=N] $0"
  exit 0
fi

: "${BHP_BUILDROOT_DIR:?BHP_BUILDROOT_DIR is required}"
: "${BHP_BUILDROOT_OUTPUT_DIR:?BHP_BUILDROOT_OUTPUT_DIR is required}"
test -f "$BHP_BUILDROOT_DIR/Makefile" || {
  bhp_fail BUILDROOT.SOURCE_INVALID "Buildroot Makefile not found" "$TRACE_ID"
  exit 1
}
if ! bhp_buildroot_source_is_pinned "$BHP_BUILDROOT_DIR"; then
  bhp_fail BUILDROOT.VERSION_MISMATCH \
    "Buildroot source does not match $BHP_PINNED_BUILDROOT_VERSION" "$TRACE_ID"
  exit 1
fi
if ! bhp_buildroot_output_is_separate "$BHP_BUILDROOT_DIR" \
  "$BHP_BUILDROOT_OUTPUT_DIR" "$PROJECT_ROOT"; then
  bhp_fail BUILDROOT.OUTPUT_OVERLAP_OR_UNRESOLVED \
    "Output must be separate from Buildroot source and project trees" "$TRACE_ID"
  exit 1
fi
buildroot_source_id=$(
  bhp_buildroot_read_source_id "$BHP_BUILDROOT_DIR" \
    "$PROJECT_ROOT/scripts/build/buildroot_source_id.sh"
) || {
  bhp_fail BUILDROOT.SOURCE_ID_UNREADABLE \
    "Buildroot source contains an unsupported type or changed while hashing" \
    "$TRACE_ID"
  exit 1
}
BHP_BUILDROOT_SOURCE_ID=$buildroot_source_id
if [ "$buildroot_source_id" = "$BHP_PINNED_BUILDROOT_SOURCE_ID" ]; then
  BHP_BUILDROOT_ARCHIVE_SHA256=$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256
else
  BHP_BUILDROOT_ARCHIVE_SHA256=unverified
fi
export BHP_BUILDROOT_SOURCE_ID BHP_BUILDROOT_ARCHIVE_SHA256
test -f "$BHP_BUILDROOT_OUTPUT_DIR/.config" || {
  bhp_fail BUILDROOT.CONFIG_MISSING "Run configure_buildroot.sh first" "$TRACE_ID"
  exit 1
}
profile_file=$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile
test -f "$profile_file" && test ! -L "$profile_file" || {
  bhp_fail BUILDROOT.PROFILE_STAMP_MISSING \
    "Re-run configure_buildroot.sh with an explicit profile" "$TRACE_ID"
  exit 1
}
configured_profile=$(sed -n '1p' "$profile_file")
test "$(wc -l <"$profile_file" | tr -d ' ')" -eq 1 || {
  bhp_fail BUILDROOT.PROFILE_STAMP_INVALID "$profile_file" "$TRACE_ID"
  exit 1
}
case "$configured_profile" in
  production)
    if [ -n "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:-}" ]; then
      bhp_fail BUILDROOT.PRODUCTION_KEY_INPUT_FORBIDDEN \
        "Production builds never consume SSH authorization material" "$TRACE_ID"
      exit 1
    fi
    ;;
  bringup)
    : "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:?Bring-up build requires BHP_BRINGUP_AUTHORIZED_KEYS_FILE}"
    if ! bhp_bringup_authorized_key_is_valid \
      "$BHP_BRINGUP_AUTHORIZED_KEYS_FILE"; then
      bhp_fail BUILDROOT.BRINGUP_KEY_INVALID \
        "Expected one valid Ed25519 public key in an absolute regular file" \
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
  *)
    bhp_fail BUILDROOT.PROFILE_STAMP_INVALID "$configured_profile" "$TRACE_ID"
    exit 1
    ;;
esac

build_jobs=${BHP_BUILD_JOBS:-2}
case "$build_jobs" in
  *[!0-9]*|0|'') bhp_fail BUILDROOT.JOBS_INVALID "$build_jobs" "$TRACE_ID"; exit 1 ;;
esac

bhp_log info buildroot_build_started "$TRACE_ID" jobs "$build_jobs" \
  profile "$configured_profile" \
  buildroot_source_id "$buildroot_source_id" \
  buildroot_archive_sha256 "$BHP_BUILDROOT_ARCHIVE_SHA256"
make -C "$BHP_BUILDROOT_DIR" \
  O="$BHP_BUILDROOT_OUTPUT_DIR" \
  BR2_DL_DIR="$BHP_BUILDROOT_OUTPUT_DIR/downloads" \
  -j"$build_jobs"
test -d "$BHP_BUILDROOT_OUTPUT_DIR/images"
bhp_log info buildroot_build_complete "$TRACE_ID" \
  images_directory "$BHP_BUILDROOT_OUTPUT_DIR/images" \
  profile "$configured_profile"
