#!/bin/sh
# Assembles the FRDM i.MX 8M Plus SD/eMMC image after Buildroot's image stage.
# Buildroot passes BINARIES_DIR as $1 and the U-Boot DTB path
# (BR2_ROOTFS_POST_SCRIPT_ARGS) as $2, and exports BR2_EXTERNAL_BHP_PATH,
# BR2_CONFIG, BUILD_DIR, and HOST_DIR.
# Output: imx8-boot-sd.bin, extlinux.conf, and sdcard.img in BINARIES_DIR.
# Fails closed with a BHP_IMAGE.* code when the NXP LPDDR4 training firmware
# (firmware-imx, NXP-FIRMWARE-EULA human decision) or the merged boot DTB
# (FRDM-GATE-PIR) is missing.

set -eu

binaries_directory=${1:?BINARIES_DIR argument is required}
uboot_dtb=${2:?U-Boot DTB argument is required}
: "${BR2_EXTERNAL_BHP_PATH:?BR2_EXTERNAL_BHP_PATH is required}"
: "${BR2_CONFIG:?BR2_CONFIG is required}"
board_directory=$BR2_EXTERNAL_BHP_PATH/board/beaglebone-house-panel/frdm-imx8mp
boot_dtb=imx8mp-frdm-bhp-panel.dtb

if [ ! -s "$binaries_directory/ddr_fw.bin" ]; then
	printf '%s\n' \
		'BHP_IMAGE.NXP_FIRMWARE_EULA_NOT_ACCEPTED: imx8-boot-sd.bin needs the LPDDR4 training firmware from firmware-imx; a human must record the NXP-FIRMWARE-EULA decision in an accepted ADR and set BHP_BOARD_NXP_FIRMWARE_EULA=accepted-adr-NNNN before reconfiguring (docs/operations/frdm-bringup.md)' >&2
	exit 1
fi
if [ ! -s "$binaries_directory/$boot_dtb" ]; then
	printf '%s\n' \
		"BHP_IMAGE.BOOT_DTB_MISSING: $boot_dtb was not produced by bhp-board-device-tree (FRDM-GATE-PIR)" >&2
	exit 1
fi
test -s "$uboot_dtb" || {
	printf '%s\n' "BHP_IMAGE.UBOOT_DTB_MISSING: $uboot_dtb" >&2
	exit 1
}

# Buildroot's shared NXP helper builds u-boot.itb and imx8-boot-sd.bin
# (SPL + ddr_fw.bin + ATF BL31 + U-Boot) for BR2_PACKAGE_FREESCALE_IMX_PLATFORM_IMX8MP.
board/freescale/common/imx/imx8-bootloader-prepare.sh "$binaries_directory" "$uboot_dtb"
test -s "$binaries_directory/imx8-boot-sd.bin" || {
	printf '%s\n' 'BHP_IMAGE.BOOTLOADER_MISSING: imx8-boot-sd.bin' >&2
	exit 1
}

install -m 0644 "$board_directory/extlinux.conf" "$binaries_directory/extlinux.conf"
support/scripts/genimage.sh -c "$board_directory/genimage.cfg"
test -s "$binaries_directory/sdcard.img" || {
	printf '%s\n' 'BHP_IMAGE.SDCARD_IMAGE_MISSING: sdcard.img' >&2
	exit 1
}
