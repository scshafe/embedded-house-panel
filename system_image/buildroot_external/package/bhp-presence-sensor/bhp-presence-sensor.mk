################################################################################
#
# bhp-presence-sensor
#
################################################################################

BHP_PRESENCE_SENSOR_VERSION = 0.1.0-dev
BHP_PRESENCE_SENSOR_SITE = $(BR2_EXTERNAL_BHP_PATH)/../..
BHP_PRESENCE_SENSOR_SITE_METHOD = local
BHP_PRESENCE_SENSOR_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude /.build \
	--exclude /build \
	--exclude /output \
	--exclude /hardware_tests/results \
	--exclude /system_image/buildroot \
	--exclude /system_image/downloads \
	--exclude /system_image/output
BHP_PRESENCE_SENSOR_LICENSE = GPL-2.0-only (module), GPL-2.0-only OR BSD-2-Clause (overlays)
BHP_PRESENCE_SENSOR_LICENSE_FILES = \
	presence_sensor/experiments/custom_pir_input_driver/beagle_house_pir_input_driver.c
BHP_PRESENCE_SENSOR_DEPENDENCIES = host-dtc
BHP_PRESENCE_SENSOR_MODULE_SUBDIRS = presence_sensor
BHP_PRESENCE_SENSOR_OVERLAY_DESTINATION = \
	$(TARGET_DIR)/usr/lib/beaglebone-house-panel/device-tree-overlays

define BHP_PRESENCE_SENSOR_LINUX_CONFIG_FIXUPS
	$(call KCONFIG_ENABLE_OPT,CONFIG_GPIOLIB)
	$(call KCONFIG_ENABLE_OPT,CONFIG_INPUT)
	$(call KCONFIG_ENABLE_OPT,CONFIG_INPUT_EVDEV)
	$(call KCONFIG_ENABLE_OPT,CONFIG_INPUT_KEYBOARD)
	$(call KCONFIG_ENABLE_OPT,CONFIG_KEYBOARD_GPIO)
	$(call KCONFIG_ENABLE_OPT,CONFIG_PM)
	$(call KCONFIG_ENABLE_OPT,CONFIG_PM_SLEEP)
	$(call KCONFIG_ENABLE_OPT,CONFIG_SUSPEND)
endef

# The kernel module is board-neutral GPIO code. The overlays are per board:
# BHP_BOARD_PRESENCE_OVERLAYS in config/boards/<BR2_BHP_BOARD_ID>.env lists the
# inert experiment overlays this package installs. FRDM overlays are rendered
# from the profile and refuse to build while FRDM-GATE-PIR is unmeasured; the
# BeagleBone Black overlays are fixed history sources.
ifeq ($(BHP_BOARD_ID),beaglebone-black)
BHP_PRESENCE_SENSOR_LICENSE_FILES += \
	presence_sensor/experiments/custom_pir_input_driver/beagle-house-custom-pir-overlay.dts \
	presence_sensor/experiments/stock_gpio_keys_wake_baseline/beagle-house-gpio-keys-wake-baseline-overlay.dts
define BHP_PRESENCE_SENSOR_BUILD_OVERLAYS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/presence_sensor/overlays \
		KERNEL_SRC=$(LINUX_DIR) \
		CPP=$(HOSTCPP) \
		DTC=$(HOST_DIR)/bin/dtc \
		all
endef
else
BHP_PRESENCE_SENSOR_LICENSE_FILES += $(BHP_BOARD_PRESENCE_CUSTOM_OVERLAY_TEMPLATE)
define BHP_PRESENCE_SENSOR_BUILD_OVERLAYS
	test -n "$(BHP_BOARD_PRESENCE_CUSTOM_OVERLAY_TEMPLATE)" || { \
		echo 'BHP_PRESENCE_SENSOR.OVERLAY_TEMPLATE_UNSET: $(BHP_BOARD_ID)' >&2; \
		exit 1; \
	}
	$(@D)/scripts/boards/render_device_tree_overlay.sh \
		--board $(BHP_BOARD_ID) \
		--template $(@D)/$(BHP_BOARD_PRESENCE_CUSTOM_OVERLAY_TEMPLATE) \
		--output $(@D)/presence_sensor/overlays/$(BHP_BOARD_PRESENCE_OVERLAYS).dtso
	$(HOSTCC) -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
		-I$(LINUX_DIR)/include \
		-I$(LINUX_DIR)/scripts/dtc/include-prefixes \
		-I$(LINUX_DIR)/arch/$(BHP_BOARD_KERNEL_ARCH)/boot/dts/$(patsubst %/,%,$(dir $(BHP_BOARD_KERNEL_BASE_DTB))) \
		-o $(@D)/presence_sensor/overlays/$(BHP_BOARD_PRESENCE_OVERLAYS).pp \
		$(@D)/presence_sensor/overlays/$(BHP_BOARD_PRESENCE_OVERLAYS).dtso
	$(HOST_DIR)/bin/dtc -@ -I dts -O dtb \
		-o $(@D)/presence_sensor/overlays/$(BHP_BOARD_PRESENCE_OVERLAYS).dtbo \
		$(@D)/presence_sensor/overlays/$(BHP_BOARD_PRESENCE_OVERLAYS).pp
endef
endif

define BHP_PRESENCE_SENSOR_BUILD_CMDS
	$(BHP_BOARD_REQUIRE_PROFILE)
	$(BHP_PRESENCE_SENSOR_BUILD_OVERLAYS)
endef

define BHP_PRESENCE_SENSOR_INSTALL_TARGET_CMDS
	$(foreach overlay,$(subst $(comma),$(space),$(BHP_BOARD_PRESENCE_OVERLAYS)), \
		$(INSTALL) -D -m 0644 \
			$(@D)/presence_sensor/overlays/$(overlay).dtbo \
			$(BHP_PRESENCE_SENSOR_OVERLAY_DESTINATION)/$(overlay).dtbo$(sep))
endef

$(eval $(kernel-module))
$(eval $(generic-package))
