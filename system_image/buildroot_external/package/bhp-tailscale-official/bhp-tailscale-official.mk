################################################################################
#
# bhp-tailscale-official
#
################################################################################

# ADR 0010: the official static arm64 archive, version-locked to Lexinet's pin.
# A version change updates Lexinet's pin, this file's hash and ADR 0010 together.
BHP_TAILSCALE_OFFICIAL_VERSION = 1.102.2
BHP_TAILSCALE_OFFICIAL_SOURCE = tailscale_$(BHP_TAILSCALE_OFFICIAL_VERSION)_arm64.tgz
BHP_TAILSCALE_OFFICIAL_SITE = https://pkgs.tailscale.com/stable
BHP_TAILSCALE_OFFICIAL_LICENSE = BSD-3-Clause

# Same install layout as Buildroot's source-built tailscale package, so the
# S50tailscaled init script and operator procedures do not change.
define BHP_TAILSCALE_OFFICIAL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tailscale $(TARGET_DIR)/usr/bin/tailscale
	$(INSTALL) -D -m 0755 $(@D)/tailscaled $(TARGET_DIR)/usr/bin/tailscaled
	ln -f -s ../bin/tailscaled $(TARGET_DIR)/usr/sbin/tailscaled
endef

# Kernel support that Buildroot's tailscale package enables, kept identical.
define BHP_TAILSCALE_OFFICIAL_LINUX_CONFIG_FIXUPS
	$(call KCONFIG_ENABLE_OPT,CONFIG_IPV6)
	$(call KCONFIG_ENABLE_OPT,CONFIG_IPV6_MULTIPLE_TABLES)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NETFILTER)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NETFILTER_NETLINK)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NFT_CT)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NFT_MASQ)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NFT_NAT)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_CONNTRACK)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_NAT)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_TABLES)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_TABLES_INET)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_TABLES_IPV4)
	$(call KCONFIG_ENABLE_OPT,CONFIG_NF_TABLES_IPV6)
	$(call KCONFIG_ENABLE_OPT,CONFIG_TUN)
endef

$(eval $(generic-package))
