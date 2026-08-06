#!/bin/sh
# Computes a canonical identity for a complete Buildroot source tree.
# Inputs: one absolute source directory. Side effects: none.

set -eu

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ] || [ "$#" -ne 1 ]; then
	printf '%s\n' "Usage: $0 /absolute/buildroot-source"
	exit 0
fi
source_directory=$1
case "$source_directory" in
	/*) ;;
	*)
		printf '%s\n' 'BUILDROOT_SOURCE_ID.PATH_NOT_ABSOLUTE' >&2
		exit 1
		;;
esac
[ -d "$source_directory" ] && [ ! -L "$source_directory" ] || {
	printf '%s\n' 'BUILDROOT_SOURCE_ID.DIRECTORY_INVALID' >&2
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	printf '%s\n' 'BUILDROOT_SOURCE_ID.PYTHON_MISSING' >&2
	exit 1
}

python3 - "$source_directory" <<'PY'
import hashlib
import os
import stat
import sys

root = os.path.realpath(os.fsencode(sys.argv[1]))
digest = hashlib.sha256()
digest.update(b"beaglebone-house-panel-buildroot-source-v1\0")


def framed(value: bytes) -> bytes:
    return str(len(value)).encode("ascii") + b":" + value


def visit(directory: bytes, relative_parent: bytes) -> None:
    before_directory = os.lstat(directory)
    entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
    for entry in entries:
        name = entry.name
        if b"\n" in name or b"\r" in name or name in (b".", b".."):
            raise SystemExit("BUILDROOT_SOURCE_ID.UNSUPPORTED_PATH")
        relative = name if not relative_parent else relative_parent + b"/" + name
        path = os.path.join(directory, name)
        before = os.lstat(path)
        if stat.S_ISDIR(before.st_mode):
            digest.update(b"D" + framed(relative) + b"\0")
            visit(path, relative)
        elif stat.S_ISREG(before.st_mode):
            executable = b"x" if before.st_mode & 0o111 else b"-"
            digest.update(
                b"F"
                + framed(relative)
                + b"\0"
                + executable
                + b"\0"
                + str(before.st_size).encode("ascii")
                + b":"
            )
            with open(path, "rb") as source:
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    digest.update(chunk)
            digest.update(b"\0")
        elif stat.S_ISLNK(before.st_mode):
            target = os.readlink(path)
            digest.update(b"L" + framed(relative) + b"\0" + framed(target) + b"\0")
        else:
            display = os.fsdecode(relative)
            raise SystemExit(f"BUILDROOT_SOURCE_ID.SPECIAL_FILE:{display}")
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
            display = os.fsdecode(relative)
            raise SystemExit(
                f"BUILDROOT_SOURCE_ID.SOURCE_CHANGED_DURING_READ:{display}"
            )
    after_directory = os.lstat(directory)
    directory_before = (
        before_directory.st_dev,
        before_directory.st_ino,
        before_directory.st_mode,
        before_directory.st_mtime_ns,
    )
    directory_after = (
        after_directory.st_dev,
        after_directory.st_ino,
        after_directory.st_mode,
        after_directory.st_mtime_ns,
    )
    if directory_before != directory_after:
        display = os.fsdecode(relative_parent) or "."
        raise SystemExit(f"BUILDROOT_SOURCE_ID.DIRECTORY_CHANGED_DURING_READ:{display}")


visit(root, b"")
print(digest.hexdigest())
PY
