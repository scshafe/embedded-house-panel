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
	presence_sensor/experiments/custom_pir_input_driver/beagle_house_pir_input_driver.c \
	presence_sensor/experiments/custom_pir_input_driver/beagle-house-custom-pir-overlay.dts \
	presence_sensor/experiments/stock_gpio_keys_wake_baseline/beagle-house-gpio-keys-wake-baseline-overlay.dts
BHP_PRESENCE_SENSOR_DEPENDENCIES = host-dtc
BHP_PRESENCE_SENSOR_MODULE_SUBDIRS = presence_sensor

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

define BHP_PRESENCE_SENSOR_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/presence_sensor/overlays \
		KERNEL_SRC=$(LINUX_DIR) \
		CPP=$(HOSTCPP) \
		DTC=$(HOST_DIR)/bin/dtc \
		all
endef

define BHP_PRESENCE_SENSOR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 \
		$(@D)/presence_sensor/overlays/beagle-house-custom-pir.dtbo \
		$(TARGET_DIR)/usr/lib/beaglebone-house-panel/device-tree-overlays/beagle-house-custom-pir.dtbo
	$(INSTALL) -D -m 0644 \
		$(@D)/presence_sensor/overlays/beagle-house-gpio-keys-wake-baseline.dtbo \
		$(TARGET_DIR)/usr/lib/beaglebone-house-panel/device-tree-overlays/beagle-house-gpio-keys-wake-baseline.dtbo
endef

$(eval $(kernel-module))
$(eval $(generic-package))
