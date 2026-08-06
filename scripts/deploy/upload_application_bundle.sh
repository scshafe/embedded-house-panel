#!/bin/sh
# Uploads a built application directory to an unprivileged staging path.
# Inputs: BHP_BOARD_SSH_TARGET, BHP_BOARD_UPLOAD_DIR, optional --bundle DIRECTORY.
# Side effects: none with --dry-run (default); --execute creates/updates the remote staging directory.
# Idempotent: rsync converges the staging copy; it never installs or removes unrelated paths.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
# shellcheck source=application_bundle_manifest.sh
. "$PROJECT_ROOT/scripts/deploy/application_bundle_manifest.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.upload
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

mode=dry-run
bundle_directory=$PROJECT_ROOT/.build/buildroot-armv7-release/stage

usage()
{
  printf '%s\n' "Usage: $0 [--dry-run|--execute] [--bundle /absolute/build-directory]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run) mode=dry-run; shift ;;
    --execute) mode=execute; shift ;;
    --bundle)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      bundle_directory=$2
      shift 2
      ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

: "${BHP_BOARD_SSH_TARGET:?BHP_BOARD_SSH_TARGET is required}"
: "${BHP_BOARD_UPLOAD_DIR:?BHP_BOARD_UPLOAD_DIR is required}"
case "$BHP_BOARD_SSH_TARGET" in
  *[!A-Za-z0-9_.@:-]*|'') bhp_fail DEPLOY.SSH_TARGET_INVALID "$BHP_BOARD_SSH_TARGET" "$TRACE_ID"; exit 1 ;;
esac
case "$BHP_BOARD_UPLOAD_DIR" in
  *[!A-Za-z0-9_./-]*|'') bhp_fail DEPLOY.REMOTE_PATH_INVALID "$BHP_BOARD_UPLOAD_DIR" "$TRACE_ID"; exit 1 ;;
esac
case "$BHP_BOARD_UPLOAD_DIR/" in
  *'/../'*|*'/./'*|*'//'*) bhp_fail DEPLOY.REMOTE_PATH_INVALID "$BHP_BOARD_UPLOAD_DIR" "$TRACE_ID"; exit 1 ;;
esac
case "$BHP_BOARD_UPLOAD_DIR" in
  /home/*/*|/tmp/*) ;;
  *) bhp_fail DEPLOY.REMOTE_PATH_UNSAFE "$BHP_BOARD_UPLOAD_DIR" "$TRACE_ID"; exit 1 ;;
esac
case "$bundle_directory" in
  /*) ;;
  *) bhp_fail DEPLOY.BUNDLE_PATH_NOT_ABSOLUTE "$bundle_directory" "$TRACE_ID"; exit 1 ;;
esac
case "$bundle_directory" in
  /|/Users|/home|/root|/tmp) bhp_fail DEPLOY.BUNDLE_PATH_UNSAFE "$bundle_directory" "$TRACE_ID"; exit 1 ;;
esac
test -d "$bundle_directory" || {
  bhp_fail DEPLOY.BUNDLE_MISSING "$bundle_directory" "$TRACE_ID"
  exit 1
}
if ! bhp_bundle_verify_exact "$bundle_directory"; then
  bhp_fail "$BHP_BUNDLE_ERROR_CODE" "$BHP_BUNDLE_ERROR_DETAIL" "$TRACE_ID"
  exit 1
fi
manifest_path=$bundle_directory/$BHP_BUNDLE_MANIFEST_FILENAME
bundle_id=$(bhp_sha256_file "$manifest_path") || {
  bhp_fail DEPLOY.MANIFEST_ID_FAILED "$manifest_path" "$TRACE_ID"
  exit 1
}
remote_bundle_directory=$BHP_BOARD_UPLOAD_DIR/bundles/$bundle_id

if [ "$mode" = dry-run ]; then
  bhp_log info upload_dry_run "$TRACE_ID" \
    bundle "$bundle_directory" \
    bundle_id "$bundle_id" \
    target "$BHP_BOARD_SSH_TARGET" \
    destination "$remote_bundle_directory"
  sed -n '1,240p' "$manifest_path"
  exit 0
fi

command -v ssh >/dev/null 2>&1 || {
  bhp_fail DEPLOY.SSH_MISSING "ssh is required for --execute" "$TRACE_ID"
  exit 1
}
command -v rsync >/dev/null 2>&1 || {
  bhp_fail DEPLOY.RSYNC_MISSING "rsync is required for --execute" "$TRACE_ID"
  exit 1
}
ssh "$BHP_BOARD_SSH_TARGET" mkdir -p -- "$remote_bundle_directory"
# The deletion scope is a content-addressed leaf created only for this exact
# manifest. Older bundles and every path outside that leaf remain untouched.
rsync --archive --checksum --delete-delay --human-readable \
  "$bundle_directory/" "$BHP_BOARD_SSH_TARGET:$remote_bundle_directory/"
bhp_log info upload_complete "$TRACE_ID" \
  bundle_id "$bundle_id" \
  target "$BHP_BOARD_SSH_TARGET" \
  destination "$remote_bundle_directory"
