#!/bin/sh
# Adds project provenance and the selected board profile to the target root filesystem.
# Buildroot passes TARGET_DIR as the first argument and exports BR2_EXTERNAL_BHP_PATH
# and BR2_CONFIG (whose BR2_BHP_BOARD_ID selects config/boards/<id>.env).

set -eu

target_directory=${1:?TARGET_DIR argument is required}
: "${BR2_EXTERNAL_BHP_PATH:?BR2_EXTERNAL_BHP_PATH is required}"
project_root=$BR2_EXTERNAL_BHP_PATH/../..
# The external-tree location is dynamic from ShellCheck's working directory.
# shellcheck source=/dev/null
. "$project_root/scripts/build/buildroot_contract.sh"
if [ "${BR2_VERSION:-}" != "$BHP_PINNED_BUILDROOT_VERSION" ]; then
	printf '%s\n' \
		"BHP_IMAGE.BUILDROOT_VERSION_MISMATCH: expected $BHP_PINNED_BUILDROOT_VERSION, found ${BR2_VERSION:-unset}" >&2
	exit 1
fi
buildroot_version_full=${BR2_VERSION_FULL:-$BR2_VERSION}
case "$buildroot_version_full" in
	''|*[!A-Za-z0-9._+-]*)
		printf '%s\n' 'BHP_IMAGE.BUILDROOT_VERSION_FULL_INVALID: expected one safe nonempty token' >&2
		exit 1
		;;
esac
buildroot_source_id=${BHP_BUILDROOT_SOURCE_ID:-}
buildroot_archive_sha256=${BHP_BUILDROOT_ARCHIVE_SHA256:-}
case "$buildroot_source_id" in
	''|*[!0-9a-f]*)
		printf '%s\n' 'BHP_IMAGE.BUILDROOT_SOURCE_ID_INVALID: expected 64 lowercase hex characters' >&2
		exit 1
		;;
esac
if [ "${#buildroot_source_id}" -ne 64 ]; then
	printf '%s\n' 'BHP_IMAGE.BUILDROOT_SOURCE_ID_INVALID: expected 64 lowercase hex characters' >&2
	exit 1
fi
case "$buildroot_archive_sha256" in
	unverified) ;;
	*[!0-9a-f]*|'')
		printf '%s\n' 'BHP_IMAGE.BUILDROOT_ARCHIVE_ID_INVALID' >&2
		exit 1
		;;
	*)
		[ "${#buildroot_archive_sha256}" -eq 64 ] || {
			printf '%s\n' 'BHP_IMAGE.BUILDROOT_ARCHIVE_ID_INVALID' >&2
			exit 1
		}
		;;
esac
if [ "$buildroot_source_id" = "$BHP_PINNED_BUILDROOT_SOURCE_ID" ]; then
	[ "$buildroot_archive_sha256" = "$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256" ] || {
		printf '%s\n' 'BHP_IMAGE.BUILDROOT_SOURCE_ARCHIVE_ID_INCONSISTENT' >&2
		exit 1
	}
elif [ "$buildroot_archive_sha256" != unverified ]; then
	printf '%s\n' 'BHP_IMAGE.BUILDROOT_SOURCE_ARCHIVE_ID_INCONSISTENT' >&2
	exit 1
fi
test -d "$target_directory"
: "${BR2_CONFIG:?BR2_CONFIG is required}"
board_id=$(sed -n 's/^BR2_BHP_BOARD_ID="\([a-z0-9-]*\)"$/\1/p' "$BR2_CONFIG")
case "$board_id" in
	''|-*|*-|*[!a-z0-9-]*)
		printf '%s\n' 'BHP_IMAGE.BOARD_ID_INVALID: BR2_BHP_BOARD_ID must name config/boards/<id>.env' >&2
		exit 1
		;;
esac
board_profile_source=$project_root/config/boards/$board_id.env
if [ ! -f "$board_profile_source" ] || [ -L "$board_profile_source" ]; then
	printf '%s\n' "BHP_IMAGE.BOARD_PROFILE_MISSING: $board_id" >&2
	exit 1
fi
grep -F -x "BHP_BOARD_ID=$board_id" "$board_profile_source" >/dev/null || {
	printf '%s\n' "BHP_IMAGE.BOARD_PROFILE_ID_MISMATCH: $board_id" >&2
	exit 1
}
project_version=$(sed -n '1p' "$project_root/VERSION")
case "$project_version" in
	''|*[!A-Za-z0-9._+-]*)
		printf '%s\n' 'BHP_IMAGE.PROJECT_VERSION_INVALID: VERSION must contain one safe nonempty token' >&2
		exit 1
		;;
esac

project_commit=unavailable
project_content_state=source-archive
project_content_id=unavailable
if command -v git >/dev/null 2>&1 &&
   git -C "$project_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	project_content_id=$(
		"$project_root/scripts/build/project_content_id.sh" "$project_root"
	) || {
		printf '%s\n' 'BHP_IMAGE.PROJECT_CONTENT_ID_FAILED: unable to hash source tree' >&2
		exit 1
	}
	if project_commit=$(git -C "$project_root" rev-parse --verify HEAD 2>/dev/null); then
		project_status=$(git -C "$project_root" status --porcelain=v1 --untracked-files=normal) || {
			printf '%s\n' 'BHP_IMAGE.GIT_STATUS_FAILED: unable to establish source content state' >&2
			exit 1
		}
		if [ -n "$project_status" ]; then
			project_content_state=dirty
		else
			project_content_state=clean
		fi
	else
		project_commit=uncommitted
		project_content_state=uncommitted
	fi
fi
mdev_configuration=$target_directory/etc/mdev.conf
touch_classifier=$target_directory/usr/libexec/beaglebone-house-panel/classify-touch-input

# Buildroot's stock mdev policy makes all input nodes root-only and has no
# explicit DRM/fbdev rules. Keep every input node root-only. An add-event helper
# revokes legacy UI grants even for an exactly provisioned name+phys identity.
# ADR 0005 reserves raw touch and PIR input for the supervisor.
# Fail if the upstream file shape changes instead of silently shipping an image
# whose UI cannot open its configured devices.
test -f "$mdev_configuration"
test ! -L "$mdev_configuration"
test -f "$touch_classifier"
test ! -L "$touch_classifier"
test -x "$touch_classifier"
awk '
BEGIN {
	events = 0
	mice = 0
	mouse = 0
	touch = 0
}
$1 == "input/event[0-9]+" {
	print "input/event[0-9]+\troot:root 600 @/usr/libexec/beaglebone-house-panel/classify-touch-input"
	events = 1
	next
}
$1 == "input/mice" {
	print "input/mice\troot:root 600"
	mice = 1
	next
}
$1 == "input/mouse[0-9]" {
	print "input/mouse[0-9]\troot:root 600"
	mouse = 1
	next
}
$1 == "input/ts[0-9]" {
	print "input/ts[0-9]\troot:root 600"
	touch = 1
	next
}
{ print }
END {
	if (!events || !mice || !mouse || !touch)
		exit 42
	print ""
	print "# BeagleBone House Panel direct-display devices"
	print "dri/card[0-9]+\troot:video 660"
	print "dri/renderD[0-9]+\troot:video 660"
	print "fb[0-9]+\troot:video 660"
}
' "$mdev_configuration" >"$mdev_configuration.bhp-new"
mv "$mdev_configuration.bhp-new" "$mdev_configuration"

# On-target evidence scripts and operators read the same board facts the image
# was built from. UNMEASURED values stay placeholders on the target too.
board_share_directory=$target_directory/usr/share/beaglebone-house-panel
mkdir -p "$board_share_directory"
test ! -L "$board_share_directory/board-profile.env"
test ! -L "$board_share_directory/board_profile.sh"
install -m 0644 "$board_profile_source" "$board_share_directory/board-profile.env"
install -m 0644 "$project_root/scripts/boards/board_profile.sh" \
	"$board_share_directory/board_profile.sh"

mkdir -p "$target_directory/etc/beaglebone-house-panel"
# Service environment examples carry this board's facts; an unmeasured fact is
# rendered as REPLACE_ME_UNMEASURED_<gate>, which every service rejects.
for service_environment in panel-supervisor presence-sensor panel-ui; do
	service_environment_output=$target_directory/etc/beaglebone-house-panel/$service_environment.env.example
	test ! -L "$service_environment_output"
	"$project_root/scripts/boards/render_service_environment.sh" \
		--profile "$board_profile_source" \
		--template "$BR2_EXTERNAL_BHP_PATH/board/beaglebone-house-panel/service-environment/$service_environment.env.example.in" \
		--output "$service_environment_output" >/dev/null
	chmod 0644 "$service_environment_output"
done
image_version_path=$target_directory/etc/beaglebone-house-panel/image-version
image_profile_path=$target_directory/etc/beaglebone-house-panel/image-profile
test ! -L "$image_version_path"
test ! -L "$image_profile_path"
{
  printf 'board_id=%s\n' "$board_id"
  printf 'project_version=%s\n' "$project_version"
  printf 'project_commit=%s\n' "$project_commit"
  printf 'project_content_state=%s\n' "$project_content_state"
  printf 'project_content_id=%s\n' "$project_content_id"
  printf 'buildroot_version=%s\n' "$BR2_VERSION"
  printf 'buildroot_version_full=%s\n' "$buildroot_version_full"
  printf 'buildroot_source_id=%s\n' "$buildroot_source_id"
  printf 'buildroot_archive_sha256=%s\n' "$buildroot_archive_sha256"
} >"$image_version_path"
printf '%s\n' production >"$image_profile_path"
