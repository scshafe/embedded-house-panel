#!/bin/sh
# Computes the release identity of every Git-visible project source file.
# Inputs: optional absolute project root (defaults to this repository).
# Side effects: none. Ignored build products and .git metadata are excluded.

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
		printf '%s\n' 'PROJECT_CONTENT_ID.PATH_NOT_ABSOLUTE' >&2
		exit 1
		;;
esac
command -v git >/dev/null 2>&1 || {
	printf '%s\n' 'PROJECT_CONTENT_ID.GIT_MISSING' >&2
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	printf '%s\n' 'PROJECT_CONTENT_ID.PYTHON_MISSING' >&2
	exit 1
}
git -C "$PROJECT_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
	printf '%s\n' 'PROJECT_CONTENT_ID.NOT_A_GIT_WORKTREE' >&2
	exit 1
}

python3 - "$PROJECT_ROOT" <<'PY'
import hashlib
import os
import stat
import subprocess
import sys

root = os.path.realpath(sys.argv[1])
listing = subprocess.run(
    [
        "git",
        "-C",
        root,
        "ls-files",
        "-z",
        "--cached",
        "--others",
        "--exclude-standard",
    ],
    check=True,
    stdout=subprocess.PIPE,
).stdout
paths = sorted(path for path in listing.split(b"\0") if path)
if not paths:
    raise SystemExit("PROJECT_CONTENT_ID.NO_SOURCE_FILES")
if len(paths) != len(set(paths)):
    raise SystemExit("PROJECT_CONTENT_ID.DUPLICATE_PATH")

digest = hashlib.sha256()
digest.update(b"beaglebone-house-panel-project-content-v1\0")
for relative_bytes in paths:
    if b"\n" in relative_bytes or b"\r" in relative_bytes:
        raise SystemExit("PROJECT_CONTENT_ID.UNSUPPORTED_PATH")
    relative = os.fsdecode(relative_bytes)
    if os.path.isabs(relative) or relative == ".." or relative.startswith("../"):
        raise SystemExit("PROJECT_CONTENT_ID.PATH_ESCAPE")
    path = os.path.join(root, relative)
    before = os.lstat(path)
    if not stat.S_ISREG(before.st_mode):
        raise SystemExit(f"PROJECT_CONTENT_ID.NON_REGULAR_FILE:{relative}")
    executable = b"x" if before.st_mode & 0o111 else b"-"
    digest.update(str(len(relative_bytes)).encode("ascii") + b":" + relative_bytes)
    digest.update(b"\0" + executable + b"\0" + str(before.st_size).encode("ascii") + b":")
    with open(path, "rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    after = os.lstat(path)
    observed_before = (
        before.st_dev,
        before.st_ino,
        before.st_mode,
        before.st_size,
        before.st_mtime_ns,
    )
    observed_after = (
        after.st_dev,
        after.st_ino,
        after.st_mode,
        after.st_size,
        after.st_mtime_ns,
    )
    if observed_before != observed_after:
        raise SystemExit(f"PROJECT_CONTENT_ID.SOURCE_CHANGED_DURING_READ:{relative}")
    digest.update(b"\0")

print(digest.hexdigest())
PY
