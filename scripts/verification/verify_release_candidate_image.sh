#!/bin/sh
# Builds from a verified archive into fresh source/output trees on Linux x86_64.
# Inputs: BHP_BUILDROOT_ARCHIVE, BHP_RELEASE_OUTPUT_PARENT, optional jobs.
# Evidence boundary: proves build integration only; it performs no hardware test.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../build/buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_release_candidate_image
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
	printf '%s\n' \
		"Usage: BHP_BUILDROOT_ARCHIVE=/abs/buildroot-2026.05.1.tar.xz BHP_RELEASE_OUTPUT_PARENT=/abs/dedicated-output-parent [BHP_BUILD_JOBS=N] $0"
	exit 0
fi

if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
	bhp_fail RELEASE_CANDIDATE.HOST_UNSUPPORTED \
		"Full release evidence requires Linux x86_64" "$TRACE_ID"
	exit 1
fi
: "${BHP_BUILDROOT_ARCHIVE:?BHP_BUILDROOT_ARCHIVE is required}"
: "${BHP_RELEASE_OUTPUT_PARENT:?BHP_RELEASE_OUTPUT_PARENT is required}"
if [ "${BHP_BUILDROOT_DIR+x}" = x ]; then
	bhp_fail RELEASE_CANDIDATE.PREEXISTING_SOURCE_FORBIDDEN \
		"Release evidence extracts its own source; unset BHP_BUILDROOT_DIR" \
		"$TRACE_ID"
	exit 1
fi
if [ "${BHP_BUILDROOT_OUTPUT_DIR+x}" = x ]; then
	bhp_fail RELEASE_CANDIDATE.INCREMENTAL_OUTPUT_FORBIDDEN \
		"Release evidence allocates its own fresh output; unset BHP_BUILDROOT_OUTPUT_DIR" \
		"$TRACE_ID"
	exit 1
fi
if [ "${BHP_BUILDROOT_PROFILE:-production}" != production ] ||
   [ -n "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:-}" ]; then
	bhp_fail RELEASE_CANDIDATE.NON_PRODUCTION_INPUT_FORBIDDEN \
		"Release evidence always configures the SSH-free production profile" \
		"$TRACE_ID"
	exit 1
fi
for required_path in "$BHP_BUILDROOT_ARCHIVE" "$BHP_RELEASE_OUTPUT_PARENT"; do
	case "$required_path" in
		/*) ;;
		*) bhp_fail RELEASE_CANDIDATE.PATH_NOT_ABSOLUTE "$required_path" "$TRACE_ID"; exit 1 ;;
	esac
	case "$required_path" in
		/|/home|/root|/usr|/opt)
			bhp_fail RELEASE_CANDIDATE.PATH_UNSAFE "$required_path" "$TRACE_ID"
			exit 1
			;;
	esac
done
test -f "$BHP_BUILDROOT_ARCHIVE" && test ! -L "$BHP_BUILDROOT_ARCHIVE" || {
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_ARCHIVE_INVALID \
		"Buildroot archive must be a regular non-symlink file" "$TRACE_ID"
	exit 1
}
buildroot_archive_sha256=$(bhp_sha256_file "$BHP_BUILDROOT_ARCHIVE") || {
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_ARCHIVE_ID_FAILED \
		"$BHP_BUILDROOT_ARCHIVE" "$TRACE_ID"
	exit 1
}
if [ "$buildroot_archive_sha256" != \
	"$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256" ]; then
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_ARCHIVE_ID_MISMATCH \
		"Archive is not the exact pinned Buildroot release" "$TRACE_ID"
	exit 1
fi
test -d "$BHP_RELEASE_OUTPUT_PARENT" &&
	test ! -L "$BHP_RELEASE_OUTPUT_PARENT" || {
	bhp_fail RELEASE_CANDIDATE.OUTPUT_PARENT_INVALID \
		"Create a dedicated real directory outside both source trees" "$TRACE_ID"
	exit 1
}

source_content_id=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$PROJECT_ROOT"
) || {
	bhp_fail RELEASE_CANDIDATE.SOURCE_ID_FAILED \
		"The exact Git-visible project content could not be identified" "$TRACE_ID"
	exit 1
}
case "$source_content_id" in
	*[!0-9a-f]*|'')
		bhp_fail RELEASE_CANDIDATE.SOURCE_ID_INVALID "$source_content_id" "$TRACE_ID"
		exit 1
		;;
esac
[ "${#source_content_id}" -eq 64 ] || {
	bhp_fail RELEASE_CANDIDATE.SOURCE_ID_INVALID "$source_content_id" "$TRACE_ID"
	exit 1
}
canonical_output_parent=$(
	bhp_buildroot_canonical_existing_directory "$BHP_RELEASE_OUTPUT_PARENT"
) || {
	bhp_fail RELEASE_CANDIDATE.OUTPUT_PARENT_INVALID \
		"Unable to resolve output parent" "$TRACE_ID"
	exit 1
}
canonical_project_root=$(
	bhp_buildroot_canonical_existing_directory "$PROJECT_ROOT"
)
proposed_run=$canonical_output_parent/bhp-release-$source_content_id.fresh
if bhp_buildroot_paths_overlap "$proposed_run" "$canonical_project_root"; then
	bhp_fail RELEASE_CANDIDATE.OUTPUT_OVERLAP \
		"Fresh release run must be separate from the project tree" "$TRACE_ID"
	exit 1
fi
release_run_directory=$(mktemp -d \
	"$canonical_output_parent/bhp-release-$source_content_id.XXXXXX")
release_source_parent=$release_run_directory/source
mkdir "$release_source_parent"
tar -xJf "$BHP_BUILDROOT_ARCHIVE" -C "$release_source_parent"
BHP_BUILDROOT_DIR=$release_source_parent/buildroot-$BHP_PINNED_BUILDROOT_VERSION
export BHP_BUILDROOT_DIR
source_top_entries=$(find "$release_source_parent" -mindepth 1 -maxdepth 1 -print)
source_top_count=$(printf '%s\n' "$source_top_entries" |
	sed '/^$/d' | wc -l | tr -d ' ')
if [ "$source_top_count" -ne 1 ] ||
   [ "$source_top_entries" != "$BHP_BUILDROOT_DIR" ]; then
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_ARCHIVE_SHAPE_INVALID \
		"Pinned archive must extract exactly one expected source directory" \
		"$TRACE_ID"
	exit 1
fi
buildroot_source_id=$(
	bhp_buildroot_read_source_id "$BHP_BUILDROOT_DIR" \
		"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh"
) || {
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_SOURCE_ID_FAILED \
		"Fresh Buildroot source could not be identified" "$TRACE_ID"
	exit 1
}
if [ "$buildroot_source_id" != "$BHP_PINNED_BUILDROOT_SOURCE_ID" ]; then
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_SOURCE_ID_MISMATCH \
		"Verified archive extraction did not match its pinned canonical tree" \
		"$TRACE_ID"
	exit 1
fi
actual_buildroot_version=$(
	bhp_buildroot_read_source_version "$BHP_BUILDROOT_DIR"
) || {
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_VERSION_UNREADABLE \
		"Buildroot Makefile version declaration was unreadable" "$TRACE_ID"
	exit 1
}
if [ "$actual_buildroot_version" != "$BHP_PINNED_BUILDROOT_VERSION" ]; then
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_VERSION_MISMATCH \
		"Expected $BHP_PINNED_BUILDROOT_VERSION; found $actual_buildroot_version" \
		"$TRACE_ID"
	exit 1
fi
BHP_BUILDROOT_OUTPUT_DIR=$release_run_directory/output
export BHP_BUILDROOT_OUTPUT_DIR
if ! bhp_buildroot_output_is_separate "$BHP_BUILDROOT_DIR" \
	"$BHP_BUILDROOT_OUTPUT_DIR" "$PROJECT_ROOT"; then
	bhp_fail RELEASE_CANDIDATE.FRESH_OUTPUT_INVALID \
		"Atomic output allocation violated path separation" "$TRACE_ID"
	exit 1
fi
BHP_BUILDROOT_PROFILE=production
export BHP_BUILDROOT_PROFILE
bhp_log info release_candidate_fresh_output_allocated "$TRACE_ID" \
	release_run_directory "$release_run_directory" \
	output_directory "$BHP_BUILDROOT_OUTPUT_DIR" \
	project_content_id "$source_content_id" \
	buildroot_source_id "$buildroot_source_id" \
	buildroot_archive_sha256 "$buildroot_archive_sha256"
"$PROJECT_ROOT/scripts/build/configure_buildroot.sh"
test -s "$BHP_BUILDROOT_OUTPUT_DIR/.config" || {
	bhp_fail RELEASE_CANDIDATE.CONFIG_MISSING \
		"Fresh production configuration was not generated" "$TRACE_ID"
	exit 1
}
grep -F -x production "$BHP_BUILDROOT_OUTPUT_DIR/.bhp-build-profile" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROFILE_INVALID \
		"Fresh output is not stamped as production" "$TRACE_ID"
	exit 1
}
for required_config in \
	'BR2_PACKAGE_BHP_PANEL_SUPERVISOR=y' \
	'BR2_PACKAGE_BHP_PRESENCE_SENSOR=y' \
	'BR2_PACKAGE_BHP_VOCABULARY_PANEL=y' \
	'BR2_PACKAGE_LVGL=y'; do
	grep -F -x "$required_config" "$BHP_BUILDROOT_OUTPUT_DIR/.config" >/dev/null || {
		bhp_fail RELEASE_CANDIDATE.CONFIG_VALUE_MISSING "$required_config" "$TRACE_ID"
		exit 1
	}
done
if grep -F -x 'BR2_PACKAGE_DROPBEAR=y' \
	"$BHP_BUILDROOT_OUTPUT_DIR/.config" >/dev/null; then
	bhp_fail RELEASE_CANDIDATE.SSH_SERVER_FORBIDDEN \
		"Production release configuration selected Dropbear" "$TRACE_ID"
	exit 1
fi

bhp_log info release_candidate_build_started "$TRACE_ID" \
	buildroot_version "$actual_buildroot_version" \
	hardware_evidence not_performed
"$PROJECT_ROOT/scripts/build/build_buildroot_image.sh"

bhp_release_require_file()
{
	BHP_RELEASE_REQUIRED_FILE=$1
	BHP_RELEASE_REQUIRED_LABEL=$2
	if [ ! -s "$BHP_RELEASE_REQUIRED_FILE" ] || [ -L "$BHP_RELEASE_REQUIRED_FILE" ]; then
		bhp_fail RELEASE_CANDIDATE.ARTIFACT_MISSING \
			"$BHP_RELEASE_REQUIRED_LABEL:$BHP_RELEASE_REQUIRED_FILE" "$TRACE_ID"
		return 1
	fi
}

bhp_release_require_single_match()
{
	BHP_RELEASE_MATCH_ROOT=$1
	BHP_RELEASE_MATCH_NAME=$2
	BHP_RELEASE_MATCH_LABEL=$3
	if [ ! -d "$BHP_RELEASE_MATCH_ROOT" ] || [ -L "$BHP_RELEASE_MATCH_ROOT" ]; then
		bhp_fail RELEASE_CANDIDATE.ARTIFACT_ROOT_MISSING \
			"$BHP_RELEASE_MATCH_LABEL:$BHP_RELEASE_MATCH_ROOT" "$TRACE_ID"
		return 1
	fi
	BHP_RELEASE_MATCHES=$(find "$BHP_RELEASE_MATCH_ROOT" -type f \
		-name "$BHP_RELEASE_MATCH_NAME" -print) || return 1
	BHP_RELEASE_MATCH_COUNT=$(printf '%s\n' "$BHP_RELEASE_MATCHES" |
		sed '/^$/d' | wc -l | tr -d ' ')
	if [ "$BHP_RELEASE_MATCH_COUNT" -ne 1 ]; then
		bhp_fail RELEASE_CANDIDATE.ARTIFACT_COUNT_INVALID \
			"$BHP_RELEASE_MATCH_LABEL:expected=1:actual=$BHP_RELEASE_MATCH_COUNT" \
			"$TRACE_ID"
		return 1
	fi
	bhp_release_require_file "$BHP_RELEASE_MATCHES" "$BHP_RELEASE_MATCH_LABEL"
}

target_directory=$BHP_BUILDROOT_OUTPUT_DIR/target
build_directory=$BHP_BUILDROOT_OUTPUT_DIR/build
images_directory=$BHP_BUILDROOT_OUTPUT_DIR/images
bhp_release_require_file "$images_directory/sdcard.img" sdcard_image
bhp_release_require_file "$target_directory/usr/sbin/panel_supervisord" panel_supervisord
bhp_release_require_file "$target_directory/usr/bin/vocabulary_panel_terminal" vocabulary_terminal
bhp_release_require_file \
	"$target_directory/usr/lib/beaglebone-house-panel/device-tree-overlays/beagle-house-custom-pir.dtbo" \
	custom_pir_dtbo
bhp_release_require_file \
	"$target_directory/usr/lib/beaglebone-house-panel/device-tree-overlays/beagle-house-gpio-keys-wake-baseline.dtbo" \
	gpio_keys_baseline_dtbo
bhp_release_require_single_match "$build_directory" \
	libvocabulary_panel_lvgl_renderer.a vocabulary_lvgl_renderer
bhp_release_require_single_match "$target_directory/lib/modules" \
	'beagle_house_pir_input_driver.ko*' presence_sensor_kernel_module

image_version=$target_directory/etc/beaglebone-house-panel/image-version
bhp_release_require_file "$image_version" image_provenance
grep -F -x "buildroot_version=$BHP_PINNED_BUILDROOT_VERSION" "$image_version" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROVENANCE_VERSION_INVALID "$image_version" "$TRACE_ID"
	exit 1
}
grep -F -x "buildroot_source_id=$buildroot_source_id" "$image_version" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROVENANCE_BUILDROOT_SOURCE_ID_INVALID \
		"$image_version" "$TRACE_ID"
	exit 1
}
grep -F -x "buildroot_archive_sha256=$buildroot_archive_sha256" \
	"$image_version" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROVENANCE_BUILDROOT_ARCHIVE_ID_INVALID \
		"$image_version" "$TRACE_ID"
	exit 1
}
grep -E -x 'project_content_state=(clean|dirty|uncommitted|source-archive)' \
	"$image_version" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROVENANCE_CONTENT_STATE_MISSING "$image_version" "$TRACE_ID"
	exit 1
}
grep -F -x "project_content_id=$source_content_id" "$image_version" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.PROVENANCE_CONTENT_ID_INVALID "$image_version" "$TRACE_ID"
	exit 1
}
image_profile=$target_directory/etc/beaglebone-house-panel/image-profile
bhp_release_require_file "$image_profile" image_profile
grep -F -x production "$image_profile" >/dev/null || {
	bhp_fail RELEASE_CANDIDATE.IMAGE_PROFILE_INVALID "$image_profile" "$TRACE_ID"
	exit 1
}

final_source_content_id=$(
	"$PROJECT_ROOT/scripts/build/project_content_id.sh" "$PROJECT_ROOT"
) || {
	bhp_fail RELEASE_CANDIDATE.FINAL_SOURCE_ID_FAILED \
		"Project content could not be re-identified after the build" "$TRACE_ID"
	exit 1
}
if [ "$final_source_content_id" != "$source_content_id" ]; then
	bhp_fail RELEASE_CANDIDATE.SOURCE_CHANGED_DURING_BUILD \
		"before=$source_content_id:after=$final_source_content_id" "$TRACE_ID"
	exit 1
fi
final_buildroot_source_id=$(
	bhp_buildroot_read_source_id "$BHP_BUILDROOT_DIR" \
		"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh"
) || {
	bhp_fail RELEASE_CANDIDATE.FINAL_BUILDROOT_SOURCE_ID_FAILED \
		"Buildroot source could not be re-identified after the build" "$TRACE_ID"
	exit 1
}
if [ "$final_buildroot_source_id" != "$buildroot_source_id" ]; then
	bhp_fail RELEASE_CANDIDATE.BUILDROOT_SOURCE_CHANGED_DURING_BUILD \
		"before=$buildroot_source_id:after=$final_buildroot_source_id" "$TRACE_ID"
	exit 1
fi
sdcard_sha256=$(bhp_sha256_file "$images_directory/sdcard.img") || {
	bhp_fail RELEASE_CANDIDATE.IMAGE_DIGEST_FAILED \
		"$images_directory/sdcard.img" "$TRACE_ID"
	exit 1
}
evidence_file=$BHP_BUILDROOT_OUTPUT_DIR/release-evidence.env
test ! -e "$evidence_file" && test ! -L "$evidence_file" || {
	bhp_fail RELEASE_CANDIDATE.EVIDENCE_PATH_PREEXISTED "$evidence_file" "$TRACE_ID"
	exit 1
}
{
	printf 'evidence_version=2\n'
	printf 'profile=production\n'
	printf 'project_content_id=%s\n' "$source_content_id"
	printf 'buildroot_version=%s\n' "$actual_buildroot_version"
	printf 'buildroot_source_id=%s\n' "$buildroot_source_id"
	printf 'buildroot_archive_sha256=%s\n' "$buildroot_archive_sha256"
	printf 'sdcard_sha256=%s\n' "$sdcard_sha256"
	printf 'hardware_evidence=not_performed\n'
} >"$evidence_file"

bhp_log info release_candidate_build_evidence_complete "$TRACE_ID" \
	image "$images_directory/sdcard.img" \
	evidence "$evidence_file" \
	project_content_id "$source_content_id" \
	buildroot_source_id "$buildroot_source_id" \
	buildroot_archive_sha256 "$buildroot_archive_sha256" \
	buildroot_version "$actual_buildroot_version" \
	hardware_evidence not_performed
