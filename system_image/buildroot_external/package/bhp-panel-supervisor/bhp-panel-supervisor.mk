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

BHP_PANEL_SUPERVISOR_CONF_OPTS = \
	-DBHP_BUILD_TESTS=OFF \
	-DBHP_BUILD_PANEL_SUPERVISOR=ON \
	-DBHP_BUILD_VOCABULARY_PANEL=OFF \
	-DBHP_BUILD_HTTP_LEXINET_CLIENT=OFF \
	-DBHP_BUILD_LVGL_RENDERER=OFF

# The group is the sole read/connect principal for the supervisor-owned mode
# socket. The vocabulary package attaches its non-login user to this group.
define BHP_PANEL_SUPERVISOR_USERS
	- - vocabulary-panel -1 * - - - House panel mode socket clients
endef

$(eval $(cmake-package))
