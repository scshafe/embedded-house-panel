#!/bin/sh
# @canonical buildroot_release_contract -- shared Buildroot source identity checks.
# shellcheck disable=SC2034
# Public constants are consumed by scripts that source this contract library.

BHP_PINNED_BUILDROOT_VERSION=2026.05.1
BHP_PINNED_BUILDROOT_ARCHIVE=buildroot-2026.05.1.tar.xz
BHP_PINNED_BUILDROOT_URL=https://buildroot.org/downloads/buildroot-2026.05.1.tar.xz
BHP_PINNED_BUILDROOT_ARCHIVE_SHA256=ae7f706f087b9ae9083a10a587368dfbf53103c28bf81c2d690198dc4090cb58
# Canonical v1 tree identity of the verified archive after extraction. The
# algorithm includes every relative path/type, regular-file executable bit and
# content, empty directory, and symlink target; it ignores archive metadata.
BHP_PINNED_BUILDROOT_SOURCE_ID=c212bf677ec9e691e7157615a6460a1205e37c8942c188f789e30fe835514438

bhp_buildroot_canonical_existing_directory()
{
	BHP_BUILDROOT_CONTRACT_DIRECTORY=$1
	[ -d "$BHP_BUILDROOT_CONTRACT_DIRECTORY" ] || return 1
	(
		CDPATH='' cd -P -- "$BHP_BUILDROOT_CONTRACT_DIRECTORY" && pwd -P
	)
}

bhp_buildroot_canonical_existing_file()
{
	BHP_BUILDROOT_CONTRACT_FILE=$1
	[ -f "$BHP_BUILDROOT_CONTRACT_FILE" ] || return 1
	[ ! -L "$BHP_BUILDROOT_CONTRACT_FILE" ] || return 1
	BHP_BUILDROOT_CONTRACT_FILE_PARENT=$(dirname -- \
		"$BHP_BUILDROOT_CONTRACT_FILE")
	BHP_BUILDROOT_CONTRACT_FILE_LEAF=$(basename -- \
		"$BHP_BUILDROOT_CONTRACT_FILE")
	BHP_BUILDROOT_CONTRACT_FILE_PARENT=$(
		bhp_buildroot_canonical_existing_directory \
			"$BHP_BUILDROOT_CONTRACT_FILE_PARENT"
	) || return 1
	if [ "$BHP_BUILDROOT_CONTRACT_FILE_PARENT" = / ]; then
		printf '/%s\n' "$BHP_BUILDROOT_CONTRACT_FILE_LEAF"
	else
		printf '%s/%s\n' "$BHP_BUILDROOT_CONTRACT_FILE_PARENT" \
			"$BHP_BUILDROOT_CONTRACT_FILE_LEAF"
	fi
}

# Resolve an output path without creating it. Its parent must already exist so
# symlinks are resolved before a caller is allowed to mutate the destination.
bhp_buildroot_canonical_output_path()
{
	BHP_BUILDROOT_CONTRACT_OUTPUT=$1
	case "$BHP_BUILDROOT_CONTRACT_OUTPUT" in
		/*) ;;
		*) return 1 ;;
	esac
	if [ -e "$BHP_BUILDROOT_CONTRACT_OUTPUT" ] ||
	   [ -L "$BHP_BUILDROOT_CONTRACT_OUTPUT" ]; then
		[ -d "$BHP_BUILDROOT_CONTRACT_OUTPUT" ] || return 1
		[ ! -L "$BHP_BUILDROOT_CONTRACT_OUTPUT" ] || return 1
		bhp_buildroot_canonical_existing_directory \
			"$BHP_BUILDROOT_CONTRACT_OUTPUT"
		return
	fi
	BHP_BUILDROOT_CONTRACT_PARENT=$(dirname -- "$BHP_BUILDROOT_CONTRACT_OUTPUT")
	BHP_BUILDROOT_CONTRACT_LEAF=$(basename -- "$BHP_BUILDROOT_CONTRACT_OUTPUT")
	case "$BHP_BUILDROOT_CONTRACT_LEAF" in
		''|.|..) return 1 ;;
	esac
	BHP_BUILDROOT_CONTRACT_PARENT=$(
		bhp_buildroot_canonical_existing_directory \
			"$BHP_BUILDROOT_CONTRACT_PARENT"
	) || return 1
	if [ "$BHP_BUILDROOT_CONTRACT_PARENT" = / ]; then
		printf '/%s\n' "$BHP_BUILDROOT_CONTRACT_LEAF"
	else
		printf '%s/%s\n' "$BHP_BUILDROOT_CONTRACT_PARENT" \
			"$BHP_BUILDROOT_CONTRACT_LEAF"
	fi
}

bhp_buildroot_paths_overlap()
{
	BHP_BUILDROOT_CONTRACT_LEFT=$1
	BHP_BUILDROOT_CONTRACT_RIGHT=$2
	# Appending a separator turns the filesystem root into "//", so handle
	# that ancestor explicitly before performing the component-boundary tests.
	if [ "$BHP_BUILDROOT_CONTRACT_LEFT" = / ] || \
	   [ "$BHP_BUILDROOT_CONTRACT_RIGHT" = / ]; then
		return 0
	fi
	case "$BHP_BUILDROOT_CONTRACT_LEFT/" in
		"$BHP_BUILDROOT_CONTRACT_RIGHT/"*) return 0 ;;
	esac
	case "$BHP_BUILDROOT_CONTRACT_RIGHT/" in
		"$BHP_BUILDROOT_CONTRACT_LEFT/"*) return 0 ;;
	esac
	return 1
}

# The Buildroot output can contain neither source tree. Neither source tree can
# contain the output. Apart from stale-source risks, Buildroot clean targets can
# recursively remove their output, so overlap is a destructive boundary.
bhp_buildroot_output_is_separate()
{
	BHP_BUILDROOT_CONTRACT_SOURCE=$(
		bhp_buildroot_canonical_existing_directory "$1"
	) || return 1
	BHP_BUILDROOT_CONTRACT_OUTPUT=$(
		bhp_buildroot_canonical_output_path "$2"
	) || return 1
	BHP_BUILDROOT_CONTRACT_PROJECT=$(
		bhp_buildroot_canonical_existing_directory "$3"
	) || return 1
	! bhp_buildroot_paths_overlap "$BHP_BUILDROOT_CONTRACT_OUTPUT" \
		"$BHP_BUILDROOT_CONTRACT_SOURCE" || return 1
	! bhp_buildroot_paths_overlap "$BHP_BUILDROOT_CONTRACT_OUTPUT" \
		"$BHP_BUILDROOT_CONTRACT_PROJECT" || return 1
}

# Bring-up images accept exactly one caller-supplied Ed25519 public key. This
# validates authorization material, not a private credential. Keeping the
# format intentionally narrow makes malformed or multi-key input fail closed.
bhp_bringup_authorized_key_is_valid()
{
	BHP_BUILDROOT_CONTRACT_KEY_FILE=$1
	case "$BHP_BUILDROOT_CONTRACT_KEY_FILE" in
		/*) ;;
		*) return 1 ;;
	esac
	[ -f "$BHP_BUILDROOT_CONTRACT_KEY_FILE" ] || return 1
	[ ! -L "$BHP_BUILDROOT_CONTRACT_KEY_FILE" ] || return 1
	[ -s "$BHP_BUILDROOT_CONTRACT_KEY_FILE" ] || return 1
	BHP_BUILDROOT_CONTRACT_KEY_SIZE=$(wc -c \
		<"$BHP_BUILDROOT_CONTRACT_KEY_FILE" | tr -d ' ')
	case "$BHP_BUILDROOT_CONTRACT_KEY_SIZE" in
		''|*[!0-9]*) return 1 ;;
	esac
	[ "$BHP_BUILDROOT_CONTRACT_KEY_SIZE" -le 1024 ] || return 1
	LC_ALL=C awk '
		NR != 1 { invalid = 1 }
		$1 != "ssh-ed25519" { invalid = 1 }
		$2 !~ /^[A-Za-z0-9+\/=]+$/ { invalid = 1 }
		NF < 2 { invalid = 1 }
		END { exit invalid || NR != 1 }
	' "$BHP_BUILDROOT_CONTRACT_KEY_FILE" || return 1
	command -v ssh-keygen >/dev/null 2>&1 || return 1
	ssh-keygen -l -f "$BHP_BUILDROOT_CONTRACT_KEY_FILE" >/dev/null 2>&1
}

bhp_buildroot_read_source_version()
{
	BHP_BUILDROOT_CONTRACT_SOURCE_DIRECTORY=$1
	[ -f "$BHP_BUILDROOT_CONTRACT_SOURCE_DIRECTORY/Makefile" ] || return 1
	BHP_BUILDROOT_CONTRACT_VERSION=$(
		sed -n 's/^export BR2_VERSION := //p' \
			"$BHP_BUILDROOT_CONTRACT_SOURCE_DIRECTORY/Makefile"
	) || return 1
	case "$BHP_BUILDROOT_CONTRACT_VERSION" in
		''|*'
'*) return 1 ;;
	esac
	printf '%s\n' "$BHP_BUILDROOT_CONTRACT_VERSION"
}

bhp_buildroot_source_is_pinned()
{
	BHP_BUILDROOT_CONTRACT_ACTUAL_VERSION=$(
		bhp_buildroot_read_source_version "$1"
	) || return 1
	[ "$BHP_BUILDROOT_CONTRACT_ACTUAL_VERSION" = "$BHP_PINNED_BUILDROOT_VERSION" ]
}

bhp_buildroot_read_source_id()
{
	BHP_BUILDROOT_CONTRACT_SOURCE_DIRECTORY=$1
	BHP_BUILDROOT_CONTRACT_ID_SCRIPT=$2
	[ -x "$BHP_BUILDROOT_CONTRACT_ID_SCRIPT" ] || return 1
	BHP_BUILDROOT_CONTRACT_SOURCE_ID=$(
		"$BHP_BUILDROOT_CONTRACT_ID_SCRIPT" \
			"$BHP_BUILDROOT_CONTRACT_SOURCE_DIRECTORY"
	) || return 1
	case "$BHP_BUILDROOT_CONTRACT_SOURCE_ID" in
		''|*[!0-9a-f]*) return 1 ;;
	esac
	[ "${#BHP_BUILDROOT_CONTRACT_SOURCE_ID}" -eq 64 ] || return 1
	printf '%s\n' "$BHP_BUILDROOT_CONTRACT_SOURCE_ID"
}

bhp_buildroot_source_is_verified()
{
	bhp_buildroot_source_is_pinned "$1" || return 1
	BHP_BUILDROOT_CONTRACT_SOURCE_ID=$(
		bhp_buildroot_read_source_id "$1" "$2"
	) || return 1
	[ "$BHP_BUILDROOT_CONTRACT_SOURCE_ID" = \
		"$BHP_PINNED_BUILDROOT_SOURCE_ID" ]
}
