#!/bin/sh
# Builds the explicitly non-production, root-public-key-only bring-up image.
# Inputs: Buildroot source/output, caller-owned Ed25519 public key, optional
#   BHP_BOARD (default: the active board profile) and jobs.
# Side effects: configures and builds only the explicit external output tree.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.buildroot_bringup_build
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
	printf '%s\n' \
		"Usage: BHP_BUILDROOT_DIR=/abs/source BHP_BUILDROOT_OUTPUT_DIR=/abs/output BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/abs/id_ed25519.pub [BHP_BOARD=frdm-imx8mp] [BHP_BUILD_JOBS=N] $0"
	exit 0
fi

: "${BHP_BUILDROOT_DIR:?BHP_BUILDROOT_DIR is required}"
: "${BHP_BUILDROOT_OUTPUT_DIR:?BHP_BUILDROOT_OUTPUT_DIR is required}"
: "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:?BHP_BRINGUP_AUTHORIZED_KEYS_FILE is required}"

BHP_BUILDROOT_PROFILE=bringup
export BHP_BUILDROOT_PROFILE BHP_BRINGUP_AUTHORIZED_KEYS_FILE
bhp_log warn bringup_image_build_requested "$TRACE_ID" \
	profile NON_PRODUCTION_ROOT_PUBLIC_KEY_ONLY \
	output_directory "$BHP_BUILDROOT_OUTPUT_DIR"
"$PROJECT_ROOT/scripts/build/configure_buildroot.sh"
"$PROJECT_ROOT/scripts/build/build_buildroot_image.sh"
bhp_log warn bringup_image_build_complete "$TRACE_ID" \
	profile NON_PRODUCTION_ROOT_PUBLIC_KEY_ONLY \
	image "$BHP_BUILDROOT_OUTPUT_DIR/images/sdcard.img"
