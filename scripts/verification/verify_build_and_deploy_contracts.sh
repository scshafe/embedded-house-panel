#!/bin/sh
# Verifies fail-closed Buildroot, bundle, and provenance contracts without building an image.
# Inputs: none. Side effects: temporary files below TMPDIR, removed explicitly.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
# shellcheck source=../deploy/application_bundle_manifest.sh
. "$PROJECT_ROOT/scripts/deploy/application_bundle_manifest.sh"
# shellcheck source=../build/buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_build_and_deploy_contracts
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

temporary_parent=${TMPDIR:-/tmp}
case "$temporary_parent" in
	/*) ;;
	*) bhp_fail VERIFY_BUILD_DEPLOY.TMPDIR_NOT_ABSOLUTE "$temporary_parent" "$TRACE_ID"; exit 1 ;;
esac
test -d "$temporary_parent" || {
	bhp_fail VERIFY_BUILD_DEPLOY.TMPDIR_MISSING "$temporary_parent" "$TRACE_ID"
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	bhp_fail VERIFY_BUILD_DEPLOY.PYTHON_MISSING "python3 is required" "$TRACE_ID"
	exit 1
}
temporary_root=$(mktemp -d "$temporary_parent/bhp-build-deploy-contracts.XXXXXX")
panel_supervisor_fixture_short_runtime_link=

cleanup()
{
	if [ -n "$panel_supervisor_fixture_short_runtime_link" ]; then
		rm -f -- "$panel_supervisor_fixture_short_runtime_link"
	fi
	find "$temporary_root" -type l -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -type s -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -type f -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -depth -type d -exec rmdir {} \; 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

bundle_directory=$temporary_root/bundle
mkdir -p "$bundle_directory/usr/bin" "$bundle_directory/usr/lib"
printf '%s\n' supervisor >"$bundle_directory/usr/bin/panel_supervisord"
printf '%s\n' renderer >"$bundle_directory/usr/lib/libpanel-renderer.a"
bhp_bundle_create_manifest "$bundle_directory" || {
	bhp_fail VERIFY_BUILD_DEPLOY.MANIFEST_CREATE_FAILED \
		"$BHP_BUNDLE_ERROR_CODE:$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
	exit 1
}
bhp_bundle_verify_exact "$bundle_directory" || {
	bhp_fail VERIFY_BUILD_DEPLOY.MANIFEST_VERIFY_FAILED \
		"$BHP_BUNDLE_ERROR_CODE:$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
	exit 1
}

printf '%s\n' unexpected >"$bundle_directory/usr/bin/unmanifested"
if bhp_bundle_verify_exact "$bundle_directory"; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNMANIFESTED_FILE_ACCEPTED \
		"Exact bundle verification accepted an extra file" "$TRACE_ID"
	exit 1
fi
rm -- "$bundle_directory/usr/bin/unmanifested"
bhp_bundle_verify_exact "$bundle_directory" || exit 1

BHP_BOARD_SSH_TARGET=tester@board.invalid \
BHP_BOARD_UPLOAD_DIR=/home/tester/beaglebone-house-panel-staging \
	"$PROJECT_ROOT/scripts/deploy/upload_application_bundle.sh" \
	--dry-run --bundle "$bundle_directory" >/dev/null

wrong_source=$temporary_root/buildroot-wrong
wrong_output=$temporary_root/output-wrong
mkdir -p "$wrong_source"
# The generated fixture Makefile must retain make's literal $(O) expression.
# shellcheck disable=SC2016
printf '%s\n' \
	'export BR2_VERSION := 2026.05.0' \
	'.PHONY: print-version beaglebone_house_panel_defconfig' \
	'print-version:' \
	'	@printf "%s\\n" 2026.05.0' \
	'beaglebone_house_panel_defconfig:' \
	'	@mkdir -p "$(O)"' \
	'	@printf "%s\\n" configured >"$(O)/.config"' \
	>"$wrong_source/Makefile"
if BHP_BUILDROOT_DIR=$wrong_source BHP_BUILDROOT_OUTPUT_DIR=$wrong_output \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.WRONG_BUILDROOT_ACCEPTED \
		"configure_buildroot.sh accepted an unpinned source" "$TRACE_ID"
	exit 1
fi
if [ -e "$wrong_output" ] || [ -L "$wrong_output" ]; then
	bhp_fail VERIFY_BUILD_DEPLOY.WRONG_BUILDROOT_WROTE_OUTPUT "$wrong_output" "$TRACE_ID"
	exit 1
fi

provenance_target=$temporary_root/provenance-target
mkdir -p "$provenance_target/etc" \
	"$provenance_target/usr/libexec/beaglebone-house-panel"
printf '%s\n' \
	'input/event[0-9]+ root:root 600' \
	'input/mice root:root 600' \
	'input/mouse[0-9] root:root 600' \
	'input/ts[0-9] root:root 600' \
	>"$provenance_target/etc/mdev.conf"
install -m 0755 \
	"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/rootfs-overlay/usr/libexec/beaglebone-house-panel/classify-touch-input" \
	"$provenance_target/usr/libexec/beaglebone-house-panel/classify-touch-input"
provenance_br2_config=$temporary_root/provenance-br2.config
printf '%s\n' 'BR2_BHP_BOARD_ID="frdm-imx8mp"' >"$provenance_br2_config"
BR2_CONFIG=$provenance_br2_config
export BR2_CONFIG
unknown_board_br2_config=$temporary_root/unknown-board-br2.config
printf '%s\n' 'BR2_BHP_BOARD_ID="no-such-board"' >"$unknown_board_br2_config"
if BR2_CONFIG=$unknown_board_br2_config \
	BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
	BR2_VERSION=2026.05.1 BR2_VERSION_FULL=2026.05.1 \
	BHP_BUILDROOT_SOURCE_ID=$BHP_PINNED_BUILDROOT_SOURCE_ID \
	BHP_BUILDROOT_ARCHIVE_SHA256=$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256 \
	"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/post-build.sh" \
	"$provenance_target" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNKNOWN_BOARD_PROFILE_ACCEPTED \
		"post-build.sh accepted a board id without a profile" "$TRACE_ID"
	exit 1
fi
if BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
	BR2_VERSION=2026.05.0 BR2_VERSION_FULL=2026.05.0 \
	BHP_BUILDROOT_SOURCE_ID=$BHP_PINNED_BUILDROOT_SOURCE_ID \
	BHP_BUILDROOT_ARCHIVE_SHA256=$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256 \
	"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/post-build.sh" \
	"$provenance_target" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.WRONG_STAMP_VERSION_ACCEPTED \
		"post-build.sh accepted an unpinned Buildroot version" "$TRACE_ID"
	exit 1
fi
if BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
	BR2_VERSION=2026.05.1 BR2_VERSION_FULL=2026.05.1 \
	BHP_BUILDROOT_SOURCE_ID=$BHP_PINNED_BUILDROOT_SOURCE_ID \
	BHP_BUILDROOT_ARCHIVE_SHA256=unverified \
	"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/post-build.sh" \
	"$provenance_target" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.INCONSISTENT_BUILDROOT_IDENTITY_ACCEPTED \
		"Image provenance associated canonical source with an unverified archive" \
		"$TRACE_ID"
	exit 1
fi
BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
BR2_VERSION=2026.05.1 BR2_VERSION_FULL=2026.05.1 \
BHP_BUILDROOT_SOURCE_ID=$BHP_PINNED_BUILDROOT_SOURCE_ID \
BHP_BUILDROOT_ARCHIVE_SHA256=$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256 \
	"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/post-build.sh" \
	"$provenance_target"
image_version=$provenance_target/etc/beaglebone-house-panel/image-version
grep -F -x 'buildroot_version=2026.05.1' "$image_version" >/dev/null
grep -F -x "buildroot_source_id=$BHP_PINNED_BUILDROOT_SOURCE_ID" \
	"$image_version" >/dev/null
grep -F -x "buildroot_archive_sha256=$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256" \
	"$image_version" >/dev/null
grep -E -x 'project_content_state=(clean|dirty|uncommitted|source-archive)' \
	"$image_version" >/dev/null
grep -E -x 'project_content_id=[0-9a-f]{64}' "$image_version" >/dev/null
grep -F -x 'board_id=frdm-imx8mp' "$image_version" >/dev/null
cmp -s "$PROJECT_ROOT/config/boards/frdm-imx8mp.env" \
	"$provenance_target/usr/share/beaglebone-house-panel/board-profile.env" || {
	bhp_fail VERIFY_BUILD_DEPLOY.IMAGE_BOARD_PROFILE_INVALID \
		"post-build.sh did not install the selected board profile verbatim" "$TRACE_ID"
	exit 1
}
rendered_environment_directory=$provenance_target/etc/beaglebone-house-panel
for rendered_environment_contract in \
	'panel-supervisor.env.example:BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH=REPLACE_ME_UNMEASURED_FRDM-GATE-BACKLIGHT/brightness' \
	'panel-supervisor.env.example:BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME=bhp-frdm-pir-gpio-keys' \
	'panel-supervisor.env.example:BHP_PANEL_SUPERVISOR_SUSPEND_MODE=disabled' \
	'panel-supervisor.env.example:# BHP_PANEL_SUPERVISOR_MEM_SLEEP_STATE=REPLACE_ME_UNMEASURED_FRDM-GATE-SUSPEND' \
	'presence-sensor.env.example:BHP_PRESENCE_SENSOR_ENABLED=0' \
	'panel-ui.env.example:# BHP_PANEL_UI_DISPLAY_BACKEND=drm' \
	'panel-ui.env.example:BHP_PANEL_UI_ENABLED=0'; do
	rendered_environment_file=$rendered_environment_directory/${rendered_environment_contract%%:*}
	grep -F -x -- "${rendered_environment_contract#*:}" "$rendered_environment_file" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.SERVICE_ENVIRONMENT_INVALID \
			"$rendered_environment_contract" "$TRACE_ID"
		exit 1
	}
done
# The host template, the rendered board example, and the daemon's accepted
# variables must name exactly the same keys (commented keys included).
python3 - "$PROJECT_ROOT/config/panel-supervisor.env.example" \
	"$rendered_environment_directory/panel-supervisor.env.example" \
	"$PROJECT_ROOT/panel_supervisor/src/panel_supervisord_config.cpp" \
	"$PROJECT_ROOT/panel_supervisor/include/panel_supervisor/touch_runtime_configuration.hpp" <<'PY_SERVICE_KEYS' || {
import re
import sys

def keys(path):
    return set(re.findall(r"^#? ?(BHP_PANEL_SUPERVISOR_[A-Z_]+)=", open(path).read(), re.MULTILINE))

daemon = set(re.findall(r'"(BHP_PANEL_SUPERVISOR_[A-Z_]+)"', open(sys.argv[3]).read()))
daemon.update(re.findall(r'"(BHP_PANEL_SUPERVISOR_[A-Z_]+)"', open(sys.argv[4]).read()))
daemon.add("BHP_PANEL_SUPERVISOR_ENABLED")
host, rendered = keys(sys.argv[1]), keys(sys.argv[2])
if not (host == rendered == daemon):
    print(sorted(host ^ daemon), sorted(rendered ^ daemon))
    sys.exit(1)
PY_SERVICE_KEYS
	bhp_fail VERIFY_BUILD_DEPLOY.SERVICE_ENVIRONMENT_KEYS_DIVERGED \
		"config/panel-supervisor.env.example, the rendered example, and panel_supervisord_config.cpp disagree" \
		"$TRACE_ID"
	exit 1
}
if grep -E '@BHP_BOARD_[A-Z0-9_]+@' "$rendered_environment_directory"/*.env.example >/dev/null; then
	bhp_fail VERIFY_BUILD_DEPLOY.SERVICE_ENVIRONMENT_TOKEN_LEFT \
		"post-build.sh left a board token in a service environment example" "$TRACE_ID"
	exit 1
fi
unset BR2_CONFIG
grep -F -x production \
	"$provenance_target/etc/beaglebone-house-panel/image-profile" >/dev/null
grep -F -x \
	'input/event[0-9]+	root:root 600 @/usr/libexec/beaglebone-house-panel/classify-touch-input' \
	"$provenance_target/etc/mdev.conf" >/dev/null
for root_only_input_rule in \
	'input/mice	root:root 600' \
	'input/mouse[0-9]	root:root 600' \
	'input/ts[0-9]	root:root 600'; do
	grep -F -x "$root_only_input_rule" "$provenance_target/etc/mdev.conf" >/dev/null
done
if grep -F 'root:input' "$provenance_target/etc/mdev.conf" >/dev/null; then
	bhp_fail VERIFY_BUILD_DEPLOY.BROAD_INPUT_ACCESS_PRESENT \
		"mdev policy must not grant generic input-group access" "$TRACE_ID"
	exit 1
fi

classifier=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/rootfs-overlay/usr/libexec/beaglebone-house-panel/classify-touch-input
classifier_fixture=$temporary_root/classifier
classifier_configuration=$classifier_fixture/configuration
classifier_sys=$classifier_fixture/sys/class/input
classifier_dev=$classifier_fixture/dev
mkdir -p "$classifier_configuration" "$classifier_dev/input" \
	"$classifier_sys/event0/device" "$classifier_sys/event1/device" \
	"$classifier_sys/event2/device" "$classifier_sys/event4/device"
printf '%s\n' '4D Cape Touch Controller' >"$classifier_configuration/touch-input.name"
printf '%s\n' 'i2c-1/1-0048/input0' >"$classifier_configuration/touch-input.phys"
printf '%s\n' '4D Cape Touch Controller' >"$classifier_sys/event0/device/name"
printf '%s\n' 'i2c-1/1-0048/input0' >"$classifier_sys/event0/device/phys"
printf '%s\n' 'Beagle House PIR custom experiment' \
	>"$classifier_sys/event1/device/name"
printf '%s\n' 'gpio-keys/input0' >"$classifier_sys/event1/device/phys"
printf '%s\n' '4D Cape Touch Controller' >"$classifier_sys/event2/device/name"
printf '%s\n' 'i2c-1/1-0049/input0' >"$classifier_sys/event2/device/phys"
printf '%s\n' 'Beagle House gpio-keys wake baseline' \
	>"$classifier_sys/event4/device/name"
printf '%s\n' 'gpio-keys/input1' >"$classifier_sys/event4/device/phys"
: >"$classifier_dev/input/event0"
: >"$classifier_dev/input/event1"
: >"$classifier_dev/input/event2"
: >"$classifier_dev/input/event4"
chmod 0600 "$classifier_dev/input/event0" "$classifier_dev/input/event1" \
	"$classifier_dev/input/event2" "$classifier_dev/input/event4"
test_group=$(id -gn)

# BusyBox mdev sets MDEV to node_name, which retains DEVNAME's input/eventN
# shape. The helper deliberately rejects a basename-only approximation.
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=event0 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event0"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event0 "$classifier"
python3 -c 'import os,stat,sys; value=os.stat(sys.argv[1]); assert stat.S_IMODE(value.st_mode) == 0o600; assert value.st_gid == os.getgid()' \
	"$classifier_dev/input/event0"

# A non-touch add still revokes every legacy grant; even exact touch is root-only.
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event1 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600; assert stat.S_IMODE(os.stat(sys.argv[2]).st_mode) == 0o600' \
	"$classifier_dev/input/event0" "$classifier_dev/input/event1"

# A later device with the same name+phys pair revokes the earlier grant and is
# denied itself; uniqueness is evaluated across every current event device.
mkdir -p "$classifier_sys/event3/device"
printf '%s\n' '4D Cape Touch Controller' >"$classifier_sys/event3/device/name"
printf '%s\n' 'i2c-1/1-0048/input0' >"$classifier_sys/event3/device/phys"
: >"$classifier_dev/input/event3"
chmod 0600 "$classifier_dev/input/event3"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event3 "$classifier"
python3 -c 'import os,stat,sys; assert all(stat.S_IMODE(os.stat(path).st_mode) == 0o600 for path in sys.argv[1:])' \
	"$classifier_dev/input/event0" "$classifier_dev/input/event3"

# Even an exact but mistaken PIR configuration cannot grant the UI access.
printf '%s\n' 'Beagle House PIR custom experiment' \
	>"$classifier_configuration/touch-input.name"
printf '%s\n' 'gpio-keys/input0' >"$classifier_configuration/touch-input.phys"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event1 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event1"
printf '%s\n' 'Beagle House gpio-keys wake baseline' \
	>"$classifier_configuration/touch-input.name"
printf '%s\n' 'gpio-keys/input1' >"$classifier_configuration/touch-input.phys"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event4 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event4"

# The FRDM PIR identity is denied the same way.
mkdir -p "$classifier_sys/event5/device"
printf '%s\n' 'bhp-frdm-pir-gpio-keys' >"$classifier_sys/event5/device/name"
printf '%s\n' 'gpio-keys/input0' >"$classifier_sys/event5/device/phys"
: >"$classifier_dev/input/event5"
chmod 0600 "$classifier_dev/input/event5"
printf '%s\n' 'bhp-frdm-pir-gpio-keys' >"$classifier_configuration/touch-input.name"
printf '%s\n' 'gpio-keys/input0' >"$classifier_configuration/touch-input.phys"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event5 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event5"

# An ambiguous physical identity and a CR-bearing configuration both fail shut.
printf '%s\n' '4D Cape Touch Controller' >"$classifier_configuration/touch-input.name"
printf '%s\n' 'i2c-1/1-0048/input0' >"$classifier_configuration/touch-input.phys"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event2 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event2"
printf '4D Cape Touch Controller\r\n' >"$classifier_configuration/touch-input.name"
BHP_MDEV_TEST_MODE=1 \
BHP_MDEV_TEST_CONFIGURATION_ROOT=$classifier_configuration \
BHP_MDEV_TEST_SYS_CLASS_INPUT=$classifier_sys \
BHP_MDEV_TEST_DEVICE_ROOT=$classifier_dev \
BHP_MDEV_TEST_TOUCH_GROUP=$test_group \
MDEV=input/event2 "$classifier"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$classifier_dev/input/event2"

content_fixture=$temporary_root/content-fixture
mkdir -p "$content_fixture"
git -C "$content_fixture" init -q
printf '%s\n' 'ignored.cache' >"$content_fixture/.gitignore"
printf '%s\n' alpha >"$content_fixture/source.txt"
content_id_one=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$content_fixture"
)
printf '%s\n' not-source >"$content_fixture/ignored.cache"
content_id_ignored=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$content_fixture"
)
[ "$content_id_one" = "$content_id_ignored" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.IGNORED_OUTPUT_CHANGED_SOURCE_ID \
		"Ignored build output entered the project content identity" "$TRACE_ID"
	exit 1
}
printf '%s\n' beta >"$content_fixture/source.txt"
content_id_two=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$content_fixture"
)
[ "$content_id_one" != "$content_id_two" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.SOURCE_CHANGE_NOT_IDENTIFIED \
		"Visible source mutation did not change the project content identity" "$TRACE_ID"
	exit 1
}
ln -s source.txt "$content_fixture/linked-source"
if "$PROJECT_ROOT/scripts/build/project_content_id.sh" \
	"$content_fixture" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.SOURCE_SYMLINK_ACCEPTED \
		"Release source identity accepted a symlink" "$TRACE_ID"
	exit 1
fi
rm -- "$content_fixture/linked-source"

# The application-bundle handshake identity is exact for a clean commit and
# gains the full Git-visible source ID for a dirty build; a generic "dirty"
# marker would let two divergent binaries pass the release check.
printf '%s\n' '0.1.0' >"$content_fixture/VERSION"
git -C "$content_fixture" add .gitignore VERSION source.txt
git -C "$content_fixture" \
	-c user.name=fixture -c user.email=fixture.invalid commit -q -m fixture
application_release_clean=$(
	"$PROJECT_ROOT/scripts/build/application_release_id.sh" "$content_fixture"
)
printf '%s\n' "$application_release_clean" | \
	grep -E -x '0\.1\.0\+g[0-9a-f]{40}' >/dev/null || {
	bhp_fail VERIFY_BUILD_DEPLOY.APPLICATION_RELEASE_CLEAN_ID_INVALID \
		"Clean application release identity does not bind the full commit" "$TRACE_ID"
	exit 1
}
printf '%s\n' gamma >"$content_fixture/source.txt"
dirty_content_id=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$content_fixture"
)
application_release_dirty=$(
	"$PROJECT_ROOT/scripts/build/application_release_id.sh" "$content_fixture"
)
[ "$application_release_dirty" = \
	"$application_release_clean-dirty.$dirty_content_id" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.APPLICATION_RELEASE_DIRTY_ID_INVALID \
		"Dirty application release identity did not bind exact source content" "$TRACE_ID"
	exit 1
}
[ "${#application_release_dirty}" -le 128 ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.APPLICATION_RELEASE_ID_TOO_LONG \
		"Application release identity exceeded the protocol bound" "$TRACE_ID"
	exit 1
}

buildroot_identity_fixture=$temporary_root/buildroot-identity-fixture
mkdir -p "$buildroot_identity_fixture/empty-directory" \
	"$buildroot_identity_fixture/package"
printf '%s\n' source >"$buildroot_identity_fixture/Makefile"
printf '%s\n' payload >"$buildroot_identity_fixture/package/data"
ln -s package/data "$buildroot_identity_fixture/data-link"
buildroot_identity_one=$(
	"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh" \
		"$buildroot_identity_fixture"
)
printf '%s\n' changed >"$buildroot_identity_fixture/package/data"
buildroot_identity_content_changed=$(
	"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh" \
		"$buildroot_identity_fixture"
)
[ "$buildroot_identity_one" != "$buildroot_identity_content_changed" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.BUILDROOT_CONTENT_CHANGE_NOT_IDENTIFIED \
		"Buildroot source content mutation did not change its identity" "$TRACE_ID"
	exit 1
}
printf '%s\n' payload >"$buildroot_identity_fixture/package/data"
chmod u+x "$buildroot_identity_fixture/package/data"
buildroot_identity_mode_changed=$(
	"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh" \
		"$buildroot_identity_fixture"
)
[ "$buildroot_identity_one" != "$buildroot_identity_mode_changed" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.BUILDROOT_MODE_CHANGE_NOT_IDENTIFIED \
		"Buildroot executable-bit mutation did not change its identity" "$TRACE_ID"
	exit 1
}
chmod u-x "$buildroot_identity_fixture/package/data"
rm -- "$buildroot_identity_fixture/data-link"
ln -s Makefile "$buildroot_identity_fixture/data-link"
buildroot_identity_link_changed=$(
	"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh" \
		"$buildroot_identity_fixture"
)
[ "$buildroot_identity_one" != "$buildroot_identity_link_changed" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.BUILDROOT_LINK_CHANGE_NOT_IDENTIFIED \
		"Buildroot symlink-target mutation did not change its identity" "$TRACE_ID"
	exit 1
}
rm -- "$buildroot_identity_fixture/data-link"
mkfifo "$buildroot_identity_fixture/special-input"
if "$PROJECT_ROOT/scripts/build/buildroot_source_id.sh" \
	"$buildroot_identity_fixture" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.BUILDROOT_SPECIAL_FILE_ACCEPTED \
		"Buildroot source identity accepted a special file" "$TRACE_ID"
	exit 1
fi
rm -- "$buildroot_identity_fixture/special-input"

untrusted_source_destination=$temporary_root/untrusted-buildroot-source
mkdir -p "$untrusted_source_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION"
printf '%s\n' preserve-me \
	>"$untrusted_source_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION/caller-file"
if "$PROJECT_ROOT/scripts/build/download_verified_buildroot.sh" \
	--destination "$untrusted_source_destination" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNTRUSTED_EXISTING_BUILDROOT_ACCEPTED \
		"Downloader trusted an arbitrary pre-existing version-named tree" "$TRACE_ID"
	exit 1
fi
grep -F -x preserve-me \
	"$untrusted_source_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION/caller-file" \
	>/dev/null
test ! -e "$untrusted_source_destination/$BHP_PINNED_BUILDROOT_ARCHIVE"
test ! -e \
	"$untrusted_source_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION.bhp-verified-source"

untrusted_archive_destination=$temporary_root/untrusted-buildroot-archive
mkdir -p "$untrusted_archive_destination"
printf '%s\n' preserve-archive \
	>"$untrusted_archive_destination/$BHP_PINNED_BUILDROOT_ARCHIVE"
untrusted_archive_before=$(bhp_sha256_file \
	"$untrusted_archive_destination/$BHP_PINNED_BUILDROOT_ARCHIVE")
if "$PROJECT_ROOT/scripts/build/download_verified_buildroot.sh" \
	--destination "$untrusted_archive_destination" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNTRUSTED_EXISTING_ARCHIVE_ACCEPTED \
		"Downloader trusted an arbitrary pre-existing archive" "$TRACE_ID"
	exit 1
fi
untrusted_archive_after=$(bhp_sha256_file \
	"$untrusted_archive_destination/$BHP_PINNED_BUILDROOT_ARCHIVE")
[ "$untrusted_archive_before" = "$untrusted_archive_after" ] || {
	bhp_fail VERIFY_BUILD_DEPLOY.UNTRUSTED_ARCHIVE_REPLACED \
		"Downloader modified a caller-owned conflicting archive" "$TRACE_ID"
	exit 1
}

command -v ssh-keygen >/dev/null 2>&1 || {
	bhp_fail VERIFY_BUILD_DEPLOY.SSH_KEYGEN_MISSING \
		"ssh-keygen is required for bring-up key validation" "$TRACE_ID"
	exit 1
}
bringup_key=$temporary_root/operator_ed25519
ssh-keygen -q -t ed25519 -N '' -f "$bringup_key"
bhp_bringup_authorized_key_is_valid "$bringup_key.pub" || {
	bhp_fail VERIFY_BUILD_DEPLOY.GENERATED_BRINGUP_KEY_REJECTED \
		"Generated Ed25519 public key failed validation" "$TRACE_ID"
	exit 1
}
printf '%s\n' not-a-key >"$temporary_root/not-a-key.pub"
if bhp_bringup_authorized_key_is_valid "$temporary_root/not-a-key.pub"; then
	bhp_fail VERIFY_BUILD_DEPLOY.INVALID_BRINGUP_KEY_ACCEPTED \
		"Malformed bring-up authorization was accepted" "$TRACE_ID"
	exit 1
fi

pinned_source=$temporary_root/buildroot-pinned
mkdir -p "$pinned_source"
# shellcheck disable=SC2016
{
	printf '%s\n' \
		'export BR2_VERSION := 2026.05.1' \
		'.PHONY: print-version beaglebone_house_panel_defconfig beaglebone_house_panel_bringup_defconfig frdm_imx8mp_house_panel_defconfig frdm_imx8mp_house_panel_bringup_defconfig' \
		'print-version:'
	printf '\t%s\n' '@printf "%s\\n" 2026.05.1'
	printf '%s\n' \
		'beaglebone_house_panel_defconfig beaglebone_house_panel_bringup_defconfig frdm_imx8mp_house_panel_defconfig frdm_imx8mp_house_panel_bringup_defconfig:'
	printf '\t%s\n' '@mkdir -p "$(O)"'
	printf '\t%s\n' '@printf "%s\\n" configured >"$(O)/.config"'
} >"$pinned_source/Makefile"
overlap_child=$pinned_source/output-must-not-exist
if BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$overlap_child \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.OVERLAPPING_OUTPUT_ACCEPTED \
		"configure_buildroot.sh accepted output inside its source" "$TRACE_ID"
	exit 1
fi
if [ -e "$overlap_child" ] || [ -L "$overlap_child" ]; then
	bhp_fail VERIFY_BUILD_DEPLOY.OVERLAP_CHECK_MUTATED_SOURCE "$overlap_child" "$TRACE_ID"
	exit 1
fi
if bhp_buildroot_output_is_separate "$pinned_source" "$temporary_root" \
	"$PROJECT_ROOT"; then
	bhp_fail VERIFY_BUILD_DEPLOY.OUTPUT_CONTAINING_SOURCE_ACCEPTED \
		"Path guard accepted an output containing Buildroot source" "$TRACE_ID"
	exit 1
fi
if bhp_buildroot_output_is_separate "$pinned_source" "$pinned_source" \
	"$PROJECT_ROOT"; then
	bhp_fail VERIFY_BUILD_DEPLOY.OUTPUT_EQUAL_SOURCE_ACCEPTED \
		"Path guard accepted Buildroot source as output" "$TRACE_ID"
	exit 1
fi
if bhp_buildroot_output_is_separate "$pinned_source" \
	"$PROJECT_ROOT/.build" "$PROJECT_ROOT"; then
	bhp_fail VERIFY_BUILD_DEPLOY.PROJECT_OUTPUT_OVERLAP_ACCEPTED \
		"Path guard accepted project-contained output" "$TRACE_ID"
	exit 1
fi
if bhp_buildroot_output_is_separate "$pinned_source" "$PROJECT_ROOT" \
	"$PROJECT_ROOT"; then
	bhp_fail VERIFY_BUILD_DEPLOY.OUTPUT_EQUAL_PROJECT_ACCEPTED \
		"Path guard accepted project root as output" "$TRACE_ID"
	exit 1
fi
project_parent=$(dirname "$PROJECT_ROOT")
if bhp_buildroot_output_is_separate "$pinned_source" "$project_parent" \
	"$PROJECT_ROOT"; then
	bhp_fail VERIFY_BUILD_DEPLOY.OUTPUT_CONTAINING_PROJECT_ACCEPTED \
		"Path guard accepted output containing project root" "$TRACE_ID"
	exit 1
fi
valid_output=$temporary_root/valid-output
BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$valid_output \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null
grep -F -x production "$valid_output/.bhp-build-profile" >/dev/null
active_board_id=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" --value BHP_BOARD_ID)
grep -F -x "$active_board_id" "$valid_output/.bhp-board-id" >/dev/null || {
	bhp_fail VERIFY_BUILD_DEPLOY.BOARD_STAMP_INVALID \
		"configure_buildroot.sh did not stamp the active board $active_board_id" "$TRACE_ID"
	exit 1
}
if BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$valid_output \
	BHP_BOARD=beaglebone-black \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.BOARD_SWITCH_ACCEPTED \
		"An output configured for one board was reused for another" "$TRACE_ID"
	exit 1
fi
legacy_output=$temporary_root/legacy-bbb-output
mkdir -p "$legacy_output"
printf '%s\n' production >"$legacy_output/.bhp-build-profile"
if BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$legacy_output \
	BHP_BOARD=frdm-imx8mp \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.LEGACY_BBB_OUTPUT_REUSED \
		"A pre-profile BeagleBone output was reused for the FRDM board" "$TRACE_ID"
	exit 1
fi
if BHP_BUILDROOT_DIR=$pinned_source \
	BHP_BUILDROOT_OUTPUT_DIR=$temporary_root/unknown-board-output \
	BHP_BOARD=no-such-board \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNKNOWN_BOARD_ACCEPTED \
		"configure_buildroot.sh accepted a board without a profile" "$TRACE_ID"
	exit 1
fi
if BHP_BUILDROOT_DIR=$pinned_source \
	BHP_BUILDROOT_OUTPUT_DIR=$temporary_root/production-key-forbidden \
	BHP_BRINGUP_AUTHORIZED_KEYS_FILE=$bringup_key.pub \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.PRODUCTION_KEY_INPUT_ACCEPTED \
		"Production configuration consumed bring-up authorization" "$TRACE_ID"
	exit 1
fi
bringup_output=$temporary_root/bringup-output
BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$bringup_output \
BHP_BUILDROOT_PROFILE=bringup \
BHP_BRINGUP_AUTHORIZED_KEYS_FILE=$bringup_key.pub \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null
grep -F -x bringup "$bringup_output/.bhp-build-profile" >/dev/null
if BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$bringup_output \
	BHP_BUILDROOT_PROFILE=production \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.BRINGUP_OUTPUT_REUSED_FOR_PRODUCTION \
		"Profile switching could retain a development SSH server" "$TRACE_ID"
	exit 1
fi
grep -F -x bringup "$bringup_output/.bhp-build-profile" >/dev/null
unknown_output=$temporary_root/unknown-output
mkdir -p "$unknown_output"
printf '%s\n' unknown >"$unknown_output/preexisting-artifact"
if BHP_BUILDROOT_DIR=$pinned_source BHP_BUILDROOT_OUTPUT_DIR=$unknown_output \
	"$PROJECT_ROOT/scripts/build/configure_buildroot.sh" >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.UNKNOWN_OUTPUT_ACCEPTED \
		"Unstamped nonempty output was accepted" "$TRACE_ID"
	exit 1
fi
grep -F -x unknown "$unknown_output/preexisting-artifact" >/dev/null

mkdir -p "$provenance_target/etc/default"
printf '%s\n' 'DROPBEAR_ARGS="-s"' \
	>"$provenance_target/etc/default/dropbear"
bringup_post=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/bringup/post-build.sh
BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
BHP_BRINGUP_AUTHORIZED_KEYS_FILE=$bringup_key.pub \
	"$bringup_post" "$provenance_target"
cmp -s "$bringup_key.pub" "$provenance_target/root/.ssh/authorized_keys"
grep -F -x bringup-root-public-key-only \
	"$provenance_target/etc/beaglebone-house-panel/image-profile" >/dev/null
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o600' \
	"$provenance_target/root/.ssh/authorized_keys"
# Same-key reruns converge; a deliberate key change atomically replaces only
# the bring-up authorization and its public fingerprint.
BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
BHP_BRINGUP_AUTHORIZED_KEYS_FILE=$bringup_key.pub \
	"$bringup_post" "$provenance_target"
second_bringup_key=$temporary_root/operator_ed25519_second
ssh-keygen -q -t ed25519 -N '' -f "$second_bringup_key"
BR2_EXTERNAL_BHP_PATH=$PROJECT_ROOT/system_image/buildroot_external \
BHP_BRINGUP_AUTHORIZED_KEYS_FILE=$second_bringup_key.pub \
	"$bringup_post" "$provenance_target"
cmp -s "$second_bringup_key.pub" \
	"$provenance_target/root/.ssh/authorized_keys"

grep -F 'mktemp -d' \
	"$PROJECT_ROOT/scripts/verification/verify_release_candidate_image.sh" >/dev/null
grep -F 'INCREMENTAL_OUTPUT_FORBIDDEN' \
	"$PROJECT_ROOT/scripts/verification/verify_release_candidate_image.sh" >/dev/null
grep -F 'project_content_id.sh' \
	"$PROJECT_ROOT/scripts/verification/verify_release_candidate_image.sh" >/dev/null
for release_buildroot_identity_contract in \
	'BHP_BUILDROOT_ARCHIVE' \
	'PREEXISTING_SOURCE_FORBIDDEN' \
	'BHP_PINNED_BUILDROOT_ARCHIVE_SHA256' \
	'BHP_PINNED_BUILDROOT_SOURCE_ID' \
	'buildroot_source_id.sh' \
	'tar -xJf' \
	'BUILDROOT_SOURCE_CHANGED_DURING_BUILD' \
	'buildroot_archive_sha256=%s'; do
	grep -F "$release_buildroot_identity_contract" \
		"$PROJECT_ROOT/scripts/verification/verify_release_candidate_image.sh" \
		>/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.RELEASE_BUILDROOT_IDENTITY_CONTRACT_MISSING \
			"$release_buildroot_identity_contract" "$TRACE_ID"
		exit 1
	}
done
grep -F ' - video BeagleBone vocabulary panel' \
	"$PROJECT_ROOT/system_image/buildroot_external/package/bhp-vocabulary-panel/bhp-vocabulary-panel.mk" >/dev/null
if grep -E 'video,(input|bhp-touch)' \
	"$PROJECT_ROOT/system_image/buildroot_external/package/bhp-vocabulary-panel/bhp-vocabulary-panel.mk" >/dev/null; then
	bhp_fail VERIFY_BUILD_DEPLOY.VOCABULARY_GENERIC_INPUT_GROUP_PRESENT \
		"Vocabulary user must not join the generic input group" "$TRACE_ID"
	exit 1
fi

panel_ui_package=$PROJECT_ROOT/system_image/buildroot_external/package/bhp-panel-ui/bhp-panel-ui.mk
grep -F "panel-ui 1102 panel-ui 1102 * - - - BeagleBone neutral panel UI" \
	"$panel_ui_package" >/dev/null || {
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_PRINCIPAL_INVALID \
		"Expected exact non-login panel-ui UID/GID allocation without supplementary groups" \
		"$TRACE_ID"
	exit 1
}
for release_identity_contract in \
	'BHP_PANEL_UI_PRE_CONFIGURE_HOOKS += BHP_PANEL_UI_VALIDATE_APPLICATION_RELEASE_ID' \
	'BHP_PANEL_UI.APPLICATION_RELEASE_ID_MISSING'; do
	grep -F "$release_identity_contract" "$panel_ui_package" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_RELEASE_IDENTITY_CONTRACT_MISSING \
			"$release_identity_contract" "$TRACE_ID"
		exit 1
	}
done

for panel_ui_package_contract in \
	'BHP_PANEL_UI_DEPENDENCIES = openssl' \
	'-DBHP_BUILD_PANEL_UI=ON' \
	'-DPANEL_UI_BUILD_TESTS=OFF'; do
	grep -F -- "$panel_ui_package_contract" "$panel_ui_package" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_PACKAGE_CONTRACT_MISSING \
			"$panel_ui_package_contract" "$TRACE_ID"
		exit 1
	}
done

panel_ui_cmake=$PROJECT_ROOT/panel_ui/CMakeLists.txt
# This is a literal CMake source contract, not a verifier-shell expansion.
# shellcheck disable=SC2016
panel_ui_install_rule='install(TARGETS bhp-panel-ui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")'
grep -F -x "$panel_ui_install_rule" "$panel_ui_cmake" >/dev/null || {
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_PRODUCTION_INSTALL_RULE_MISSING \
		"Expected sole installed target bhp-panel-ui" "$TRACE_ID"
	exit 1
}
panel_ui_install_rule_count=$(grep -E -c '^[[:space:]]*install\(TARGETS[[:space:]]' \
	"$panel_ui_cmake" || true)
if [ "$panel_ui_install_rule_count" -ne 1 ]; then
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_INSTALLED_TARGET_SET_INVALID \
		"Expected one panel_ui install rule; found $panel_ui_install_rule_count" \
		"$TRACE_ID"
	exit 1
fi
if grep -E 'install\(TARGETS[[:space:]]+panel_ui_core_smoke' \
	"$panel_ui_cmake" >/dev/null; then
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_TEST_BINARY_INSTALLED \
		"panel_ui_core_smoke must remain host-test-only" "$TRACE_ID"
	exit 1
fi

panel_ui_init=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/rootfs-overlay/etc/init.d/S75bhp-panel-ui
panel_ui_environment_example=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/service-environment/panel-ui.env.example.in
test -f "$panel_ui_environment_example" || {
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_ENVIRONMENT_TEMPLATE_MISSING \
		"$panel_ui_environment_example" "$TRACE_ID"
	exit 1
}
for panel_ui_init_contract in \
	'BHP_PANEL_UI_ENVIRONMENT_FILE=/etc/beaglebone-house-panel/panel-ui.env' \
	'BHP_PANEL_UI.DISPLAY_COMMIT_BACKEND_UNAVAILABLE' \
	'panel_ui_disabled_by_configuration'; do
	grep -F "$panel_ui_init_contract" "$panel_ui_init" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_INIT_CONTRACT_MISSING \
			"$panel_ui_init_contract" "$TRACE_ID"
		exit 1
	}
done
if [ "$(grep -F -x -c 'BHP_PANEL_UI_ENABLED=0' \
	"$panel_ui_environment_example" || true)" -ne 1 ]; then
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_UI_DEFAULT_ENABLEMENT_INVALID \
		"Panel UI environment example must contain exactly one disabled default" \
		"$TRACE_ID"
	exit 1
fi

panel_supervisor_package=$PROJECT_ROOT/system_image/buildroot_external/package/bhp-panel-supervisor/bhp-panel-supervisor.mk
for release_identity_contract in \
	'BHP_PANEL_SUPERVISOR_DEPENDENCIES = openssl bhp-panel-ui' \
	'BHP_PANEL_SUPERVISOR_PRE_CONFIGURE_HOOKS += BHP_PANEL_SUPERVISOR_VALIDATE_APPLICATION_RELEASE_ID' \
	'BHP_PANEL_SUPERVISOR.APPLICATION_RELEASE_ID_MISSING'; do
	grep -F "$release_identity_contract" "$panel_supervisor_package" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_SUPERVISOR_RELEASE_IDENTITY_CONTRACT_MISSING \
			"$release_identity_contract" "$TRACE_ID"
		exit 1
	}
done

for required_toolchain_contract in \
	'PKG_CONFIG_EXECUTABLE' \
	'PKG_CONFIG_LIBDIR' \
	'CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY' \
	'CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY' \
	'CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY' \
	'CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE' \
	'CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE'; do
	grep -F "$required_toolchain_contract" \
		"$PROJECT_ROOT/cmake/toolchains/buildroot-sdk.cmake" >/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.TOOLCHAIN_CONTRACT_MISSING \
			"$required_toolchain_contract" "$TRACE_ID"
		exit 1
	}
	done
# Every board wrapper must pin its processor and delegate to the one canonical
# Buildroot SDK toolchain; the processor must match the board profile.
for toolchain_board in frdm-imx8mp:aarch64 beaglebone-black:arm; do
	toolchain_board_id=${toolchain_board%%:*}
	toolchain_processor=${toolchain_board#*:}
	toolchain_preset=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" \
		--board "$toolchain_board_id" --value BHP_BOARD_CMAKE_PRESET)
	toolchain_profile_processor=$("$PROJECT_ROOT/scripts/boards/show_board_profile.sh" \
		--board "$toolchain_board_id" --value BHP_BOARD_CMAKE_SYSTEM_PROCESSOR)
	[ "$toolchain_profile_processor" = "$toolchain_processor" ] || {
		bhp_fail VERIFY_BUILD_DEPLOY.TOOLCHAIN_PROFILE_MISMATCH \
			"$toolchain_board_id processor $toolchain_profile_processor" "$TRACE_ID"
		exit 1
	}
	toolchain_wrapper=$(python3 - "$PROJECT_ROOT/CMakePresets.json" "$toolchain_preset" <<'PY_TOOLCHAIN_PRESET'
import json
import sys

presets = json.load(open(sys.argv[1], encoding="utf-8"))["configurePresets"]
matches = [preset for preset in presets if preset["name"] == sys.argv[2]]
if len(matches) != 1:
    sys.exit(1)
print(matches[0]["toolchainFile"].replace("${sourceDir}/", ""))
PY_TOOLCHAIN_PRESET
	) || {
		bhp_fail VERIFY_BUILD_DEPLOY.TOOLCHAIN_PRESET_MISSING "$toolchain_preset" "$TRACE_ID"
		exit 1
	}
	# The CMake variable reference is a literal file contract.
	# shellcheck disable=SC2016
	for toolchain_wrapper_contract in \
		"set(BHP_BUILDROOT_TARGET_PROCESSOR $toolchain_processor)" \
		'include("${CMAKE_CURRENT_LIST_DIR}/buildroot-sdk.cmake")'; do
		grep -F -x "$toolchain_wrapper_contract" \
			"$PROJECT_ROOT/$toolchain_wrapper" >/dev/null || {
			bhp_fail VERIFY_BUILD_DEPLOY.TOOLCHAIN_WRAPPER_INVALID \
				"$toolchain_wrapper: $toolchain_wrapper_contract" "$TRACE_ID"
			exit 1
		}
	done
done

panel_supervisor_init=$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/rootfs-overlay/etc/init.d/S70panel_supervisord
# These are literal source contracts, not expressions for this verifier shell.
# shellcheck disable=SC2016
for panel_supervisor_restart_contract in \
	'BHP_PANEL_SUPERVISOR_RESTART_LIMIT=3' \
	'BHP_PANEL_SUPERVISOR_RESTART_BACKOFF_SECONDS=5' \
	'panel_supervisord-supervisor.pid' \
	'panel_supervisor_restart_attempt_' \
	'BHP_PANEL_SUPERVISOR.RESTART_LIMIT_EXHAUSTED' \
	'kill -TERM "$BHP_PANEL_SUPERVISOR_CHILD_PID"' \
	'chown root:panel-ui "$BHP_PANEL_SUPERVISOR_MODE_SOCKET"' \
	'chmod 0660 "$BHP_PANEL_SUPERVISOR_MODE_SOCKET"' \
	'-p "$SUPERVISOR_PIDFILE" -x /bin/sh' \
	'-p "$PIDFILE"' \
	'-x "$BHP_PANEL_SUPERVISOR_DAEMON"'; do
	grep -F -- "$panel_supervisor_restart_contract" "$panel_supervisor_init" \
		>/dev/null || {
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_SUPERVISOR_RESTART_CONTRACT_MISSING \
			"$panel_supervisor_restart_contract" "$TRACE_ID"
		exit 1
	}
done

# Execute the real foreground supervision path against a private root. The
# fixture shortens only the declared retry count/backoff; production values are
# asserted above. Its fake daemon creates a real Unix socket and records TERM.
panel_supervisor_fixture=$temporary_root/panel-supervisor-supervision
panel_supervisor_fixture_root=$panel_supervisor_fixture/root
panel_supervisor_fixture_bin=$panel_supervisor_fixture/bin
panel_supervisor_fixture_runtime=$panel_supervisor_fixture_root/run/beaglebone-house-panel
panel_supervisor_fixture_short_runtime_link=$(mktemp -d /tmp/bhp-ps.XXXXXX)
rmdir "$panel_supervisor_fixture_short_runtime_link"
ln -s "$panel_supervisor_fixture_runtime" \
	"$panel_supervisor_fixture_short_runtime_link"
panel_supervisor_fixture_pid_directory=$panel_supervisor_fixture_root/var/run
panel_supervisor_fixture_environment=$panel_supervisor_fixture_root/etc/beaglebone-house-panel/panel-supervisor.env
panel_supervisor_fixture_library=$panel_supervisor_fixture_root/etc/beaglebone-house-panel/init-service-library.sh
panel_supervisor_fixture_init=$panel_supervisor_fixture_root/etc/init.d/S70panel_supervisord
panel_supervisor_fixture_daemon=$panel_supervisor_fixture_root/usr/sbin/panel_supervisord
panel_supervisor_fixture_socket=$panel_supervisor_fixture_short_runtime_link/panel-mode.sock
panel_supervisor_fixture_attempts=$panel_supervisor_fixture/attempts
panel_supervisor_fixture_stop_record=$panel_supervisor_fixture/stop-record
panel_supervisor_fixture_supervisor_pid=$panel_supervisor_fixture_pid_directory/panel_supervisord-supervisor.pid
panel_supervisor_fixture_daemon_pid=$panel_supervisor_fixture_pid_directory/panel_supervisord.pid
panel_supervisor_fixture_ready=$panel_supervisor_fixture_pid_directory/panel_supervisord.ready
mkdir -p "$panel_supervisor_fixture_bin" "$panel_supervisor_fixture_runtime" \
	"$panel_supervisor_fixture_pid_directory" \
	"$(dirname "$panel_supervisor_fixture_environment")" \
	"$(dirname "$panel_supervisor_fixture_init")" \
	"$(dirname "$panel_supervisor_fixture_daemon")"

sed \
	-e 's/BHP_PANEL_SUPERVISOR_RESTART_LIMIT=3$/BHP_PANEL_SUPERVISOR_RESTART_LIMIT=2/' \
	-e 's/BHP_PANEL_SUPERVISOR_RESTART_BACKOFF_SECONDS=5$/BHP_PANEL_SUPERVISOR_RESTART_BACKOFF_SECONDS=1/' \
	"$panel_supervisor_init" >"$panel_supervisor_fixture_init"
chmod 0755 "$panel_supervisor_fixture_init"

{
	printf '. "%s"\n' \
		"$PROJECT_ROOT/system_image/buildroot_external/board/beaglebone-house-panel/rootfs-overlay/etc/beaglebone-house-panel/init-service-library.sh"
	# The appended function bodies are literal fixture source.
	# shellcheck disable=SC2016
	printf '%s\n' \
		'bhp_init_environment_file_is_secure()' \
		'{' \
		'  [ -f "$1" ] && [ -r "$1" ]' \
		'}' \
		'bhp_init_prepare_panel_runtime_directory()' \
		'{' \
		'  mkdir -p "$BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY" &&' \
		'    chmod 2750 "$BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY"' \
		'}'
} >"$panel_supervisor_fixture_library"
chmod 0600 "$panel_supervisor_fixture_library"

cat >"$panel_supervisor_fixture_bin/start-stop-daemon" <<'EOF_START_STOP_DAEMON'
#!/bin/sh
operation=
test_only=0
pidfile=
while [ "$#" -gt 0 ]; do
	case "$1" in
		-K) operation=stop; shift ;;
		-t) test_only=1; shift ;;
		-p) pidfile=$2; shift 2 ;;
		-x) shift 2 ;;
		*) shift ;;
	esac
done
[ "$operation" = stop ] && [ -r "$pidfile" ] || exit 1
pid=$(sed -n '1p' "$pidfile")
case "$pid" in
	''|*[!0-9]*) exit 1 ;;
esac
kill -0 "$pid" 2>/dev/null || exit 1
[ "$test_only" -eq 0 ] || exit 0
kill -TERM "$pid"
EOF_START_STOP_DAEMON
chmod 0755 "$panel_supervisor_fixture_bin/start-stop-daemon"

cat >"$panel_supervisor_fixture_bin/chown" <<'EOF_CHOWN'
#!/bin/sh
exit 0
EOF_CHOWN
chmod 0755 "$panel_supervisor_fixture_bin/chown"

cat >"$panel_supervisor_fixture_daemon" <<'EOF_PANEL_SUPERVISORD'
#!/usr/bin/env python3
import os
import signal
import socket
import sys
import time

attempt_path = os.environ["BHP_TEST_PANEL_SUPERVISOR_ATTEMPTS"]
try:
    with open(attempt_path, encoding="utf-8") as attempts_file:
        attempt = int(attempts_file.read()) + 1
except FileNotFoundError:
    attempt = 1
with open(attempt_path, "w", encoding="utf-8") as attempts_file:
    attempts_file.write(f"{attempt}\n")

socket_path = os.environ["BHP_PANEL_SUPERVISOR_MODE_SOCKET"]
try:
    os.unlink(socket_path)
except FileNotFoundError:
    pass
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(socket_path)
server.listen(1)

behavior = os.environ["BHP_TEST_PANEL_SUPERVISOR_BEHAVIOR"]
if behavior == "always_exit" or (
    behavior == "fail_once_then_hold" and attempt == 1
):
    server.close()
    os.unlink(socket_path)
    sys.exit(23)

stop_requested = False


def request_stop(_signal_number, _frame):
    global stop_requested
    stop_requested = True
    with open(
        os.environ["BHP_TEST_PANEL_SUPERVISOR_STOP_RECORD"],
        "w",
        encoding="utf-8",
    ) as stop_file:
        stop_file.write(f"{attempt}\n")


signal.signal(signal.SIGTERM, request_stop)
while not stop_requested:
    time.sleep(0.05)
server.close()
try:
    os.unlink(socket_path)
except FileNotFoundError:
    pass
EOF_PANEL_SUPERVISORD
chmod 0755 "$panel_supervisor_fixture_daemon"

printf '%s\n' 'BHP_PANEL_SUPERVISOR_ENABLED=0' \
	>"$panel_supervisor_fixture_environment"
chmod 0600 "$panel_supervisor_fixture_environment"
if env \
	PATH="$panel_supervisor_fixture_bin:$PATH" \
	BHP_PANEL_SUPERVISOR_INIT_LIBRARY="$panel_supervisor_fixture_library" \
	BHP_PANEL_SUPERVISOR_INIT_ENVIRONMENT_FILE="$panel_supervisor_fixture_environment" \
	BHP_PANEL_SUPERVISOR_INIT_SCRIPT_PATH="$panel_supervisor_fixture_init" \
	BHP_PANEL_SUPERVISOR_INIT_DAEMON_PATH="$panel_supervisor_fixture_daemon" \
	BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY="$panel_supervisor_fixture_short_runtime_link" \
	BHP_PANEL_SUPERVISOR_INIT_PID_DIRECTORY="$panel_supervisor_fixture_pid_directory" \
	BHP_PANEL_SUPERVISOR_MODE_SOCKET="$panel_supervisor_fixture_socket" \
	BHP_TEST_PANEL_SUPERVISOR_ATTEMPTS="$panel_supervisor_fixture_attempts" \
	BHP_TEST_PANEL_SUPERVISOR_STOP_RECORD="$panel_supervisor_fixture_stop_record" \
	BHP_TEST_PANEL_SUPERVISOR_BEHAVIOR=fail_once_then_hold \
	"$panel_supervisor_fixture_init" bhp-supervise >/dev/null 2>&1; then
	bhp_fail VERIFY_BUILD_DEPLOY.DISABLED_PANEL_SUPERVISION_STARTED \
		"Internal supervision bypassed opt-in enablement" "$TRACE_ID"
	exit 1
fi
test ! -e "$panel_supervisor_fixture_attempts"

printf '%s\n' 'BHP_PANEL_SUPERVISOR_ENABLED=1' \
	>"$panel_supervisor_fixture_environment"
panel_supervisor_fixture_restart_log=$panel_supervisor_fixture/restart.log
env \
	PATH="$panel_supervisor_fixture_bin:$PATH" \
	BHP_PANEL_SUPERVISOR_INIT_LIBRARY="$panel_supervisor_fixture_library" \
	BHP_PANEL_SUPERVISOR_INIT_ENVIRONMENT_FILE="$panel_supervisor_fixture_environment" \
	BHP_PANEL_SUPERVISOR_INIT_SCRIPT_PATH="$panel_supervisor_fixture_init" \
	BHP_PANEL_SUPERVISOR_INIT_DAEMON_PATH="$panel_supervisor_fixture_daemon" \
	BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY="$panel_supervisor_fixture_short_runtime_link" \
	BHP_PANEL_SUPERVISOR_INIT_PID_DIRECTORY="$panel_supervisor_fixture_pid_directory" \
	BHP_PANEL_SUPERVISOR_MODE_SOCKET="$panel_supervisor_fixture_socket" \
	BHP_TEST_PANEL_SUPERVISOR_ATTEMPTS="$panel_supervisor_fixture_attempts" \
	BHP_TEST_PANEL_SUPERVISOR_STOP_RECORD="$panel_supervisor_fixture_stop_record" \
	BHP_TEST_PANEL_SUPERVISOR_BEHAVIOR=fail_once_then_hold \
	"$panel_supervisor_fixture_init" bhp-supervise \
	>"$panel_supervisor_fixture_restart_log" 2>&1 &
panel_supervisor_fixture_process=$!
panel_supervisor_fixture_wait_count=0
while [ ! -r "$panel_supervisor_fixture_ready" ]; do
	if ! kill -0 "$panel_supervisor_fixture_process" 2>/dev/null ||
		[ "$panel_supervisor_fixture_wait_count" -ge 8 ]; then
		sed -n '1,40p' "$panel_supervisor_fixture_restart_log" >&2
		bhp_fail VERIFY_BUILD_DEPLOY.PANEL_SUPERVISOR_RESTART_NOT_READY \
			"Restarted fixture daemon did not become ready" "$TRACE_ID"
		exit 1
	fi
	sleep 1
	panel_supervisor_fixture_wait_count=$((panel_supervisor_fixture_wait_count + 1))
done
grep -F -x "$panel_supervisor_fixture_process" \
	"$panel_supervisor_fixture_supervisor_pid" >/dev/null
grep -F -x 2 "$panel_supervisor_fixture_attempts" >/dev/null
cmp -s "$panel_supervisor_fixture_daemon_pid" "$panel_supervisor_fixture_ready"
python3 -c 'import os,stat,sys; assert stat.S_IMODE(os.stat(sys.argv[1]).st_mode) == 0o660' \
	"$panel_supervisor_fixture_socket"
python3 - "$panel_supervisor_fixture_restart_log" <<'PY_RESTART_LOG'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as log_file:
    records = [json.loads(line) for line in log_file if line.strip()]
messages = {record["message"] for record in records}
assert "panel_supervisor_restart_attempt_1_of_2_scheduled_after_exit_23_backoff_1_seconds" in messages
assert "panel_supervisor_restart_attempt_1_of_2_running_backoff_1_seconds" in messages
assert all(record["service"] == "beaglebone_house_panel.panel_supervisor" for record in records)
PY_RESTART_LOG

env \
	PATH="$panel_supervisor_fixture_bin:$PATH" \
	BHP_PANEL_SUPERVISOR_INIT_LIBRARY="$panel_supervisor_fixture_library" \
	BHP_PANEL_SUPERVISOR_INIT_ENVIRONMENT_FILE="$panel_supervisor_fixture_environment" \
	BHP_PANEL_SUPERVISOR_INIT_SCRIPT_PATH="$panel_supervisor_fixture_init" \
	BHP_PANEL_SUPERVISOR_INIT_DAEMON_PATH="$panel_supervisor_fixture_daemon" \
	BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY="$panel_supervisor_fixture_short_runtime_link" \
	BHP_PANEL_SUPERVISOR_INIT_PID_DIRECTORY="$panel_supervisor_fixture_pid_directory" \
	BHP_PANEL_SUPERVISOR_MODE_SOCKET="$panel_supervisor_fixture_socket" \
	"$panel_supervisor_fixture_init" status >/dev/null
env \
	PATH="$panel_supervisor_fixture_bin:$PATH" \
	BHP_PANEL_SUPERVISOR_INIT_LIBRARY="$panel_supervisor_fixture_library" \
	BHP_PANEL_SUPERVISOR_INIT_ENVIRONMENT_FILE="$panel_supervisor_fixture_environment" \
	BHP_PANEL_SUPERVISOR_INIT_SCRIPT_PATH="$panel_supervisor_fixture_init" \
	BHP_PANEL_SUPERVISOR_INIT_DAEMON_PATH="$panel_supervisor_fixture_daemon" \
	BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY="$panel_supervisor_fixture_short_runtime_link" \
	BHP_PANEL_SUPERVISOR_INIT_PID_DIRECTORY="$panel_supervisor_fixture_pid_directory" \
	BHP_PANEL_SUPERVISOR_MODE_SOCKET="$panel_supervisor_fixture_socket" \
	"$panel_supervisor_fixture_init" stop >/dev/null
wait "$panel_supervisor_fixture_process"
grep -F -x 2 "$panel_supervisor_fixture_stop_record" >/dev/null
panel_supervisor_fixture_attempt_count_before_stop=$(
	sed -n '1p' "$panel_supervisor_fixture_attempts"
)
sleep 2
grep -F -x "$panel_supervisor_fixture_attempt_count_before_stop" \
	"$panel_supervisor_fixture_attempts" >/dev/null
grep -F 'panel_supervisor_restart_attempt_1_of_2_stopped_intentionally_backoff_1_seconds' \
	"$panel_supervisor_fixture_restart_log" >/dev/null
test ! -e "$panel_supervisor_fixture_supervisor_pid"
test ! -e "$panel_supervisor_fixture_daemon_pid"
test ! -e "$panel_supervisor_fixture_ready"

rm -f "$panel_supervisor_fixture_attempts" \
	"$panel_supervisor_fixture_stop_record" "$panel_supervisor_fixture_socket"
panel_supervisor_fixture_exhaustion_log=$panel_supervisor_fixture/exhaustion.log
env \
	PATH="$panel_supervisor_fixture_bin:$PATH" \
	BHP_PANEL_SUPERVISOR_INIT_LIBRARY="$panel_supervisor_fixture_library" \
	BHP_PANEL_SUPERVISOR_INIT_ENVIRONMENT_FILE="$panel_supervisor_fixture_environment" \
	BHP_PANEL_SUPERVISOR_INIT_SCRIPT_PATH="$panel_supervisor_fixture_init" \
	BHP_PANEL_SUPERVISOR_INIT_DAEMON_PATH="$panel_supervisor_fixture_daemon" \
	BHP_PANEL_SUPERVISOR_INIT_RUNTIME_DIRECTORY="$panel_supervisor_fixture_short_runtime_link" \
	BHP_PANEL_SUPERVISOR_INIT_PID_DIRECTORY="$panel_supervisor_fixture_pid_directory" \
	BHP_PANEL_SUPERVISOR_MODE_SOCKET="$panel_supervisor_fixture_socket" \
	BHP_TEST_PANEL_SUPERVISOR_ATTEMPTS="$panel_supervisor_fixture_attempts" \
	BHP_TEST_PANEL_SUPERVISOR_STOP_RECORD="$panel_supervisor_fixture_stop_record" \
	BHP_TEST_PANEL_SUPERVISOR_BEHAVIOR=always_exit \
	"$panel_supervisor_fixture_init" bhp-supervise \
	>"$panel_supervisor_fixture_exhaustion_log" 2>&1 &
panel_supervisor_fixture_process=$!
if wait "$panel_supervisor_fixture_process"; then
	bhp_fail VERIFY_BUILD_DEPLOY.PANEL_SUPERVISOR_RETRY_LIMIT_IGNORED \
		"Fixture supervisor survived after exhausting finite retries" "$TRACE_ID"
	exit 1
fi
grep -F -x 3 "$panel_supervisor_fixture_attempts" >/dev/null
python3 - "$panel_supervisor_fixture_exhaustion_log" <<'PY_EXHAUSTION_LOG'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as log_file:
    records = [json.loads(line) for line in log_file if line.strip()]
messages = {record["message"] for record in records}
for attempt in (1, 2):
    assert f"panel_supervisor_restart_attempt_{attempt}_of_2_scheduled_after_exit_23_backoff_1_seconds" in messages
    assert f"panel_supervisor_restart_attempt_{attempt}_of_2_startup_not_ready_exit_23_backoff_1_seconds" in messages
assert "panel_supervisor_restart_attempt_2_of_2_limit_exhausted_after_exit_23_backoff_1_seconds" in messages
assert any(record.get("error_code") == "BHP_PANEL_SUPERVISOR.RESTART_LIMIT_EXHAUSTED" for record in records)
PY_EXHAUSTION_LOG
test ! -e "$panel_supervisor_fixture_supervisor_pid"
test ! -e "$panel_supervisor_fixture_daemon_pid"
test ! -e "$panel_supervisor_fixture_ready"

grep -F -x '*.credential' "$PROJECT_ROOT/.gitignore" >/dev/null
python3 -m json.tool \
	"$PROJECT_ROOT/hardware_tests/schemas/power_measurement.schema.json" >/dev/null
python3 -c \
	'import json,sys; schema=json.load(open(sys.argv[1])); assert "schema_version" in schema["required"]; assert schema["properties"]["schema_version"].get("const") == 2; assert "supply_boundary" in schema["required"]; assert "board_input" in schema["properties"]["supply_boundary"]["enum"]; assert "accessory_revision" in schema["required"]; assert "cape_revision" not in schema["properties"]' \
	"$PROJECT_ROOT/hardware_tests/schemas/power_measurement.schema.json"

bhp_log info build_and_deploy_contract_verification_complete "$TRACE_ID" \
	hardware_evidence not_performed
