#!/bin/sh
# @canonical application_release_identity -- VERSION + Git commit + dirty content ID.
# Inputs: optional absolute project root (defaults to this repository).
# Side effects: none.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
DEFAULT_PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
PROJECT_ROOT=${1:-$DEFAULT_PROJECT_ROOT}

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ] || [ "$#" -gt 1 ]; then
	printf '%s\n' "Usage: $0 [/absolute/project-root]"
	exit 0
fi
case "$PROJECT_ROOT" in
	/*) ;;
	*)
		printf '%s\n' 'APPLICATION_RELEASE_ID.PATH_NOT_ABSOLUTE' >&2
		exit 1
		;;
esac

version_file=$PROJECT_ROOT/VERSION
if [ ! -f "$version_file" ] || [ -L "$version_file" ]; then
	printf '%s\n' 'APPLICATION_RELEASE_ID.VERSION_INVALID' >&2
	exit 1
fi
project_version=$(sed -n '1p' "$version_file")
case "$project_version" in
	''|*[!A-Za-z0-9._+-]*|[!A-Za-z0-9]*)
		printf '%s\n' 'APPLICATION_RELEASE_ID.VERSION_INVALID' >&2
		exit 1
		;;
esac

revision=$(git -C "$PROJECT_ROOT" rev-parse --verify HEAD 2>/dev/null) || {
	printf '%s\n' 'APPLICATION_RELEASE_ID.GIT_REVISION_UNAVAILABLE' >&2
	exit 1
}
case "$revision" in
	*[!0-9a-f]*|'')
		printf '%s\n' 'APPLICATION_RELEASE_ID.GIT_REVISION_INVALID' >&2
		exit 1
		;;
esac
if [ "${#revision}" -ne 40 ]; then
	printf '%s\n' 'APPLICATION_RELEASE_ID.GIT_REVISION_INVALID' >&2
	exit 1
fi

dirty_suffix=
if [ -n "$(git -C "$PROJECT_ROOT" status --porcelain=v1 --untracked-files=normal)" ]; then
	dirty_content_id=$("$SCRIPT_DIRECTORY/project_content_id.sh" "$PROJECT_ROOT") || {
		printf '%s\n' 'APPLICATION_RELEASE_ID.DIRTY_CONTENT_ID_UNAVAILABLE' >&2
		exit 1
	}
	case "$dirty_content_id" in
		*[!0-9a-f]*|'')
			printf '%s\n' 'APPLICATION_RELEASE_ID.DIRTY_CONTENT_ID_INVALID' >&2
			exit 1
			;;
	esac
	if [ "${#dirty_content_id}" -ne 64 ]; then
		printf '%s\n' 'APPLICATION_RELEASE_ID.DIRTY_CONTENT_ID_INVALID' >&2
		exit 1
	fi
	dirty_suffix=-dirty.$dirty_content_id
fi
release_id=$project_version+g$revision$dirty_suffix
if [ "${#release_id}" -gt 128 ]; then
	printf '%s\n' 'APPLICATION_RELEASE_ID.LENGTH_INVALID' >&2
	exit 1
fi
printf '%s\n' "$release_id"
