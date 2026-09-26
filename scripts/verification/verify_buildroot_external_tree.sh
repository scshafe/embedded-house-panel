#!/bin/sh
# Checks Buildroot external-tree wiring for every board profile and optionally
# evaluates each board's defconfigs.
# Inputs: optional BHP_BUILDROOT_DIR pointing at pinned Buildroot 2026.05.1;
#   optional BHP_BUILDROOT_EVALUATE_BOARDS (space-separated ids; default: all).
# Side effects: optional Kconfig output below .build/buildroot-kconfig-verification-<board>
#   and .build/buildroot-bringup-kconfig-verification-<board>.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../build/buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
# shellcheck source=../boards/board_profile.sh
. "$PROJECT_ROOT/scripts/boards/board_profile.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_buildroot_external
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

external_tree=$PROJECT_ROOT/system_image/buildroot_external
common_required_files="
external.desc
external.mk
Config.in
board/beaglebone-house-panel/post-build.sh
board/beaglebone-house-panel/bringup/post-build.sh
board/beaglebone-house-panel/bringup/rootfs-overlay/etc/default/dropbear
board/beaglebone-house-panel/rootfs-overlay/etc/init.d/S50tailscaled
board/beaglebone-house-panel/rootfs-overlay/etc/init.d/S75bhp-panel-ui
board/beaglebone-house-panel/service-environment/panel-supervisor.env.example.in
board/beaglebone-house-panel/service-environment/presence-sensor.env.example.in
board/beaglebone-house-panel/service-environment/panel-ui.env.example.in
board/beaglebone-house-panel/rootfs-overlay/usr/libexec/beaglebone-house-panel/classify-touch-input
package/bhp-panel-supervisor/Config.in
package/bhp-panel-supervisor/bhp-panel-supervisor.mk
package/bhp-panel-ui/Config.in
package/bhp-panel-ui/bhp-panel-ui.mk
package/bhp-panel-ui/README.md
package/bhp-presence-sensor/Config.in
package/bhp-presence-sensor/bhp-presence-sensor.mk
package/bhp-vocabulary-panel/Config.in
package/bhp-vocabulary-panel/bhp-vocabulary-panel.mk
package/bhp-board-device-tree/Config.in
package/bhp-board-device-tree/bhp-board-device-tree.mk
package/bhp-nxp-mwifiex/Config.in
package/bhp-nxp-mwifiex/bhp-nxp-mwifiex.mk
package/bhp-nxp-mwifiex/bhp-nxp-mwifiex.hash
package/bhp-nxp-iw612-firmware/Config.in
package/bhp-nxp-iw612-firmware/bhp-nxp-iw612-firmware.mk
package/bhp-nxp-iw612-firmware/bhp-nxp-iw612-firmware.hash
package/lvgl/Config.in
package/lvgl/lvgl.mk
"
for relative_path in $common_required_files; do
  test -s "$external_tree/$relative_path" || {
    bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$relative_path" "$TRACE_ID"
    exit 1
  }
done

# NXP-licensed firmware never appears in a checked-in defconfig; only the
# human-decision fragment may select it.
nxp_licensed_selections="
BR2_PACKAGE_FIRMWARE_IMX=y
BR2_PACKAGE_BHP_NXP_IW612_FIRMWARE=y
BR2_PACKAGE_NXP_BT_WIFI_FIRMWARE=y
"
for defconfig_path in "$external_tree"/configs/*_defconfig; do
  for nxp_licensed_selection in $nxp_licensed_selections; do
    if grep -F -x "$nxp_licensed_selection" "$defconfig_path" >/dev/null; then
      bhp_fail VERIFY_BUILDROOT.NXP_LICENSED_FIRMWARE_IN_DEFCONFIG \
        "${defconfig_path##*/}: $nxp_licensed_selection needs a recorded human EULA decision" \
        "$TRACE_ID"
      exit 1
    fi
  done
done

require_line()
{
  required_file=$1
  required_line=$2
  required_error=$3
  grep -F -x "$required_line" "$required_file" >/dev/null || {
    bhp_fail "$required_error" "${required_file##*/}: $required_line" "$TRACE_ID"
    exit 1
  }
}

verified_boards=
for board_profile_path in "$PROJECT_ROOT"/config/boards/*.env; do
  board_id=${board_profile_path##*/}
  board_id=${board_id%.env}
  bhp_board_profile_load "$PROJECT_ROOT" "$board_id" || {
    bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  }
  # Debian/QEMU simulation has no production image recipe or hardware gates.
  [ "$BHP_BOARD_STATUS" != simulation ] || continue
  board_directory=$BHP_BOARD_BUILDROOT_BOARD_DIRECTORY
  kernel_fragment=$BHP_BOARD_SUSPEND_KCONFIG_FRAGMENT
  test -d "$external_tree/$board_directory" || {
    bhp_fail VERIFY_BUILDROOT.BOARD_DIRECTORY_MISSING "$board_id: $board_directory" "$TRACE_ID"
    exit 1
  }
  test -s "$external_tree/$kernel_fragment" || {
    bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$board_id: $kernel_fragment" "$TRACE_ID"
    exit 1
  }
  "$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh" --board "$board_id" \
    --config "$external_tree/$kernel_fragment" --mode fragment >/dev/null
  for defconfig_name in "$BHP_BOARD_BUILDROOT_DEFCONFIG_PRODUCTION" \
    "$BHP_BOARD_BUILDROOT_DEFCONFIG_BRINGUP"; do
    defconfig_path=$external_tree/configs/$defconfig_name
    test -s "$defconfig_path" || {
      bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$board_id: configs/$defconfig_name" "$TRACE_ID"
      exit 1
    }
    for expected_defconfig_line in \
      "BR2_BHP_BOARD_ID=\"$board_id\"" \
      "BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"\$(BR2_EXTERNAL_BHP_PATH)/$kernel_fragment\"" \
      'BR2_TARGET_ROOTFS_EXT2_SIZE="512M"' \
      "BR2_SYSTEM_DHCP=\"$BHP_BOARD_BRINGUP_DHCP_INTERFACE\"" \
      BR2_PACKAGE_BHP_PANEL_SUPERVISOR=y \
      BR2_PACKAGE_BHP_PANEL_UI=y \
      BR2_PACKAGE_BHP_PRESENCE_SENSOR=y \
      BR2_PACKAGE_BHP_VOCABULARY_PANEL=y \
      BR2_PACKAGE_LVGL=y; do
      require_line "$defconfig_path" "$expected_defconfig_line" \
        VERIFY_BUILDROOT.DEFCONFIG_VALUE_MISSING
    done
  done
  case "$board_id" in
    beaglebone-black)
      expected_post_script_args="BR2_ROOTFS_POST_SCRIPT_ARGS=\"-c \$(BR2_EXTERNAL_BHP_PATH)/$board_directory/genimage.cfg\""
      for defconfig_name in "$BHP_BOARD_BUILDROOT_DEFCONFIG_PRODUCTION" \
        "$BHP_BOARD_BUILDROOT_DEFCONFIG_BRINGUP"; do
        require_line "$external_tree/configs/$defconfig_name" \
          "$expected_post_script_args" VERIFY_BUILDROOT.DEFCONFIG_VALUE_MISSING
      done
      ;;
    *)
      for defconfig_name in "$BHP_BOARD_BUILDROOT_DEFCONFIG_PRODUCTION" \
        "$BHP_BOARD_BUILDROOT_DEFCONFIG_BRINGUP"; do
        for expected_defconfig_line in \
          "BR2_ROOTFS_POST_IMAGE_SCRIPT=\"\$(BR2_EXTERNAL_BHP_PATH)/$board_directory/post-image.sh\"" \
          "BR2_GLOBAL_PATCH_DIR=\"\$(BR2_EXTERNAL_BHP_PATH)/$board_directory/patches\"" \
          BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y \
          BR2_PACKAGE_BHP_BOARD_DEVICE_TREE=y \
          BR2_DOWNLOAD_FORCE_CHECK_HASHES=y; do
          require_line "$external_tree/configs/$defconfig_name" \
            "$expected_defconfig_line" VERIFY_BUILDROOT.DEFCONFIG_VALUE_MISSING
        done
        kernel_base_dts=${BHP_BOARD_KERNEL_BASE_DTB%.dtb}
        require_line "$external_tree/configs/$defconfig_name" \
          "BR2_LINUX_KERNEL_INTREE_DTS_NAME=\"$kernel_base_dts\"" \
          VERIFY_BUILDROOT.DEFCONFIG_VALUE_MISSING
      done
      for board_file in post-image.sh extlinux.conf nxp-firmware-eula.fragment; do
        test -s "$external_tree/$board_directory/$board_file" || {
          bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$board_id: $board_file" "$TRACE_ID"
          exit 1
        }
      done
      test -x "$external_tree/$board_directory/post-image.sh" || {
        bhp_fail VERIFY_BUILDROOT.POST_IMAGE_NOT_EXECUTABLE "$board_id" "$TRACE_ID"
        exit 1
      }
      require_line "$external_tree/$board_directory/extlinux.conf" \
        "	fdt /$BHP_BOARD_BOOT_DTB" VERIFY_BUILDROOT.EXTLINUX_DTB_INVALID
      for patch_hash in linux/linux.hash uboot/uboot.hash \
        arm-trusted-firmware/arm-trusted-firmware.hash; do
        grep -E '^sha256  [0-9a-f]{64}  ' "$external_tree/$board_directory/patches/$patch_hash" \
          >/dev/null || {
          bhp_fail VERIFY_BUILDROOT.PINNED_HASH_MISSING "$board_id: $patch_hash" "$TRACE_ID"
          exit 1
        }
      done
      ;;
  esac

  genimage_configuration=$external_tree/$board_directory/genimage.cfg
  test -s "$genimage_configuration" || {
    bhp_fail VERIFY_BUILDROOT.REQUIRED_FILE_MISSING "$board_id: genimage.cfg" "$TRACE_ID"
    exit 1
  }
  genimage_dtb_references=$(grep -E -o '[[:alnum:]_.+-]+\.dtb' \
    "$genimage_configuration" || true)
  if [ "$genimage_dtb_references" != "$BHP_BOARD_BOOT_DTB" ]; then
    bhp_fail VERIFY_BUILDROOT.GENIMAGE_DTB_SET_INVALID \
      "$board_id: expected only $BHP_BOARD_BOOT_DTB; found: ${genimage_dtb_references:-none}" \
      "$TRACE_ID"
    exit 1
  fi
  for expected_genimage_pattern in \
    '[[:space:]]*image = "rootfs\.ext4"' \
    '[[:space:]]*size = 512M'; do
    grep -E -x "$expected_genimage_pattern" "$genimage_configuration" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.GENIMAGE_VALUE_MISSING \
        "$board_id: $expected_genimage_pattern" "$TRACE_ID"
      exit 1
    }
  done
  if [ "$BHP_BOARD_TARGET_ARCH" = aarch64 ]; then
    grep -E -x '[[:space:]]*offset = 32K' "$genimage_configuration" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.GENIMAGE_BOOT_OFFSET_INVALID \
        "$board_id: i.MX 8M Plus boots imx8-boot-sd.bin at 32 KiB" "$TRACE_ID"
      exit 1
    }
  fi
  verified_boards="$verified_boards $board_id"
done

evaluate_board()
{
  evaluation_board=$1
  bhp_board_profile_load "$PROJECT_ROOT" "$evaluation_board" || {
    bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" "$TRACE_ID"
    exit 1
  }
  output_directory=$PROJECT_ROOT/.build/buildroot-kconfig-verification-$evaluation_board
  bringup_output_directory=$PROJECT_ROOT/.build/buildroot-bringup-kconfig-verification-$evaluation_board
  mkdir -p "$output_directory" "$bringup_output_directory"
  make -C "$BHP_BUILDROOT_DIR" \
    O="$output_directory" \
    BR2_DL_DIR="$output_directory/downloads" \
    BR2_EXTERNAL="$external_tree" \
    "$BHP_BOARD_BUILDROOT_DEFCONFIG_PRODUCTION"
  test -s "$output_directory/.config"
  for expected_config_line in \
    "BR2_BHP_BOARD_ID=\"$evaluation_board\"" \
    "BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"\$(BR2_EXTERNAL_BHP_PATH)/$BHP_BOARD_SUSPEND_KCONFIG_FRAGMENT\"" \
    'BR2_PACKAGE_BHP_PRESENCE_SENSOR=y' \
    'BR2_PACKAGE_LVGL=y' \
    'BR2_PACKAGE_CHRONY=y' \
    'BR2_PACKAGE_TAILSCALE=y' \
    'BR2_TARGET_ROOTFS_EXT2_SIZE="512M"' \
    "BR2_SYSTEM_DHCP=\"$BHP_BOARD_BRINGUP_DHCP_INTERFACE\""; do
    grep -F -x "$expected_config_line" "$output_directory/.config" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.CONFIG_VALUE_MISSING \
        "$evaluation_board: $expected_config_line" "$TRACE_ID"
      exit 1
    }
  done
  if [ "$BHP_BOARD_TARGET_ARCH" = aarch64 ]; then
    for expected_config_line in \
      'BR2_aarch64=y' \
      'BR2_cortex_a53=y' \
      'BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE=y' \
      'BR2_PACKAGE_BHP_BOARD_DEVICE_TREE=y' \
      'BR2_PACKAGE_BHP_NXP_MWIFIEX=y' \
      'BR2_LINUX_KERNEL_DTB_OVERLAY_SUPPORT=y' \
      'BR2_TARGET_ARM_TRUSTED_FIRMWARE_PLATFORM="imx8mp"' \
      "BR2_TARGET_UBOOT_BOARD_DEFCONFIG=\"$BHP_BOARD_UBOOT_DEFCONFIG\""; do
      grep -F -x "$expected_config_line" "$output_directory/.config" >/dev/null || {
        bhp_fail VERIFY_BUILDROOT.CONFIG_VALUE_MISSING \
          "$evaluation_board: $expected_config_line" "$TRACE_ID"
        exit 1
      }
    done
  fi
  for forbidden_config_line in \
    'BR2_TARGET_ENABLE_ROOT_LOGIN=y' \
    'BR2_PACKAGE_DROPBEAR=y' \
    'BR2_PACKAGE_FIRMWARE_IMX=y' \
    'BR2_PACKAGE_BHP_NXP_IW612_FIRMWARE=y' \
    'BR2_PACKAGE_LIBCURL_PROXY_SUPPORT=y' \
    'BR2_PACKAGE_LIBCURL_COOKIES_SUPPORT=y' \
    'BR2_PACKAGE_LIBCURL_EXTRA_PROTOCOLS_FEATURES=y'; do
    if grep -F -x "$forbidden_config_line" "$output_directory/.config" >/dev/null; then
      bhp_fail VERIFY_BUILDROOT.FORBIDDEN_CONFIG_ENABLED \
        "$evaluation_board: $forbidden_config_line" "$TRACE_ID"
      exit 1
    fi
  done
  if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    for expected_toolchain_line in \
      'BR2_TOOLCHAIN_EXTERNAL_BOOTLIN=y' \
      'BR2_TOOLCHAIN_USES_GLIBC=y' \
      'BR2_INSTALL_LIBSTDCPP=y' \
      'BR2_TOOLCHAIN_GCC_AT_LEAST_9=y' \
      'BR2_PACKAGE_BHP_PANEL_SUPERVISOR=y' \
      'BR2_PACKAGE_BHP_PANEL_UI=y' \
      'BR2_PACKAGE_BHP_VOCABULARY_PANEL=y' \
      'BR2_PACKAGE_LIBCURL_FORCE_TLS=y' \
      'BR2_PACKAGE_JSON_FOR_MODERN_CPP=y'; do
      grep -F -x "$expected_toolchain_line" "$output_directory/.config" >/dev/null || {
        bhp_fail VERIFY_BUILDROOT.TOOLCHAIN_SELECTION_MISSING \
          "$evaluation_board: $expected_toolchain_line" "$TRACE_ID"
        exit 1
      }
    done
    make -C "$BHP_BUILDROOT_DIR" \
      O="$output_directory" \
      BR2_DL_DIR="$output_directory/downloads" \
      BR2_EXTERNAL="$external_tree" \
      linux-configure
    effective_kernel_config=
    effective_kernel_config_count=0
    for candidate_kernel_config in "$output_directory"/build/linux-*/.config; do
      test -f "$candidate_kernel_config" || continue
      effective_kernel_config=$candidate_kernel_config
      effective_kernel_config_count=$((effective_kernel_config_count + 1))
    done
    if [ "$effective_kernel_config_count" -ne 1 ]; then
      bhp_fail VERIFY_BUILDROOT.EFFECTIVE_KERNEL_CONFIG_INVALID \
        "$evaluation_board: expected one generated Linux .config; found $effective_kernel_config_count" \
        "$TRACE_ID"
      exit 1
    fi
    "$SCRIPT_DIRECTORY/check_suspend_kernel_config.sh" --board "$evaluation_board" \
      --config "$effective_kernel_config" --mode effective
    bhp_log info buildroot_kernel_config_evaluated "$TRACE_ID" \
      board_id "$evaluation_board" \
      effective_kernel_config "$effective_kernel_config" \
      suspend_prerequisites available \
      suspend_wake_evidence not_established
  else
    bhp_log warn buildroot_toolchain_and_kernel_resolution_skipped "$TRACE_ID" \
      board_id "$evaluation_board" \
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
  if [ "$BHP_BOARD_TARGET_ARCH" = aarch64 ]; then
    expected_targets="$expected_targets bhp-board-device-tree bhp-nxp-mwifiex arm-trusted-firmware uboot"
  fi
  if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    expected_targets="$expected_targets bhp-panel-supervisor bhp-panel-ui bhp-vocabulary-panel"
  fi
  for expected_target in $expected_targets; do
    printf '%s\n' "$registered_targets" | tr ' ' '\n' | grep -F -x "$expected_target" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.TARGET_NOT_REGISTERED \
        "$evaluation_board: $expected_target" "$TRACE_ID"
      exit 1
    }
  done
  make -C "$BHP_BUILDROOT_DIR" \
    O="$bringup_output_directory" \
    BR2_DL_DIR="$bringup_output_directory/downloads" \
    BR2_EXTERNAL="$external_tree" \
    "$BHP_BOARD_BUILDROOT_DEFCONFIG_BRINGUP"
  for expected_bringup_config_line in \
    "BR2_BHP_BOARD_ID=\"$evaluation_board\"" \
    'BR2_PACKAGE_DROPBEAR=y' \
    'BR2_PACKAGE_DROPBEAR_DISABLE_REVERSEDNS=y' \
    'BR2_PACKAGE_RSYNC=y' \
    'BR2_PACKAGE_DTC=y' \
    'BR2_PACKAGE_I2C_TOOLS=y' \
    'BR2_PACKAGE_STRACE=y' \
    'BR2_TARGET_ROOTFS_EXT2_SIZE="512M"'; do
    grep -F -x "$expected_bringup_config_line" \
      "$bringup_output_directory/.config" >/dev/null || {
      bhp_fail VERIFY_BUILDROOT.BRINGUP_CONFIG_VALUE_MISSING \
        "$evaluation_board: $expected_bringup_config_line" "$TRACE_ID"
      exit 1
    }
  done
  for forbidden_bringup_config_line in \
    'BR2_TARGET_ENABLE_ROOT_LOGIN=y' \
    'BR2_PACKAGE_FIRMWARE_IMX=y' \
    'BR2_PACKAGE_BHP_NXP_IW612_FIRMWARE=y'; do
    if grep -F -x "$forbidden_bringup_config_line" \
      "$bringup_output_directory/.config" >/dev/null; then
      bhp_fail VERIFY_BUILDROOT.BRINGUP_FORBIDDEN_CONFIG_ENABLED \
        "$evaluation_board: $forbidden_bringup_config_line" "$TRACE_ID"
      exit 1
    fi
  done
  bhp_log info buildroot_defconfig_evaluated "$TRACE_ID" \
    board_id "$evaluation_board" \
    output_directory "$output_directory" \
    bringup_output_directory "$bringup_output_directory" \
    buildroot_version "$actual_buildroot_version" \
    buildroot_source_id "$actual_buildroot_source_id"
}

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
  for evaluation_board in ${BHP_BUILDROOT_EVALUATE_BOARDS:-$verified_boards}; do
    evaluate_board "$evaluation_board"
  done
else
  bhp_log warn buildroot_defconfig_evaluation_skipped "$TRACE_ID" reason BHP_BUILDROOT_DIR_not_set
fi

bhp_log info buildroot_external_verification_complete "$TRACE_ID" \
  external_tree "$external_tree" \
  boards "${verified_boards# }"
