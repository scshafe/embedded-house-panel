################################################################################
#
# bhp-board-device-tree
#
################################################################################

BHP_BOARD_DEVICE_TREE_VERSION = 0.1.0-dev
BHP_BOARD_DEVICE_TREE_SITE = $(BR2_EXTERNAL_BHP_PATH)/../..
BHP_BOARD_DEVICE_TREE_SITE_METHOD = local
BHP_BOARD_DEVICE_TREE_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude /.build \
	--exclude /build \
	--exclude /output \
	--exclude /hardware_tests/results \
	--exclude /system_image/buildroot \
	--exclude /system_image/downloads \
	--exclude /system_image/output
BHP_BOARD_DEVICE_TREE_LICENSE = GPL-2.0-only OR BSD-2-Clause
BHP_BOARD_DEVICE_TREE_LICENSE_FILES = $(BHP_BOARD_PROJECT_OVERLAY_TEMPLATE)
BHP_BOARD_DEVICE_TREE_DEPENDENCIES = linux host-dtc
BHP_BOARD_DEVICE_TREE_INSTALL_TARGET = NO
BHP_BOARD_DEVICE_TREE_INSTALL_IMAGES = YES

BHP_BOARD_DEVICE_TREE_WORK_DIR = $(@D)/.bhp-device-tree
BHP_BOARD_DEVICE_TREE_KERNEL_DTS_DIR = \
	$(LINUX_DIR)/arch/$(BHP_BOARD_KERNEL_ARCH)/boot/dts
BHP_BOARD_DEVICE_TREE_CPP_FLAGS = \
	-E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
	-I$(LINUX_DIR)/include \
	-I$(LINUX_DIR)/scripts/dtc/include-prefixes \
	-I$(BHP_BOARD_DEVICE_TREE_KERNEL_DTS_DIR)/$(patsubst %/,%,$(dir $(BHP_BOARD_KERNEL_BASE_DTB)))

define BHP_BOARD_DEVICE_TREE_BUILD_CMDS
	$(BHP_BOARD_REQUIRE_PROFILE)
	test -n "$(BHP_BOARD_PROJECT_OVERLAY_TEMPLATE)" || { \
		echo 'BHP_BOARD_DEVICE_TREE.TEMPLATE_UNSET: $(BHP_BOARD_ID) has no project overlay' >&2; \
		exit 1; \
	}
	mkdir -p $(BHP_BOARD_DEVICE_TREE_WORK_DIR)
	$(@D)/scripts/boards/render_device_tree_overlay.sh \
		--board $(BHP_BOARD_ID) \
		--template $(@D)/$(BHP_BOARD_PROJECT_OVERLAY_TEMPLATE) \
		--output $(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.dtso
	$(HOSTCC) $(BHP_BOARD_DEVICE_TREE_CPP_FLAGS) \
		-o $(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.pp \
		$(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.dtso
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb \
		-o $(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.dtbo \
		$(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.pp
	$(HOST_DIR)/bin/fdtoverlay \
		-i $(BHP_BOARD_DEVICE_TREE_KERNEL_DTS_DIR)/$(BHP_BOARD_KERNEL_BASE_DTB) \
		-o $(BHP_BOARD_DEVICE_TREE_WORK_DIR)/$(BHP_BOARD_BOOT_DTB) \
		$(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.dtbo
endef

define BHP_BOARD_DEVICE_TREE_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 \
		$(BHP_BOARD_DEVICE_TREE_WORK_DIR)/$(BHP_BOARD_BOOT_DTB) \
		$(BINARIES_DIR)/$(BHP_BOARD_BOOT_DTB)
	$(INSTALL) -D -m 0644 \
		$(BHP_BOARD_DEVICE_TREE_WORK_DIR)/project-overlay.dtbo \
		$(BINARIES_DIR)/bhp-$(BHP_BOARD_ID)-project-overlay.dtbo
endef

$(eval $(generic-package))
