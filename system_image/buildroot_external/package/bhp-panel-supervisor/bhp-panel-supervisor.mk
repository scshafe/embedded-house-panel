################################################################################
#
# bhp-panel-supervisor
#
################################################################################

BHP_PANEL_SUPERVISOR_VERSION = 0.1.0-dev
BHP_PANEL_SUPERVISOR_SITE = $(BR2_EXTERNAL_BHP_PATH)/../..
BHP_PANEL_SUPERVISOR_SITE_METHOD = local
BHP_PANEL_SUPERVISOR_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude /.build \
	--exclude /build \
	--exclude /output \
	--exclude /hardware_tests/results \
	--exclude /system_image/buildroot \
	--exclude /system_image/downloads \
	--exclude /system_image/output
BHP_PANEL_SUPERVISOR_LICENSE = Proprietary
BHP_PANEL_SUPERVISOR_SUPPORTS_IN_SOURCE_BUILD = NO
BHP_PANEL_SUPERVISOR_DEPENDENCIES = openssl bhp-panel-ui

BHP_PANEL_SUPERVISOR_CONF_OPTS = \
	-DBHP_BUILD_TESTS=OFF \
	-DBHP_BUILD_PANEL_SUPERVISOR=ON \
	-DBHP_BUILD_PANEL_UI=OFF \
	-DBHP_BUILD_VOCABULARY_PANEL=OFF \
	-DBHP_BUILD_HTTP_LEXINET_CLIENT=OFF \
	-DBHP_BUILD_LVGL_RENDERER=OFF \
	-DBHP_APPLIANCE_RELEASE_ID=$(BHP_APPLIANCE_RELEASE_ID)

# Kconfig and target discovery must remain parseable without a release build
# environment. Refuse only when this package is about to configure, before
# CMake could derive a different identity from Buildroot's local-source copy.
define BHP_PANEL_SUPERVISOR_VALIDATE_APPLICATION_RELEASE_ID
	test -n "$(strip $(BHP_APPLIANCE_RELEASE_ID))" || { \
		echo 'BHP_PANEL_SUPERVISOR.APPLICATION_RELEASE_ID_MISSING' >&2; \
		exit 1; \
	}
endef
BHP_PANEL_SUPERVISOR_PRE_CONFIGURE_HOOKS += BHP_PANEL_SUPERVISOR_VALIDATE_APPLICATION_RELEASE_ID

$(eval $(cmake-package))
