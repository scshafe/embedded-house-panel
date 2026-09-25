#!/bin/sh
# Configures the pinned Buildroot external defconfig in an explicit output directory.
# Inputs: BHP_BUILDROOT_DIR, BHP_BUILDROOT_OUTPUT_DIR, optional BHP_BOARD (default:
#   the active board profile), optional profile/key.
# NXP-licensed firmware is merged only when the board profile records an
#   accepted human decision (BHP_BOARD_NXP_FIRMWARE_EULA=accepted-adr-NNNN).
# Side effects: writes generated configuration only below BHP_BUILDROOT_OUTPUT_DIR.
# Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
# shellcheck source=../boards/board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.buildroot_configure
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' \
    "Usage: BHP_BUILDROOT_DIR=/abs/source BHP_BUILDROOT_OUTPUT_DIR=/abs/output [BHP_BOARD=frdm-imx8mp|beaglebone-black] [BHP_BUILDROOT_PROFILE=production|bringup] [BHP_BRINGUP_AUTHORIZED_KEYS_FILE=/abs/id.pub] $0"
  exit 0
fi

: "${BHP_BUILDROOT_DIR:?BHP_BUILDROOT_DIR is required}"
: "${BHP_BUILDROOT_OUTPUT_DIR:?BHP_BUILDROOT_OUTPUT_DIR is required}"
if ! bhp_board_profile_load_selected "$PROJECT_ROOT"; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
board_id=$BHP_BOARD_ID
defconfig_target=
buildroot_profile=${BHP_BUILDROOT_PROFILE:-production}
case "$buildroot_profile" in
  production) defconfig_key=BHP_BOARD_BUILDROOT_DEFCONFIG_PRODUCTION ;;
  bringup) defconfig_key=BHP_BOARD_BUILDROOT_DEFCONFIG_BRINGUP ;;
  *)
    bhp_fail BUILDROOT.PROFILE_INVALID "$buildroot_profile" "$TRACE_ID"
    exit 1
    ;;
esac
if ! bhp_board_profile_require_into defconfig_target "$defconfig_key"; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
if [ "$BHP_BOARD_STATUS" != active ]; then
  bhp_log warn buildroot_board_deprecated "$TRACE_ID" \
    board_id "$board_id" board_status "$BHP_BOARD_STATUS" decision ADR-0008
fi
nxp_firmware_state=not-required
nxp_firmware_decision=none
case "$(bhp_board_profile_value_state BHP_BOARD_NXP_FIRMWARE_EULA)" in
  absent) ;;
  human_decision)
    nxp_firmware_state=human_decision_required
    ;;
  measured)
    nxp_firmware_value=$(bhp_board_profile_value BHP_BOARD_NXP_FIRMWARE_EULA)
    case "$nxp_firmware_value" in
      not-required) ;;
      accepted-adr-[0-9][0-9][0-9][0-9])
        nxp_firmware_decision=${nxp_firmware_value#accepted-adr-}
        nxp_firmware_state=accepted
        ;;
      *)
        bhp_fail BUILDROOT.NXP_FIRMWARE_EULA_VALUE_INVALID "$nxp_firmware_value" "$TRACE_ID"
        exit 1
        ;;
    esac
    ;;
  *)
    bhp_fail BUILDROOT.NXP_FIRMWARE_EULA_VALUE_INVALID \
      "BHP_BOARD_NXP_FIRMWARE_EULA must be not-required, accepted-adr-NNNN, or a human-decision placeholder" \
      "$TRACE_ID"
    exit 1
    ;;
esac
if [ "$nxp_firmware_state" = accepted ]; then
  nxp_firmware_adr_count=0
  nxp_firmware_adr=
  for candidate_adr in "$PROJECT_ROOT/docs/decisions/$nxp_firmware_decision"-*.md; do
    [ -f "$candidate_adr" ] || continue
    nxp_firmware_adr=$candidate_adr
    nxp_firmware_adr_count=$((nxp_firmware_adr_count + 1))
  done
  if [ "$nxp_firmware_adr_count" -ne 1 ] ||
     ! grep -F -x '<!-- @canonical nxp-firmware-eula-acceptance -->' \
       "$nxp_firmware_adr" >/dev/null ||
     ! grep -E '^\*\*Status:\*\* accepted' "$nxp_firmware_adr" >/dev/null; then
    bhp_fail BUILDROOT.NXP_FIRMWARE_EULA_DECISION_UNRECORDED \
      "ADR $nxp_firmware_decision must exist once, carry the nxp-firmware-eula-acceptance marker, and be accepted" \
      "$TRACE_ID"
    exit 1
  fi
  nxp_firmware_fragment=$PROJECT_ROOT/system_image/buildroot_external/$(bhp_board_profile_value BHP_BOARD_BUILDROOT_BOARD_DIRECTORY)/nxp-firmware-eula.fragment
  test -s "$nxp_firmware_fragment" || {
    bhp_fail BUILDROOT.NXP_FIRMWARE_FRAGMENT_MISSING "$nxp_firmware_fragment" "$TRACE_ID"
    exit 1
  }
fi

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
  existing_board_file=$BHP_BUILDROOT_OUTPUT_DIR/.bhp-board-id
  existing_board=
  if [ -e "$existing_board_file" ] || [ -L "$existing_board_file" ]; then
    if [ ! -f "$existing_board_file" ] || [ -L "$existing_board_file" ] ||
       [ "$(wc -l <"$existing_board_file" | tr -d ' ')" -ne 1 ]; then
      bhp_fail BUILDROOT.EXISTING_BOARD_STAMP_INVALID \
        "$existing_board_file" "$TRACE_ID"
      exit 1
    fi
    existing_board=$(sed -n '1p' "$existing_board_file")
  elif [ -f "$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile" ]; then
    # Outputs configured before board profiles existed were BeagleBone Black.
    existing_board=beaglebone-black
  fi
  if [ -n "$existing_board" ] && [ "$existing_board" != "$board_id" ]; then
    bhp_fail BUILDROOT.BOARD_SWITCH_FORBIDDEN \
      "Output was configured for $existing_board; use a distinct empty output for $board_id" \
      "$TRACE_ID"
    exit 1
  fi
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
if [ "$nxp_firmware_state" = accepted ]; then
  # Recorded human decision: append NXP-licensed firmware and re-resolve.
  grep -E '^BR2_[A-Z0-9_]+=' "$nxp_firmware_fragment" \
    >>"$BHP_BUILDROOT_OUTPUT_DIR/.config"
  make -C "$BHP_BUILDROOT_DIR" \
    O="$BHP_BUILDROOT_OUTPUT_DIR" \
    BR2_DL_DIR="$BHP_BUILDROOT_OUTPUT_DIR/downloads" \
    BR2_EXTERNAL="$PROJECT_ROOT/system_image/buildroot_external" \
    olddefconfig
  grep -E '^BR2_[A-Z0-9_]+=' "$nxp_firmware_fragment" |
    while IFS= read -r nxp_firmware_line; do
      grep -F -x "$nxp_firmware_line" "$BHP_BUILDROOT_OUTPUT_DIR/.config" >/dev/null || {
        bhp_fail BUILDROOT.NXP_FIRMWARE_FRAGMENT_DROPPED "$nxp_firmware_line" "$TRACE_ID"
        exit 1
      }
    done
elif [ "$nxp_firmware_state" = human_decision_required ]; then
  bhp_log warn nxp_firmware_eula_not_accepted "$TRACE_ID" \
    board_id "$board_id" \
    decision NXP-FIRMWARE-EULA \
    consequence "image build stops before imx8-boot-sd.bin (no LPDDR4 training firmware) and ships no IW612 firmware" \
    remedy "a human records acceptance in an accepted ADR and sets BHP_BOARD_NXP_FIRMWARE_EULA=accepted-adr-NNNN"
fi
printf '%s\n' "$buildroot_profile" >"$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile"
printf '%s\n' "$board_id" >"$BHP_BUILDROOT_OUTPUT_DIR/.bhp-board-id"

bhp_log info buildroot_configure_complete "$TRACE_ID" \
  output_directory "$BHP_BUILDROOT_OUTPUT_DIR" \
  board_id "$board_id" \
  defconfig "$defconfig_target" \
  profile "$buildroot_profile" \
  nxp_firmware_eula "$nxp_firmware_state" \
  buildroot_version "$actual_buildroot_version"
