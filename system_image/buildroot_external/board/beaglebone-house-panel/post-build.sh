#!/bin/sh
# Adds project provenance to the target root filesystem.
# Buildroot passes TARGET_DIR as the first argument and exports BR2_EXTERNAL_BHP_PATH.

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
# may grant one exact provisioned name+phys identity to the dedicated touch
# group; unknown devices and the PIR identities remain root-only.
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

mkdir -p "$target_directory/etc/beaglebone-house-panel"
image_version_path=$target_directory/etc/beaglebone-house-panel/image-version
image_profile_path=$target_directory/etc/beaglebone-house-panel/image-profile
test ! -L "$image_version_path"
test ! -L "$image_profile_path"
{
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
