#!/bin/sh
# @canonical application_bundle_manifest -- creates, verifies, and retires
# exact regular-file application bundles without recursive deletion.

BHP_BUNDLE_MANIFEST_FILENAME=MANIFEST.sha256
BHP_BUNDLE_ERROR_CODE=
BHP_BUNDLE_ERROR_DETAIL=

bhp_bundle_set_error()
{
	# These are the documented result fields consumed by sourcing scripts.
	# shellcheck disable=SC2034
	BHP_BUNDLE_ERROR_CODE=$1
	# shellcheck disable=SC2034
	BHP_BUNDLE_ERROR_DETAIL=$2
	return 1
}

bhp_bundle_relative_path_is_valid()
{
	BHP_BUNDLE_RELATIVE_PATH=$1
	case "$BHP_BUNDLE_RELATIVE_PATH" in
		''|/*|./*|../*|*//*|*'/./'*|*'/../'*|*'/.'|*'/..'|*'
'*|*''*|*'	'*) return 1 ;;
	esac
	return 0
}

bhp_bundle_directory_is_valid()
{
	BHP_BUNDLE_DIRECTORY=$1
	case "$BHP_BUNDLE_DIRECTORY" in
		/*) ;;
		*) return 1 ;;
	esac
	[ -d "$BHP_BUNDLE_DIRECTORY" ] && [ ! -L "$BHP_BUNDLE_DIRECTORY" ]
}

bhp_bundle_reject_special_files()
{
	BHP_BUNDLE_DIRECTORY=$1
	BHP_BUNDLE_SPECIAL_PATH=$(
		find "$BHP_BUNDLE_DIRECTORY" -mindepth 1 ! -type d ! -type f -print |
			sed -n '1p'
	) || return 1
	[ -z "$BHP_BUNDLE_SPECIAL_PATH" ] ||
		bhp_bundle_set_error BUNDLE.SPECIAL_FILE_NOT_ALLOWED "$BHP_BUNDLE_SPECIAL_PATH"
}

bhp_bundle_create_manifest()
{
	BHP_BUNDLE_DIRECTORY=$1
	bhp_bundle_directory_is_valid "$BHP_BUNDLE_DIRECTORY" ||
		bhp_bundle_set_error BUNDLE.DIRECTORY_INVALID "$BHP_BUNDLE_DIRECTORY" || return 1
	command -v bhp_sha256_file >/dev/null 2>&1 ||
		bhp_bundle_set_error BUNDLE.HASH_FUNCTION_MISSING bhp_sha256_file || return 1
	bhp_bundle_reject_special_files "$BHP_BUNDLE_DIRECTORY" || return 1

	BHP_BUNDLE_MANIFEST_PATH=$BHP_BUNDLE_DIRECTORY/$BHP_BUNDLE_MANIFEST_FILENAME
	BHP_BUNDLE_FILE_LIST=$BHP_BUNDLE_DIRECTORY.manifest-files.$$
	BHP_BUNDLE_MANIFEST_PENDING=$BHP_BUNDLE_DIRECTORY/.bhp-manifest-pending.$$
	if [ -e "$BHP_BUNDLE_FILE_LIST" ] || [ -L "$BHP_BUNDLE_FILE_LIST" ] ||
	   [ -e "$BHP_BUNDLE_MANIFEST_PENDING" ] || [ -L "$BHP_BUNDLE_MANIFEST_PENDING" ] ||
	   [ -L "$BHP_BUNDLE_MANIFEST_PATH" ]; then
		bhp_bundle_set_error BUNDLE.TEMPORARY_PATH_OCCUPIED "$BHP_BUNDLE_DIRECTORY"
		return 1
	fi

	# The list file is a sibling of the searched directory, not an input.
	# shellcheck disable=SC2094
	find "$BHP_BUNDLE_DIRECTORY" -type f \
		! -path "$BHP_BUNDLE_MANIFEST_PATH" \
		! -path "$BHP_BUNDLE_FILE_LIST" \
		! -path "$BHP_BUNDLE_MANIFEST_PENDING" \
		-print | LC_ALL=C sort >"$BHP_BUNDLE_FILE_LIST"
	if [ ! -s "$BHP_BUNDLE_FILE_LIST" ]; then
		rm -f -- "$BHP_BUNDLE_FILE_LIST"
		bhp_bundle_set_error BUNDLE.EMPTY "$BHP_BUNDLE_DIRECTORY"
		return 1
	fi

	BHP_BUNDLE_CREATE_STATUS=0
	while IFS= read -r BHP_BUNDLE_INSTALLED_FILE; do
		BHP_BUNDLE_RELATIVE_PATH=${BHP_BUNDLE_INSTALLED_FILE#"$BHP_BUNDLE_DIRECTORY"/}
		if ! bhp_bundle_relative_path_is_valid "$BHP_BUNDLE_RELATIVE_PATH" ||
		   [ ! -f "$BHP_BUNDLE_INSTALLED_FILE" ] ||
		   [ -L "$BHP_BUNDLE_INSTALLED_FILE" ]; then
			bhp_bundle_set_error BUNDLE.FILE_PATH_INVALID "$BHP_BUNDLE_RELATIVE_PATH"
			BHP_BUNDLE_CREATE_STATUS=1
			break
		fi
		BHP_BUNDLE_FILE_HASH=$(bhp_sha256_file "$BHP_BUNDLE_INSTALLED_FILE") || {
			bhp_bundle_set_error BUNDLE.HASH_FAILED "$BHP_BUNDLE_RELATIVE_PATH"
			BHP_BUNDLE_CREATE_STATUS=1
			break
		}
		printf '%s  %s\n' "$BHP_BUNDLE_FILE_HASH" "$BHP_BUNDLE_RELATIVE_PATH"
	done <"$BHP_BUNDLE_FILE_LIST" >"$BHP_BUNDLE_MANIFEST_PENDING"
	rm -f -- "$BHP_BUNDLE_FILE_LIST"
	if [ "$BHP_BUNDLE_CREATE_STATUS" -ne 0 ]; then
		rm -f -- "$BHP_BUNDLE_MANIFEST_PENDING"
		return 1
	fi
	mv -- "$BHP_BUNDLE_MANIFEST_PENDING" "$BHP_BUNDLE_MANIFEST_PATH"
	bhp_bundle_verify_exact "$BHP_BUNDLE_DIRECTORY"
}

bhp_bundle_parse_manifest_line()
{
	BHP_BUNDLE_MANIFEST_LINE=$1
	BHP_BUNDLE_EXPECTED_HASH=${BHP_BUNDLE_MANIFEST_LINE%% *}
	BHP_BUNDLE_SEPARATOR_AND_PATH=${BHP_BUNDLE_MANIFEST_LINE#"$BHP_BUNDLE_EXPECTED_HASH"}
	case "$BHP_BUNDLE_SEPARATOR_AND_PATH" in
		'  '*) BHP_BUNDLE_RELATIVE_PATH=${BHP_BUNDLE_SEPARATOR_AND_PATH#'  '} ;;
		*) return 1 ;;
	esac
	case "$BHP_BUNDLE_EXPECTED_HASH" in
		''|*[!0-9A-Fa-f]*) return 1 ;;
	esac
	[ "${#BHP_BUNDLE_EXPECTED_HASH}" -eq 64 ] || return 1
	bhp_bundle_relative_path_is_valid "$BHP_BUNDLE_RELATIVE_PATH"
}

bhp_bundle_verify_exact()
{
	BHP_BUNDLE_DIRECTORY=$1
	bhp_bundle_directory_is_valid "$BHP_BUNDLE_DIRECTORY" ||
		bhp_bundle_set_error BUNDLE.DIRECTORY_INVALID "$BHP_BUNDLE_DIRECTORY" || return 1
	command -v bhp_sha256_file >/dev/null 2>&1 ||
		bhp_bundle_set_error BUNDLE.HASH_FUNCTION_MISSING bhp_sha256_file || return 1
	bhp_bundle_reject_special_files "$BHP_BUNDLE_DIRECTORY" || return 1

	BHP_BUNDLE_MANIFEST_PATH=$BHP_BUNDLE_DIRECTORY/$BHP_BUNDLE_MANIFEST_FILENAME
	if [ ! -s "$BHP_BUNDLE_MANIFEST_PATH" ] || [ -L "$BHP_BUNDLE_MANIFEST_PATH" ]; then
		bhp_bundle_set_error BUNDLE.MANIFEST_MISSING "$BHP_BUNDLE_MANIFEST_PATH"
		return 1
	fi

	BHP_BUNDLE_ACTUAL_FILE_COUNT=$(
		find "$BHP_BUNDLE_DIRECTORY" -type f \
			! -path "$BHP_BUNDLE_MANIFEST_PATH" -print |
			wc -l | tr -d ' '
	) || return 1
	BHP_BUNDLE_MANIFEST_ENTRY_COUNT=0
	BHP_BUNDLE_PREVIOUS_RELATIVE_PATH=
	while IFS= read -r BHP_BUNDLE_MANIFEST_LINE ||
	      [ -n "$BHP_BUNDLE_MANIFEST_LINE" ]; do
		if ! bhp_bundle_parse_manifest_line "$BHP_BUNDLE_MANIFEST_LINE"; then
			bhp_bundle_set_error BUNDLE.MANIFEST_LINE_INVALID "$BHP_BUNDLE_MANIFEST_LINE"
			return 1
		fi
		if [ -n "$BHP_BUNDLE_PREVIOUS_RELATIVE_PATH" ]; then
			BHP_BUNDLE_SORTED_FIRST=$(
				printf '%s\n%s\n' \
					"$BHP_BUNDLE_PREVIOUS_RELATIVE_PATH" \
					"$BHP_BUNDLE_RELATIVE_PATH" |
					LC_ALL=C sort | sed -n '1p'
			)
			if [ "$BHP_BUNDLE_SORTED_FIRST" != "$BHP_BUNDLE_PREVIOUS_RELATIVE_PATH" ] ||
			   [ "$BHP_BUNDLE_RELATIVE_PATH" = "$BHP_BUNDLE_PREVIOUS_RELATIVE_PATH" ]; then
				bhp_bundle_set_error BUNDLE.MANIFEST_ORDER_INVALID "$BHP_BUNDLE_RELATIVE_PATH"
				return 1
			fi
		fi
		BHP_BUNDLE_INSTALLED_FILE=$BHP_BUNDLE_DIRECTORY/$BHP_BUNDLE_RELATIVE_PATH
		if [ ! -f "$BHP_BUNDLE_INSTALLED_FILE" ] || [ -L "$BHP_BUNDLE_INSTALLED_FILE" ]; then
			bhp_bundle_set_error BUNDLE.MANIFEST_FILE_MISSING "$BHP_BUNDLE_RELATIVE_PATH"
			return 1
		fi
		BHP_BUNDLE_ACTUAL_HASH=$(bhp_sha256_file "$BHP_BUNDLE_INSTALLED_FILE") || return 1
		if [ "$BHP_BUNDLE_ACTUAL_HASH" != "$BHP_BUNDLE_EXPECTED_HASH" ]; then
			bhp_bundle_set_error BUNDLE.HASH_MISMATCH "$BHP_BUNDLE_RELATIVE_PATH"
			return 1
		fi
		BHP_BUNDLE_PREVIOUS_RELATIVE_PATH=$BHP_BUNDLE_RELATIVE_PATH
		BHP_BUNDLE_MANIFEST_ENTRY_COUNT=$((BHP_BUNDLE_MANIFEST_ENTRY_COUNT + 1))
	done <"$BHP_BUNDLE_MANIFEST_PATH"

	if [ "$BHP_BUNDLE_MANIFEST_ENTRY_COUNT" -eq 0 ] ||
	   [ "$BHP_BUNDLE_MANIFEST_ENTRY_COUNT" -ne "$BHP_BUNDLE_ACTUAL_FILE_COUNT" ]; then
		bhp_bundle_set_error BUNDLE.FILE_SET_MISMATCH \
			"manifest=$BHP_BUNDLE_MANIFEST_ENTRY_COUNT actual=$BHP_BUNDLE_ACTUAL_FILE_COUNT"
		return 1
	fi
	return 0
}

bhp_bundle_remove_verified()
{
	BHP_BUNDLE_DIRECTORY=$1
	bhp_bundle_verify_exact "$BHP_BUNDLE_DIRECTORY" || return 1
	BHP_BUNDLE_MANIFEST_PATH=$BHP_BUNDLE_DIRECTORY/$BHP_BUNDLE_MANIFEST_FILENAME
	while IFS= read -r BHP_BUNDLE_MANIFEST_LINE ||
	      [ -n "$BHP_BUNDLE_MANIFEST_LINE" ]; do
		bhp_bundle_parse_manifest_line "$BHP_BUNDLE_MANIFEST_LINE" || return 1
		rm -- "$BHP_BUNDLE_DIRECTORY/$BHP_BUNDLE_RELATIVE_PATH" || return 1
	done <"$BHP_BUNDLE_MANIFEST_PATH"
	rm -- "$BHP_BUNDLE_MANIFEST_PATH" || return 1
	find "$BHP_BUNDLE_DIRECTORY" -depth -type d -exec rmdir {} \; 2>/dev/null || true
	[ ! -e "$BHP_BUNDLE_DIRECTORY" ] ||
		bhp_bundle_set_error BUNDLE.RETIRE_NOT_EMPTY "$BHP_BUNDLE_DIRECTORY"
}
