#!/bin/sh
# Cross-compiles the applications for the active AArch64 board and checks ELF identity.
# Inputs: optional BHP_BOARD; optional BHP_AARCH64_CROSS_PREFIX (default
#   aarch64-linux-gnu); optional BHP_AARCH64_SYSROOT (default
#   <project>/.build/aarch64-verification-sysroot when its pinned stamp exists).
# Scope: with a sysroot (see scripts/environment/build_aarch64_verification_sysroot.sh)
#   builds panel_supervisord, bhp-panel-ui, and the vocabulary terminal plus the
#   HTTP Lexinet adapter; without one, builds the OpenSSL-free vocabulary slice.
#   A host without the cross compiler logs a structured skip.
# Side effects: writes only below .build/linux-aarch64-cross. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../boards/board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_aarch64_cross_build
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if ! bhp_board_profile_load_selected "$PROJECT_ROOT"; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
target_arch=
target_cpu=
elf_machine=
if ! bhp_board_profile_require_into target_arch BHP_BOARD_TARGET_ARCH ||
   ! bhp_board_profile_require_into target_cpu BHP_BOARD_TARGET_CPU ||
   ! bhp_board_profile_require_into elf_machine BHP_BOARD_TARGET_ELF_MACHINE; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
if [ "$target_arch" != aarch64 ]; then
  bhp_log warn aarch64_cross_build_skipped "$TRACE_ID" \
    board_id "$BHP_BOARD_ID" reason board_is_not_aarch64 target_arch "$target_arch"
  exit 0
fi

cross_prefix=${BHP_AARCH64_CROSS_PREFIX:-aarch64-linux-gnu}
if ! command -v "$cross_prefix-g++" >/dev/null 2>&1 ||
   ! command -v "$cross_prefix-gcc" >/dev/null 2>&1; then
  bhp_log warn aarch64_cross_build_skipped "$TRACE_ID" \
    board_id "$BHP_BOARD_ID" reason cross_compiler_missing \
    compiler "$cross_prefix-g++" \
    remedy "install a GNU AArch64 cross compiler (Debian/Ubuntu: g++-aarch64-linux-gnu)"
  exit 0
fi
command -v cmake >/dev/null 2>&1 || {
  bhp_fail VERIFY_AARCH64.CMAKE_MISSING "cmake is required" "$TRACE_ID"
  exit 1
}

sysroot=${BHP_AARCH64_SYSROOT:-}
if [ -z "$sysroot" ] &&
   [ -f "$PROJECT_ROOT/.build/aarch64-verification-sysroot/.bhp-aarch64-verification-sysroot.json" ]; then
  sysroot=$PROJECT_ROOT/.build/aarch64-verification-sysroot
fi
build_directory=$PROJECT_ROOT/.build/linux-aarch64-cross
case "$build_directory" in
  "$PROJECT_ROOT"/.build/*) ;;
  *) bhp_fail VERIFY_AARCH64.BUILD_DIRECTORY_UNSAFE "$build_directory" "$TRACE_ID"; exit 1 ;;
esac
if [ -d "$build_directory" ]; then
  find "$build_directory" -depth -mindepth 1 -delete
fi

BHP_AARCH64_CROSS_PREFIX=$cross_prefix
BHP_AARCH64_TARGET_CPU=$target_cpu
BHP_AARCH64_SYSROOT=$sysroot
export BHP_AARCH64_CROSS_PREFIX BHP_AARCH64_TARGET_CPU BHP_AARCH64_SYSROOT
unset CMAKE_PREFIX_PATH CMAKE_LIBRARY_PATH CMAKE_INCLUDE_PATH PKG_CONFIG_PATH

if [ -n "$sysroot" ]; then
  scope=supervisor_ui_vocabulary_http
  cmake --preset linux-aarch64-cross -DBHP_BUILD_HTTP_LEXINET_CLIENT=ON >/dev/null
  expected_binaries="panel_supervisor/panel_supervisord panel_ui/bhp-panel-ui vocabulary_panel/vocabulary_panel_terminal"
  expected_libraries="vocabulary_panel/libvocabulary_panel_http_lexinet.a"
else
  scope=vocabulary_only_no_sysroot
  cmake --preset linux-aarch64-cross \
    -DBHP_BUILD_PANEL_SUPERVISOR=OFF -DBHP_BUILD_PANEL_UI=OFF >/dev/null
  expected_binaries="vocabulary_panel/vocabulary_panel_terminal"
  expected_libraries=
fi
cmake --build --preset linux-aarch64-cross --parallel "${BHP_BUILD_JOBS:-2}" >/dev/null

elf_machine_of()
{
  python3 - "$1" <<'PY_ELF_MACHINE'
import struct
import sys

with open(sys.argv[1], "rb") as elf_file:
    header = elf_file.read(20)
if header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1:
    print("not-elf64-le")
else:
    print(struct.unpack_from("<H", header, 18)[0])
PY_ELF_MACHINE
}

verified=
for relative_binary in $expected_binaries; do
  binary_path=$build_directory/$relative_binary
  test -x "$binary_path" || {
    bhp_fail VERIFY_AARCH64.BINARY_MISSING "$relative_binary" "$TRACE_ID"
    exit 1
  }
  actual_machine=$(elf_machine_of "$binary_path")
  [ "$actual_machine" = "$elf_machine" ] || {
    bhp_fail VERIFY_AARCH64.ELF_MACHINE_MISMATCH \
      "$relative_binary has e_machine $actual_machine; board expects $elf_machine" "$TRACE_ID"
    exit 1
  }
  verified="$verified ${relative_binary##*/}"
done
for relative_library in $expected_libraries; do
  test -s "$build_directory/$relative_library" || {
    bhp_fail VERIFY_AARCH64.LIBRARY_MISSING "$relative_library" "$TRACE_ID"
    exit 1
  }
  verified="$verified ${relative_library##*/}"
done

bhp_log info aarch64_cross_build_verified "$TRACE_ID" \
  board_id "$BHP_BOARD_ID" \
  target_cpu "$target_cpu" \
  elf_machine "$elf_machine" \
  scope "$scope" \
  sysroot "${sysroot:-none}" \
  artifacts "${verified# }" \
  lvgl_renderer not_built_without_target_libdrm \
  hardware_evidence not_performed
