################################################################################
#
# bhp-vocabulary-panel
#
################################################################################

BHP_VOCABULARY_PANEL_VERSION = 0.1.0-dev
BHP_VOCABULARY_PANEL_SITE = $(BR2_EXTERNAL_BHP_PATH)/../..
BHP_VOCABULARY_PANEL_SITE_METHOD = local
BHP_VOCABULARY_PANEL_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude /.build \
	--exclude /build \
	--exclude /output \
	--exclude /hardware_tests/results \
	--exclude /system_image/buildroot \
	--exclude /system_image/downloads \
	--exclude /system_image/output
BHP_VOCABULARY_PANEL_LICENSE = Proprietary
BHP_VOCABULARY_PANEL_DEPENDENCIES = json-for-modern-cpp libcurl lvgl
BHP_VOCABULARY_PANEL_SUPPORTS_IN_SOURCE_BUILD = NO

BHP_VOCABULARY_PANEL_CONF_OPTS = \
	-DBHP_BUILD_TESTS=OFF \
	-DBHP_BUILD_PANEL_SUPERVISOR=OFF \
	-DBHP_BUILD_PANEL_UI=OFF \
	-DBHP_BUILD_VOCABULARY_PANEL=ON \
	-DBHP_BUILD_HTTP_LEXINET_CLIENT=ON \
	-DBHP_BUILD_LVGL_RENDERER=ON

# The vocabulary feature runs beneath the neutral bhp-panel-ui shell, so this
# package installs only fixtures and libraries. It defines no service principal
# and grants no device group (system_image/service-principals.json).

$(eval $(cmake-package))
