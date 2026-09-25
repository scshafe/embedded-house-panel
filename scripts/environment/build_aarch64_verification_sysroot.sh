#!/bin/sh
# Builds pinned AArch64 target libraries for the host cross-compile check.
# Inputs: optional BHP_AARCH64_SYSROOT (absolute; default
#   <project>/.build/aarch64-verification-sysroot), BHP_AARCH64_CROSS_PREFIX
#   (default aarch64-linux-gnu), BHP_BUILD_JOBS (default 2).
# Output: OpenSSL, libcurl, and nlohmann_json installed below
#   $BHP_AARCH64_SYSROOT/usr plus a JSON stamp recording every pinned commit.
# Side effects: clones pinned Git tags into $BHP_AARCH64_SYSROOT.sources and
#   writes only below the sysroot and that source directory.
# Versions match Buildroot 2026.05.1 (libopenssl 3.6.3, libcurl 8.21.0,
#   json-for-modern-cpp 3.12.0). Only Git over HTTPS to github.com is needed.
# This is verification tooling; images are built by Buildroot, never from it.
# Idempotent: yes; an existing matching stamp returns immediately.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.build_aarch64_verification_sysroot
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

openssl_url=https://github.com/openssl/openssl.git
openssl_tag=openssl-3.6.3
openssl_commit=aae016bfd52fcad2bc9657c2c782cfdf73b1ed5f
curl_url=https://github.com/curl/curl.git
curl_tag=curl-8_21_0
curl_commit=68720b4837284335b2d63cb358f8f6ce65f5bc55
json_url=https://github.com/nlohmann/json.git
json_tag=v3.12.0
json_commit=55f93686c01528224f448c19128836e7df245f72

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
  printf '%s\n' "Usage: [BHP_AARCH64_SYSROOT=/abs/prefix] [BHP_AARCH64_CROSS_PREFIX=aarch64-linux-gnu] $0"
  exit 0
fi

sysroot=${BHP_AARCH64_SYSROOT:-$PROJECT_ROOT/.build/aarch64-verification-sysroot}
cross_prefix=${BHP_AARCH64_CROSS_PREFIX:-aarch64-linux-gnu}
build_jobs=${BHP_BUILD_JOBS:-2}
case "$sysroot" in
  /*) ;;
  *) bhp_fail AARCH64_SYSROOT.PATH_NOT_ABSOLUTE "$sysroot" "$TRACE_ID"; exit 1 ;;
esac
case "$sysroot" in
  /|/usr|/usr/*|/home|/root|/Users|/opt)
    bhp_fail AARCH64_SYSROOT.PATH_UNSAFE "$sysroot" "$TRACE_ID"
    exit 1
    ;;
esac
case "$cross_prefix" in
  aarch64-*) ;;
  *) bhp_fail AARCH64_SYSROOT.PREFIX_INVALID "$cross_prefix" "$TRACE_ID"; exit 1 ;;
esac
case "$build_jobs" in
  *[!0-9]*|0|'') bhp_fail AARCH64_SYSROOT.JOBS_INVALID "$build_jobs" "$TRACE_ID"; exit 1 ;;
esac
for required_command in git make cmake perl "$cross_prefix-gcc" "$cross_prefix-g++"; do
  command -v "$required_command" >/dev/null 2>&1 || {
    bhp_fail AARCH64_SYSROOT.DEPENDENCY_MISSING "$required_command" "$TRACE_ID"
    exit 1
  }
done

stamp_path=$sysroot/.bhp-aarch64-verification-sysroot.json
expected_stamp=$(printf '{"schema_version":1,"cross_prefix":"%s","openssl":"%s","curl":"%s","nlohmann_json":"%s"}' \
  "$cross_prefix" "$openssl_commit" "$curl_commit" "$json_commit")
if [ -f "$stamp_path" ] && [ "$(cat "$stamp_path")" = "$expected_stamp" ]; then
  bhp_log info aarch64_verification_sysroot_current "$TRACE_ID" sysroot "$sysroot"
  exit 0
fi
if [ -e "$sysroot" ] && [ -n "$(find "$sysroot" -mindepth 1 -print -quit)" ]; then
  bhp_fail AARCH64_SYSROOT.UNSTAMPED_OUTPUT_NOT_EMPTY \
    "Move $sysroot aside; its content does not match the pinned stamp" "$TRACE_ID"
  exit 1
fi
source_root=$sysroot.sources
if [ -e "$source_root" ]; then
  bhp_fail AARCH64_SYSROOT.SOURCE_DIRECTORY_OCCUPIED \
    "Move $source_root aside before rebuilding" "$TRACE_ID"
  exit 1
fi
mkdir -p "$sysroot/usr" "$source_root"

clone_pinned()
{
  clone_url=$1
  clone_tag=$2
  clone_commit=$3
  clone_directory=$4
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$clone_tag" \
    "$clone_url" "$clone_directory"
  actual_commit=$(git -C "$clone_directory" rev-parse HEAD)
  [ "$actual_commit" = "$clone_commit" ] || {
    bhp_fail AARCH64_SYSROOT.PIN_MISMATCH \
      "$clone_url $clone_tag resolved to $actual_commit, expected $clone_commit" "$TRACE_ID"
    exit 1
  }
  bhp_log info aarch64_verification_source_pinned "$TRACE_ID" \
    url "$clone_url" tag "$clone_tag" commit "$actual_commit"
}

clone_pinned "$openssl_url" "$openssl_tag" "$openssl_commit" "$source_root/openssl"
clone_pinned "$curl_url" "$curl_tag" "$curl_commit" "$source_root/curl"
clone_pinned "$json_url" "$json_tag" "$json_commit" "$source_root/json"

(
  cd "$source_root/openssl"
  ./Configure linux-aarch64 "--cross-compile-prefix=$cross_prefix-" \
    "--prefix=$sysroot/usr" "--openssldir=$sysroot/etc/ssl" --libdir=lib \
    shared no-tests no-docs >/dev/null
  make -j"$build_jobs" build_sw >/dev/null
  make install_sw >/dev/null
)
bhp_log info aarch64_verification_library_installed "$TRACE_ID" library openssl

cross_cmake_arguments="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64"
# shellcheck disable=SC2086
# Word splitting of the fixed argument list is intentional.
cmake -S "$source_root/curl" -B "$source_root/curl-build" $cross_cmake_arguments \
  "-DCMAKE_C_COMPILER=$cross_prefix-gcc" \
  "-DCMAKE_FIND_ROOT_PATH=$sysroot" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  "-DCMAKE_INSTALL_PREFIX=$sysroot/usr" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_CURL_EXE=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_LIBCURL_DOCS=OFF \
  -DBUILD_MISC_DOCS=OFF \
  -DENABLE_CURL_MANUAL=OFF \
  -DCURL_USE_OPENSSL=ON \
  "-DOPENSSL_ROOT_DIR=$sysroot/usr" \
  -DCURL_USE_LIBPSL=OFF \
  -DCURL_USE_LIBSSH2=OFF \
  -DCURL_ZLIB=OFF \
  -DCURL_BROTLI=OFF \
  -DCURL_ZSTD=OFF \
  -DUSE_NGHTTP2=OFF \
  -DUSE_LIBIDN2=OFF \
  -DCURL_USE_PKGCONFIG=OFF >/dev/null
cmake --build "$source_root/curl-build" --parallel "$build_jobs" >/dev/null
cmake --install "$source_root/curl-build" >/dev/null
bhp_log info aarch64_verification_library_installed "$TRACE_ID" library libcurl

cmake -S "$source_root/json" -B "$source_root/json-build" \
  "-DCMAKE_INSTALL_PREFIX=$sysroot/usr" \
  -DJSON_BuildTests=OFF \
  -DJSON_Install=ON >/dev/null
cmake --install "$source_root/json-build" >/dev/null
bhp_log info aarch64_verification_library_installed "$TRACE_ID" library nlohmann_json

printf '%s' "$expected_stamp" >"$stamp_path"
bhp_log info aarch64_verification_sysroot_complete "$TRACE_ID" \
  sysroot "$sysroot" \
  openssl_commit "$openssl_commit" \
  curl_commit "$curl_commit" \
  nlohmann_json_commit "$json_commit" \
  hardware_evidence not_performed
