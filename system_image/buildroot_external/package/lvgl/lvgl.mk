################################################################################
#
# lvgl
#
################################################################################

LVGL_VERSION = 9.5.0
LVGL_SITE = $(call github,lvgl,lvgl,v$(LVGL_VERSION))
LVGL_LICENSE = MIT
LVGL_LICENSE_FILES = LICENCE.txt
LVGL_INSTALL_STAGING = YES
LVGL_DEPENDENCIES = libdrm

define LVGL_INSTALL_PROJECT_CONFIGURATION
	$(INSTALL) -D -m 0644 $(LVGL_PKGDIR)/lv_conf.h $(@D)/lv_conf.h
endef
LVGL_PRE_CONFIGURE_HOOKS += LVGL_INSTALL_PROJECT_CONFIGURATION

LVGL_CONF_OPTS = \
	-DLV_BUILD_CONF_PATH=$(@D)/lv_conf.h \
	-DLV_BUILD_LVGL_H_SYSTEM_INCLUDE=ON \
	-DLV_BUILD_SET_CONFIG_OPTS=OFF \
	-DCONFIG_LV_BUILD_EXAMPLES=OFF \
	-DCONFIG_LV_BUILD_DEMOS=OFF \
	-DCONFIG_LV_USE_THORVG_INTERNAL=OFF \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libdrm"

$(eval $(cmake-package))
