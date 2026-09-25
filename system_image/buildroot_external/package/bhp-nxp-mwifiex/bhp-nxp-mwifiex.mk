################################################################################
#
# bhp-nxp-mwifiex
#
################################################################################

BHP_NXP_MWIFIEX_VERSION = lf-6.12.49-2.2.0
BHP_NXP_MWIFIEX_SITE = $(call github,nxp-imx,mwifiex,$(BHP_NXP_MWIFIEX_VERSION))
BHP_NXP_MWIFIEX_LICENSE = GPL-2.0
BHP_NXP_MWIFIEX_LICENSE_FILES = LICENSE

BHP_NXP_MWIFIEX_MAKE_OPTS = KERNELDIR=$(LINUX_DIR)

define BHP_NXP_MWIFIEX_LINUX_CONFIG_FIXUPS
	$(call KCONFIG_ENABLE_OPT,CONFIG_NET)
	$(call KCONFIG_ENABLE_OPT,CONFIG_WIRELESS)
	$(call KCONFIG_ENABLE_OPT,CONFIG_CFG80211)
	$(call KCONFIG_ENABLE_OPT,CONFIG_MMC)
endef

$(eval $(kernel-module))
$(eval $(generic-package))
