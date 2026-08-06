#!/bin/sh
# Checks Buildroot external-tree wiring and optionally evaluates the defconfig.
# Inputs: optional BHP_BUILDROOT_DIR pointing at pinned Buildroot 2026.05.1.
# Side effects: optional Kconfig output below .build/buildroot-kconfig-verification.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../build/buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_buildroot_external
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

external_tree=$PROJECT_ROOT/system_image/buildroot_external
required_files="
external.desc
external.mk
Config.in
configs/beaglebone_house_panel_defconfig
configs/beaglebone_house_panel_bringup_defconfig
board/beaglebone-house-panel/post-build.sh
board/beaglebone-house-panel/bringup/post-build.sh
board/beaglebone-house-panel/bringup/rootfs-overlay/etc/default/dropbear
board/beaglebone-house-panel/rootfs-overlay/etc/init.d/S50tailscaled
board/beaglebone-house-panel/rootfs-overlay/usr/libexec/beaglebone-house-panel/classify-touch-input
package/bhp-panel-supervisor/Config.in
package/bhp-panel-supervisor/bhp-panel-supervisor.mk
package/bhp-presence-sensor/Config.in
package/bhp-presence-sensor/bhp-presence-sensor.mk
package/bhp-vocabulary-panel/Config.in
package/bhp-vocabulary-panel/bhp-vocabulary-panel.mk
package/lvgl/Config.in
package/lvgl/lvgl.mk
"

for relative_path in $required_files; do
  test -s "$external_tree/$relative_path" || {
    bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$relative_path" "$TRACE_ID"
    exit 1
  }
done

for package_symbol in BR2_PACKAGE_BHP_PANEL_SUPERVISOR BR2_PACKAGE_BHP_PRESENCE_SENSOR BR2_PACKAGE_BHP_VOCABULARY_PANEL BR2_PACKAGE_LVGL; do
  grep -F "$package_symbol" "$external_tree/configs/beaglebone_house_panel_defconfig" >/dev/null || {
    bhp_fail VERIFY_BUILDROOT.PACKAGE_NOT_SELECTED "$package_symbol" "$TRACE_ID"
    exit 1
  }
done

if [ -n "${BHP_BUILDROOT_DIR:-}" ]; then
  test -f "$BHP_BUILDROOT_DIR/Makefile" || {
    bhp_fail VERIFY_BUILDROOT.SOURCE_INVALID "$BHP_BUILDROOT_DIR" "$TRACE_ID"
    exit 1
  }
  actual_buildroot_version=$(
    bhp_buildroot_read_source_version "$BHP_BUILDROOT_DIR"
  ) || {
    bhp_fail VERIFY_BUILDROOT.VERSION_UNREADABLE "$BHP_BUILDROOT_DIR" "$TRACE_ID"
    exit 1
  }
  if [ "$actual_buildroot_version" != "$BHP_PINNED_BUILDROOT_VERSION" ]; then
    bhp_fail VERIFY_BUILDROOT.VERSION_MISMATCH \
      "Expected $BHP_PINNED_BUILDROOT_VERSION; found $actual_buildroot_version" \
      "$TRACE_ID"
    exit 1
  fi
  actual_buildroot_source_id=$(
    bhp_buildroot_read_source_id "$BHP_BUILDROOT_DIR" \
      "$PROJECT_ROOT/scripts/build/buildroot_source_id.sh"
  ) || {
    bhp_fail VERIFY_BUILDROOT.SOURCE_ID_UNREADABLE "$BHP_BUILDROOT_DIR" "$TRACE_ID"
    exit 1
  }
  if [ "$actual_buildroot_source_id" != "$BHP_PINNED_BUILDROOT_SOURCE_ID" ]; then
    bhp_fail VERIFY_BUILDROOT.SOURCE_ID_MISMATCH \
      "Buildroot tree does not match the exact verified archive extraction" \
      "$TRACE_ID"
    exit 1
  fi
  output_directory=$PROJECT_ROOT/.build/buildroot-kconfig-verification
  mkdir -p "$output_directory"
  make -C "$BHP_BUILDROOT_DIR" \
    O="$output_directory" \
    BR2_DL_DIR="$output_directory/downloads" \
    BR2_EXTERNAL="$external_tree" \
    beaglebone_house_panel_defconfig
  test -s "$output_directory/.config"
  for expected_config_line in \
    'BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.18.21"' \
    'BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2026.04"' \
    'BR2_PACKAGE_BHP_PRESENCE_SENSOR=y' \
    'BR2_PACKAGE_LVGL=y' \
    'BR2_PACKAGE_CHRONY=y' \
    'BR2_PACKAGE_TAILSCALE=y' \
    'BR2_SYSTEM_DHCP="eth0"'; do
    grep -F -x "$expected_config_line" "$output_directory/.config" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.CONFIG_VALUE_MISSING "$expected_config_line" "$TRACE_ID"
      exit 1
    }
  done
  for forbidden_config_line in \
    'BR2_TARGET_ENABLE_ROOT_LOGIN=y' \
    'BR2_PACKAGE_DROPBEAR=y' \
    'BR2_PACKAGE_LIBCURL_PROXY_SUPPORT=y' \
    'BR2_PACKAGE_LIBCURL_COOKIES_SUPPORT=y' \
    'BR2_PACKAGE_LIBCURL_EXTRA_PROTOCOLS_FEATURES=y'; do
    if grep -F -x "$forbidden_config_line" "$output_directory/.config" >/dev/null; then
      bhp_fail VERIFY_BUILDROOT.UNNEEDED_CURL_FEATURE_ENABLED "$forbidden_config_line" "$TRACE_ID"
      exit 1
    fi
  done
  if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    for expected_toolchain_line in \
      'BR2_TOOLCHAIN_EXTERNAL_BOOTLIN=y' \
      'BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_ARMV7_EABIHF_GLIBC_STABLE=y' \
      'BR2_TOOLCHAIN_USES_GLIBC=y' \
      'BR2_INSTALL_LIBSTDCPP=y' \
      'BR2_TOOLCHAIN_GCC_AT_LEAST_9=y' \
      'BR2_PACKAGE_BHP_PANEL_SUPERVISOR=y' \
      'BR2_PACKAGE_BHP_VOCABULARY_PANEL=y' \
      'BR2_PACKAGE_LIBCURL_FORCE_TLS=y' \
      'BR2_PACKAGE_JSON_FOR_MODERN_CPP=y'; do
      grep -F -x "$expected_toolchain_line" "$output_directory/.config" >/dev/null || {
        bhp_fail VERIFY_BUILDROOT.TOOLCHAIN_SELECTION_MISSING "$expected_toolchain_line" "$TRACE_ID"
        exit 1
      }
    done
  else
    bhp_log warn buildroot_toolchain_resolution_skipped "$TRACE_ID" \
      reason Bootlin_prebuilt_toolchain_requires_Linux_x86_64 \
      host_os "$(uname -s)" \
      host_arch "$(uname -m)"
  fi
  registered_targets=$(make -s -C "$BHP_BUILDROOT_DIR" \
    O="$output_directory" \
    BR2_DL_DIR="$output_directory/downloads" \
    BR2_EXTERNAL="$external_tree" \
    show-targets)
  expected_targets="bhp-presence-sensor chrony lvgl tailscale"
  if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    expected_targets="$expected_targets bhp-panel-supervisor bhp-vocabulary-panel"
  fi
  for expected_target in $expected_targets; do
    printf '%s\n' "$registered_targets" | tr ' ' '\n' | grep -F -x "$expected_target" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.TARGET_NOT_REGISTERED "$expected_target" "$TRACE_ID"
      exit 1
    }
  done
  bringup_output_directory=$PROJECT_ROOT/.build/buildroot-bringup-kconfig-verification
  mkdir -p "$bringup_output_directory"
  make -C "$BHP_BUILDROOT_DIR" \
    O="$bringup_output_directory" \
    BR2_DL_DIR="$bringup_output_directory/downloads" \
    BR2_EXTERNAL="$external_tree" \
    beaglebone_house_panel_bringup_defconfig
  for expected_bringup_config_line in \
    'BR2_TARGET_GENERIC_HOSTNAME="beaglebone-house-panel-bringup"' \
    'BR2_PACKAGE_DROPBEAR=y' \
    'BR2_PACKAGE_DROPBEAR_DISABLE_REVERSEDNS=y' \
    'BR2_PACKAGE_RSYNC=y' \
    'BR2_PACKAGE_DTC=y' \
    'BR2_PACKAGE_I2C_TOOLS=y' \
    'BR2_PACKAGE_STRACE=y'; do
    grep -F -x "$expected_bringup_config_line" \
      "$bringup_output_directory/.config" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.BRINGUP_CONFIG_VALUE_MISSING \
        "$expected_bringup_config_line" "$TRACE_ID"
      exit 1
    }
  done
  if grep -F -x 'BR2_TARGET_ENABLE_ROOT_LOGIN=y' \
    "$bringup_output_directory/.config" >/dev/null; then
    bhp_fail VERIFY_BUILDROOT.BRINGUP_PASSWORD_ROOT_ENABLED \
      "Bring-up must retain locked root password authentication" "$TRACE_ID"
    exit 1
  fi
  bhp_log info buildroot_defconfig_evaluated "$TRACE_ID" \
    output_directory "$output_directory" \
    bringup_output_directory "$bringup_output_directory" \
    buildroot_version "$actual_buildroot_version" \
    buildroot_source_id "$actual_buildroot_source_id"
else
  bhp_log warn buildroot_defconfig_evaluation_skipped "$TRACE_ID" reason BHP_BUILDROOT_DIR_not_set
fi

bhp_log info buildroot_external_verification_complete "$TRACE_ID" external_tree "$external_tree"
