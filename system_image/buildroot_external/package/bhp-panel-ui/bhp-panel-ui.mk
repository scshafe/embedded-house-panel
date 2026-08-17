################################################################################
#
# bhp-panel-ui
#
################################################################################

BHP_PANEL_UI_VERSION = 0.1.0-dev
BHP_PANEL_UI_SITE = $(BR2_EXTERNAL_BHP_PATH)/../..
BHP_PANEL_UI_SITE_METHOD = local
BHP_PANEL_UI_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude /.build \
	--exclude /build \
	--exclude /output \
	--exclude /hardware_tests/results \
	--exclude /system_image/buildroot \
	--exclude /system_image/downloads \
	--exclude /system_image/output
BHP_PANEL_UI_LICENSE = Proprietary
BHP_PANEL_UI_DEPENDENCIES = openssl
BHP_PANEL_UI_SUPPORTS_IN_SOURCE_BUILD = NO

define BHP_PANEL_UI_VALIDATE_APPLICATION_RELEASE_ID
	test -n "$(strip $(BHP_APPLIANCE_RELEASE_ID))" || \
		{ printf '%s%s\n' \
		'BHP_PANEL_UI.APPLICATION_RELEASE_ID_MISSING:' \
		' canonical Buildroot wrapper must derive BHP_APPLIANCE_RELEASE_ID' >&2; \
		exit 1; }
endef
BHP_PANEL_UI_PRE_CONFIGURE_HOOKS += BHP_PANEL_UI_VALIDATE_APPLICATION_RELEASE_ID

BHP_PANEL_UI_CONF_OPTS = \
	-DBHP_BUILD_TESTS=OFF \
	-DBHP_BUILD_PANEL_SUPERVISOR=OFF \
	-DBHP_BUILD_PANEL_UI=ON \
	-DBHP_BUILD_VOCABULARY_PANEL=OFF \
	-DBHP_BUILD_HTTP_LEXINET_CLIENT=OFF \
	-DBHP_BUILD_LVGL_RENDERER=OFF \
	-DPANEL_UI_BUILD_TESTS=OFF \
	-DBHP_APPLIANCE_RELEASE_ID=$(BHP_APPLIANCE_RELEASE_ID)

# UID/GID 1102 is a deployment protocol allocation: panel_supervisord uses the
# exact UID from SO_PEERCRED, so it must change only in an atomic image cutover.
# The installed fail-dark executable needs neither device access nor state.
define BHP_PANEL_UI_USERS
	panel-ui 1102 panel-ui 1102 * - - - BeagleBone neutral panel UI
endef

$(eval $(cmake-package))
