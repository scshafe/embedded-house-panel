#!/bin/sh
# Downloads and authenticates the pinned Buildroot archive and extracted tree.
# Inputs: --destination ABSOLUTE_DIRECTORY.
# Side effects: creates only verified artifacts below the explicit destination.
# Existing caller trees/files are verified or rejected, never removed/replaced.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
# shellcheck source=buildroot_contract.sh
. "$PROJECT_ROOT/scripts/build/buildroot_contract.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.buildroot_download
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

destination=
download_temporary=
receipt_temporary=
extraction_stage=

usage()
{
	printf '%s\n' "Usage: $0 --destination /absolute/directory"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--destination)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			destination=$2
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done

case "$destination" in
	/*) ;;
	*)
		bhp_fail BUILDROOT.DESTINATION_NOT_ABSOLUTE \
			"Destination must be absolute" "$TRACE_ID"
		exit 1
		;;
esac
case "$destination" in
	/|/Users|/home|/root)
		bhp_fail BUILDROOT.DESTINATION_UNSAFE "Destination is too broad" "$TRACE_ID"
		exit 1
		;;
esac
if [ -e "$destination" ] || [ -L "$destination" ]; then
	[ -d "$destination" ] && [ ! -L "$destination" ] || {
		bhp_fail BUILDROOT.DESTINATION_INVALID \
			"Existing destination must be a real directory" "$TRACE_ID"
		exit 1
	}
else
	mkdir -p "$destination"
fi
canonical_destination=$(
	bhp_buildroot_canonical_existing_directory "$destination"
) || {
	bhp_fail BUILDROOT.DESTINATION_UNRESOLVED "$destination" "$TRACE_ID"
	exit 1
}
source_directory=$canonical_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION
archive_path=$canonical_destination/$BHP_PINNED_BUILDROOT_ARCHIVE
receipt_path=$canonical_destination/buildroot-$BHP_PINNED_BUILDROOT_VERSION.bhp-verified-source

cleanup_owned_temporary_paths()
{
	if [ -n "$download_temporary" ]; then
		case "$download_temporary" in
			"$canonical_destination"/.bhp-buildroot-download.*)
				[ ! -L "$download_temporary" ] || return
				rm -f -- "$download_temporary"
				;;
		esac
	fi
	if [ -n "$receipt_temporary" ]; then
		case "$receipt_temporary" in
			"$canonical_destination"/.bhp-buildroot-receipt.*)
				[ ! -L "$receipt_temporary" ] || return
				rm -f -- "$receipt_temporary"
				;;
		esac
	fi
	if [ -n "$extraction_stage" ]; then
		case "$extraction_stage" in
			"$canonical_destination"/.bhp-buildroot-extract.*)
				[ -d "$extraction_stage" ] && [ ! -L "$extraction_stage" ] || return
				find "$extraction_stage" -depth -delete
				;;
		esac
	fi
}
trap cleanup_owned_temporary_paths EXIT HUP INT TERM

bhp_verify_archive()
{
	BHP_DOWNLOADER_ARCHIVE=$1
	[ -f "$BHP_DOWNLOADER_ARCHIVE" ] || return 1
	[ ! -L "$BHP_DOWNLOADER_ARCHIVE" ] || return 1
	BHP_DOWNLOADER_ARCHIVE_ID=$(bhp_sha256_file "$BHP_DOWNLOADER_ARCHIVE") || return 1
	[ "$BHP_DOWNLOADER_ARCHIVE_ID" = \
		"$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256" ]
}

bhp_verify_source()
{
	BHP_DOWNLOADER_SOURCE=$1
	[ -d "$BHP_DOWNLOADER_SOURCE" ] || return 1
	[ ! -L "$BHP_DOWNLOADER_SOURCE" ] || return 1
	BHP_DOWNLOADER_SOURCE_ID=$(
		bhp_buildroot_read_source_id "$BHP_DOWNLOADER_SOURCE" \
			"$PROJECT_ROOT/scripts/build/buildroot_source_id.sh"
	) || return 1
	[ "$BHP_DOWNLOADER_SOURCE_ID" = "$BHP_PINNED_BUILDROOT_SOURCE_ID" ]
}

bhp_write_or_verify_receipt()
{
	receipt_temporary=$(mktemp \
		"$canonical_destination/.bhp-buildroot-receipt.XXXXXX")
	{
		printf 'receipt_version=1\n'
		printf 'buildroot_version=%s\n' "$BHP_PINNED_BUILDROOT_VERSION"
		printf 'archive_sha256=%s\n' "$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256"
		printf 'source_id=%s\n' "$BHP_PINNED_BUILDROOT_SOURCE_ID"
	} >"$receipt_temporary"
	if [ -e "$receipt_path" ] || [ -L "$receipt_path" ]; then
		[ -f "$receipt_path" ] && [ ! -L "$receipt_path" ] || return 1
		cmp -s "$receipt_temporary" "$receipt_path" || return 1
	else
		if ! ln "$receipt_temporary" "$receipt_path" 2>/dev/null; then
			[ -f "$receipt_path" ] && [ ! -L "$receipt_path" ] || return 1
			cmp -s "$receipt_temporary" "$receipt_path" || return 1
		fi
	fi
	rm -- "$receipt_temporary"
	receipt_temporary=
}

if [ -e "$source_directory" ] || [ -L "$source_directory" ]; then
	if ! bhp_verify_source "$source_directory"; then
		bhp_fail BUILDROOT.EXISTING_SOURCE_ID_MISMATCH \
			"Existing source is not the exact pinned archive tree; it was not modified" \
			"$TRACE_ID"
		exit 1
	fi
	if ! bhp_write_or_verify_receipt; then
		bhp_fail BUILDROOT.SOURCE_RECEIPT_CONFLICT \
			"Existing verification receipt is not canonical; it was not replaced" \
			"$TRACE_ID"
		exit 1
	fi
	bhp_log info buildroot_source_already_verified "$TRACE_ID" \
		source_directory "$source_directory" \
		source_id "$BHP_PINNED_BUILDROOT_SOURCE_ID" \
		archive_sha256 "$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256"
	exit 0
fi

if [ -e "$archive_path" ] || [ -L "$archive_path" ]; then
	if ! bhp_verify_archive "$archive_path"; then
		bhp_fail BUILDROOT.EXISTING_ARCHIVE_HASH_MISMATCH \
			"Existing archive is not pinned; it was not replaced" "$TRACE_ID"
		exit 1
	fi
else
	download_temporary=$(mktemp \
		"$canonical_destination/.bhp-buildroot-download.XXXXXX")
	bhp_log info buildroot_download_started "$TRACE_ID" \
		target "$BHP_PINNED_BUILDROOT_URL"
	curl --fail --location --proto '=https' --tlsv1.2 \
		"$BHP_PINNED_BUILDROOT_URL" --output "$download_temporary"
	if ! bhp_verify_archive "$download_temporary"; then
		bhp_fail BUILDROOT.DOWNLOADED_ARCHIVE_HASH_MISMATCH \
			"Downloaded archive hash did not match" "$TRACE_ID"
		exit 1
	fi
	if ! ln "$download_temporary" "$archive_path" 2>/dev/null; then
		if ! bhp_verify_archive "$archive_path"; then
			bhp_fail BUILDROOT.ARCHIVE_PUBLISH_CONFLICT \
				"A conflicting archive appeared during download" "$TRACE_ID"
			exit 1
		fi
	fi
	rm -- "$download_temporary"
	download_temporary=
fi

extraction_stage=$(mktemp -d \
	"$canonical_destination/.bhp-buildroot-extract.XXXXXX")
tar -xJf "$archive_path" -C "$extraction_stage"
extracted_source=$extraction_stage/buildroot-$BHP_PINNED_BUILDROOT_VERSION
top_level_entries=$(find "$extraction_stage" -mindepth 1 -maxdepth 1 -print)
top_level_count=$(printf '%s\n' "$top_level_entries" | sed '/^$/d' | wc -l | tr -d ' ')
if [ "$top_level_count" -ne 1 ] ||
   [ "$top_level_entries" != "$extracted_source" ] ||
   ! bhp_verify_source "$extracted_source"; then
	bhp_fail BUILDROOT.EXTRACTED_SOURCE_ID_MISMATCH \
		"Verified archive did not produce the pinned canonical tree" "$TRACE_ID"
	exit 1
fi

# mkdir reserves the final name without following or replacing a caller path.
# Copy only after the private extraction has passed its full identity check. A
# host failure can leave a partial script-created tree; a later run rejects it
# and leaves it for explicit operator inspection/removal.
if mkdir "$source_directory" 2>/dev/null; then
	if ! cp -a "$extracted_source/." "$source_directory/"; then
		bhp_fail BUILDROOT.SOURCE_PUBLISH_INCOMPLETE \
			"A partial source tree may remain; it was not automatically removed" \
			"$TRACE_ID"
		exit 1
	fi
fi
if ! bhp_verify_source "$source_directory"; then
	bhp_fail BUILDROOT.SOURCE_PUBLISH_CONFLICT \
		"A conflicting source tree appeared during extraction; it was not modified" \
		"$TRACE_ID"
	exit 1
fi
if ! bhp_write_or_verify_receipt; then
	bhp_fail BUILDROOT.SOURCE_RECEIPT_CONFLICT \
		"Verification receipt could not be published without replacement" "$TRACE_ID"
	exit 1
fi
bhp_log info buildroot_download_complete "$TRACE_ID" \
	source_directory "$source_directory" \
	source_id "$BHP_PINNED_BUILDROOT_SOURCE_ID" \
	archive_sha256 "$BHP_PINNED_BUILDROOT_ARCHIVE_SHA256"
