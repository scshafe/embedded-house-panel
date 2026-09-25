################################################################################
#
# bhp-nxp-iw612-firmware
#
################################################################################

BHP_NXP_IW612_FIRMWARE_VERSION = lf-6.12.49-2.2.0
BHP_NXP_IW612_FIRMWARE_SITE = $(call github,nxp-imx,imx-firmware,$(BHP_NXP_IW612_FIRMWARE_VERSION))
BHP_NXP_IW612_FIRMWARE_LICENSE = NXP Software License Agreement
BHP_NXP_IW612_FIRMWARE_LICENSE_FILES = LICENSE.txt
BHP_NXP_IW612_FIRMWARE_REDISTRIBUTE = NO

define BHP_NXP_IW612_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0644 -D $(@D)/wifi_mod_para.conf \
		$(TARGET_DIR)/lib/firmware/nxp/wifi_mod_para.conf
	$(INSTALL) -m 0644 -D $(@D)/mfguart/helper_uart_3000000.bin \
		$(TARGET_DIR)/lib/firmware/nxp/helper_uart_3000000.bin
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/lib/firmware/nxp/ \
		$(@D)/FwImage_IW612_SD/*
endef

$(eval $(generic-package))
