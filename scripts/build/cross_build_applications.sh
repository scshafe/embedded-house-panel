#!/bin/sh
# Cross-builds user-space applications with the generated Buildroot SDK.
# Inputs: BHP_BUILDROOT_HOST_DIR; optional BHP_BOARD (default: the active board
#   profile); optional BHP_BUILDROOT_CROSS_PREFIX, which must equal the board
#   profile's BHP_BOARD_BUILDROOT_CROSS_PREFIX when set.
# Side effects: writes only to .build/<board CMake preset>.
# Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
# shellcheck source=../deploy/application_bundle_manifest.sh
. "$PROJECT_ROOT/scripts/deploy/application_bundle_manifest.sh"
# shellcheck source=../boards/board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.cross_build
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' "Usage: BHP_BUILDROOT_HOST_DIR=/abs/output/host [BHP_BOARD=frdm-imx8mp] [BHP_BUILDROOT_CROSS_PREFIX=aarch64-buildroot-linux-gnu] $0"
  exit 0
fi

: "${BHP_BUILDROOT_HOST_DIR:?BHP_BUILDROOT_HOST_DIR is required}"
if ! bhp_board_profile_load_selected "$PROJECT_ROOT"; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
board_cross_prefix=
board_cmake_preset=
if ! bhp_board_profile_require_into board_cross_prefix BHP_BOARD_BUILDROOT_CROSS_PREFIX ||
   ! bhp_board_profile_require_into board_cmake_preset BHP_BOARD_CMAKE_PRESET; then
  bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
if [ -z "${BHP_BUILDROOT_CROSS_PREFIX:-}" ]; then
  BHP_BUILDROOT_CROSS_PREFIX=$board_cross_prefix
elif [ "$BHP_BUILDROOT_CROSS_PREFIX" != "$board_cross_prefix" ]; then
  bhp_fail CROSS_BUILD.PREFIX_BOARD_MISMATCH \
    "$BHP_BUILDROOT_CROSS_PREFIX differs from $BHP_BOARD_ID profile prefix $board_cross_prefix" \
    "$TRACE_ID"
  exit 1
fi
if [ "$BHP_BOARD_STATUS" != active ]; then
  bhp_log warn cross_build_board_deprecated "$TRACE_ID" \
    board_id "$BHP_BOARD_ID" board_status "$BHP_BOARD_STATUS" decision ADR-0008
fi
case "$BHP_BUILDROOT_HOST_DIR" in
  /*) ;;
  *) bhp_fail CROSS_BUILD.HOST_DIR_NOT_ABSOLUTE "$BHP_BUILDROOT_HOST_DIR" "$TRACE_ID"; exit 1 ;;
esac
case "$BHP_BUILDROOT_HOST_DIR" in
  /|/Users|/home|/root|/usr|/opt)
    bhp_fail CROSS_BUILD.HOST_DIR_UNSAFE "$BHP_BUILDROOT_HOST_DIR" "$TRACE_ID"
    exit 1
    ;;
esac
case "$BHP_BUILDROOT_CROSS_PREFIX" in
  ''|*[!A-Za-z0-9._+-]*)
    bhp_fail CROSS_BUILD.PREFIX_INVALID "$BHP_BUILDROOT_CROSS_PREFIX" "$TRACE_ID"
    exit 1
    ;;
esac
command -v cmake >/dev/null 2>&1 || {
  bhp_fail CROSS_BUILD.CMAKE_MISSING "cmake is required" "$TRACE_ID"
  exit 1
}
for required_tool in gcc g++ ar ranlib strip; do
  required_tool_path=$BHP_BUILDROOT_HOST_DIR/bin/$BHP_BUILDROOT_CROSS_PREFIX-$required_tool
  test -x "$required_tool_path" || {
    bhp_fail CROSS_BUILD.TOOL_MISSING "$required_tool_path" "$TRACE_ID"
    exit 1
  }
done
buildroot_pkg_config=$BHP_BUILDROOT_HOST_DIR/bin/pkg-config
test -x "$buildroot_pkg_config" || {
  bhp_fail CROSS_BUILD.PKG_CONFIG_MISSING "$buildroot_pkg_config" "$TRACE_ID"
  exit 1
}
buildroot_sysroot=$BHP_BUILDROOT_HOST_DIR/$BHP_BUILDROOT_CROSS_PREFIX/sysroot
test -d "$buildroot_sysroot" || {
  bhp_fail CROSS_BUILD.SYSROOT_MISSING "$buildroot_sysroot" "$TRACE_ID"
  exit 1
}

export BHP_BUILDROOT_HOST_DIR BHP_BUILDROOT_CROSS_PREFIX
PKG_CONFIG=$buildroot_pkg_config
PKG_CONFIG_PATH=
PKG_CONFIG_DIR=
PKG_CONFIG_SYSROOT_DIR=$buildroot_sysroot
PKG_CONFIG_LIBDIR=$buildroot_sysroot/usr/lib/pkgconfig:$buildroot_sysroot/usr/share/pkgconfig:$buildroot_sysroot/lib/pkgconfig
export PKG_CONFIG PKG_CONFIG_PATH PKG_CONFIG_DIR PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR
unset CMAKE_PREFIX_PATH CMAKE_LIBRARY_PATH CMAKE_INCLUDE_PATH
unset CMAKE_FRAMEWORK_PATH CMAKE_APPBUNDLE_PATH
build_jobs=${BHP_BUILD_JOBS:-2}
case "$build_jobs" in
  *[!0-9]*|0|'') bhp_fail CROSS_BUILD.JOBS_INVALID "$build_jobs" "$TRACE_ID"; exit 1 ;;
esac
cd "$PROJECT_ROOT"
build_directory=$PROJECT_ROOT/.build/$board_cmake_preset
cache_path=$build_directory/CMakeCache.txt
bhp_cross_cache_value()
{
  BHP_CROSS_CACHE_KEY=$1
  sed -n "s/^$BHP_CROSS_CACHE_KEY:[^=]*=//p" "$cache_path" | sed -n '1p'
}

bhp_cross_cache_target_paths_are_valid()
{
  BHP_CROSS_CACHE_PATH_KEY=$1
  BHP_CROSS_CACHE_PATH_VALUE=$(bhp_cross_cache_value "$BHP_CROSS_CACHE_PATH_KEY")
  [ -n "$BHP_CROSS_CACHE_PATH_VALUE" ] || return 1
  BHP_CROSS_CACHE_OLD_IFS=$IFS
  IFS=';'
  # Field splitting is intentional for CMake path lists.
  # shellcheck disable=SC2086
  set -- $BHP_CROSS_CACHE_PATH_VALUE
  IFS=$BHP_CROSS_CACHE_OLD_IFS
  for BHP_CROSS_CACHE_PATH in "$@"; do
    case "$BHP_CROSS_CACHE_PATH" in
      "$buildroot_sysroot"|"$buildroot_sysroot"/*) ;;
      *) return 1 ;;
    esac
  done
}

if [ -f "$cache_path" ]; then
  cached_cxx_compiler=$(bhp_cross_cache_value CMAKE_CXX_COMPILER)
  cached_pkg_config=$(bhp_cross_cache_value PKG_CONFIG_EXECUTABLE)
  if [ "$cached_cxx_compiler" != "$BHP_BUILDROOT_HOST_DIR/bin/$BHP_BUILDROOT_CROSS_PREFIX-g++" ] ||
     [ "$cached_pkg_config" != "$buildroot_pkg_config" ]; then
    bhp_fail CROSS_BUILD.CACHE_TOOLCHAIN_MISMATCH \
      "Move the existing cross-build directory aside; its SDK identity differs" \
      "$TRACE_ID"
    exit 1
  fi
fi
cmake --preset "$board_cmake_preset"
test -s "$cache_path" || {
  bhp_fail CROSS_BUILD.CACHE_MISSING "$cache_path" "$TRACE_ID"
  exit 1
}
configured_cxx_compiler=$(bhp_cross_cache_value CMAKE_CXX_COMPILER)
configured_pkg_config=$(bhp_cross_cache_value PKG_CONFIG_EXECUTABLE)
configured_sysroot=$(bhp_cross_cache_value CMAKE_SYSROOT)
if [ "$configured_cxx_compiler" != "$BHP_BUILDROOT_HOST_DIR/bin/$BHP_BUILDROOT_CROSS_PREFIX-g++" ] ||
   [ "$configured_pkg_config" != "$buildroot_pkg_config" ] ||
   [ "$configured_sysroot" != "$buildroot_sysroot" ]; then
  bhp_fail CROSS_BUILD.CONFIGURED_TOOLCHAIN_INVALID \
    "CMake did not retain the exact Buildroot compiler, pkg-config, and sysroot" \
    "$TRACE_ID"
  exit 1
fi
for target_cache_path_key in \
  CURL_INCLUDE_DIR \
  CURL_LIBRARY_RELEASE \
  nlohmann_json_DIR \
  pkgcfg_lib_BHP_LIBDRM_drm \
  pkgcfg_lib_BHP_LVGL_lvgl; do
  if ! bhp_cross_cache_target_paths_are_valid "$target_cache_path_key"; then
    bhp_fail CROSS_BUILD.HOST_PACKAGE_DISCOVERY_DETECTED \
      "$target_cache_path_key must resolve entirely below the Buildroot sysroot" \
      "$TRACE_ID"
    exit 1
  fi
done
cmake --build --preset "$board_cmake_preset" --parallel "$build_jobs"

bundle_directory=$build_directory/stage
pending_bundle_directory=$(mktemp -d "$build_directory/stage.pending.XXXXXX") || {
  bhp_fail CROSS_BUILD.STAGE_CREATE_FAILED "$build_directory" "$TRACE_ID"
  exit 1
}
DESTDIR=$pending_bundle_directory cmake --install "$build_directory" --prefix /usr
if ! bhp_bundle_create_manifest "$pending_bundle_directory"; then
  bhp_fail "$BHP_BUNDLE_ERROR_CODE" "$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi

if [ -e "$bundle_directory" ] || [ -L "$bundle_directory" ]; then
  if ! bhp_bundle_verify_exact "$bundle_directory"; then
    bhp_fail CROSS_BUILD.EXISTING_STAGE_UNVERIFIED \
      "$BHP_BUNDLE_ERROR_CODE:$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  fi
  retired_bundle_directory=$build_directory/stage.retired.$$
  if [ -e "$retired_bundle_directory" ] || [ -L "$retired_bundle_directory" ]; then
    bhp_fail CROSS_BUILD.RETIRED_STAGE_OCCUPIED "$retired_bundle_directory" "$TRACE_ID"
    exit 1
  fi
  mv -- "$bundle_directory" "$retired_bundle_directory"
  if ! mv -- "$pending_bundle_directory" "$bundle_directory"; then
    mv -- "$retired_bundle_directory" "$bundle_directory" || true
    bhp_fail CROSS_BUILD.STAGE_PUBLISH_FAILED "$bundle_directory" "$TRACE_ID"
    exit 1
  fi
  if ! bhp_bundle_remove_verified "$retired_bundle_directory"; then
    bhp_fail CROSS_BUILD.RETIRED_STAGE_CLEANUP_FAILED \
      "$BHP_BUNDLE_ERROR_CODE:$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  fi
else
  if ! mv -- "$pending_bundle_directory" "$bundle_directory"; then
    bhp_fail CROSS_BUILD.STAGE_PUBLISH_FAILED "$bundle_directory" "$TRACE_ID"
    exit 1
  fi
fi

if ! bhp_bundle_verify_exact "$bundle_directory"; then
  bhp_fail "$BHP_BUNDLE_ERROR_CODE" "$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
manifest_path=$bundle_directory/$BHP_BUNDLE_MANIFEST_FILENAME

bhp_log info cross_build_complete "$TRACE_ID" \
  board_id "$BHP_BOARD_ID" \
  cmake_preset "$board_cmake_preset" \
  build_directory "$build_directory" \
  bundle_directory "$bundle_directory" \
  manifest "$manifest_path"
