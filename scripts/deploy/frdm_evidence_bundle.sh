#!/bin/sh
# Packs, pushes, and fetches the self-contained FRDM gate-evidence tree.
# Usage:
#   frdm_evidence_bundle.sh pack
#       writes .build/frdm-evidence/frdm-evidence-bundle.tar (+ .sha256)
#   frdm_evidence_bundle.sh push  [--dry-run|--execute]
#       streams the bundle over SSH into $BHP_FRDM_EVIDENCE_REMOTE_DIR
#   frdm_evidence_bundle.sh fetch [--dry-run|--execute]
#       copies <remote>/hardware_tests/results/frdm back into the ignored
#       hardware_tests/results/frdm of this checkout (never overwrites a file)
# Inputs: BHP_BOARD_SSH_TARGET (push/fetch); optional BHP_FRDM_EVIDENCE_REMOTE_DIR
#   (default /root/bhp-evidence). Works with any target that has ssh + tar
#   (the NXP vendor image or the project bring-up image); needs no rsync.
# Output: one JSON line on stdout describing the plan or the result.
# Side effects: pack writes only .build/frdm-evidence; push/fetch change nothing
#   without --execute (the default is --dry-run). Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.frdm_evidence_bundle
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

usage()
{
  printf '%s\n' "Usage: $0 pack | push [--dry-run|--execute] | fetch [--dry-run|--execute]"
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
command_name=$1
shift
mode=dry-run
while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run) mode=dry-run; shift ;;
    --execute) mode=execute; shift ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
case "$command_name" in pack|push|fetch) ;; --help|-h) usage; exit 0 ;; *) usage >&2; exit 2 ;; esac

bundle_directory=$PROJECT_ROOT/.build/frdm-evidence
bundle=$bundle_directory/frdm-evidence-bundle.tar
remote_directory=${BHP_FRDM_EVIDENCE_REMOTE_DIR:-/root/bhp-evidence}
case "$remote_directory" in
  /*) ;;
  *) bhp_fail FRDM_EVIDENCE_BUNDLE.REMOTE_DIR_INVALID "$remote_directory must be absolute" "$TRACE_ID"; exit 1 ;;
esac
case "$remote_directory" in
  *[!A-Za-z0-9_./-]*|/|/root|/etc*|/usr*|/bin*|/sbin*|/lib*|/boot*)
    bhp_fail FRDM_EVIDENCE_BUNDLE.REMOTE_DIR_UNSAFE "$remote_directory" "$TRACE_ID"
    exit 1
    ;;
esac

pack_bundle()
{
  mkdir -p "$bundle_directory"
  temporary_bundle=$bundle.tmp.$$
  (
    cd "$PROJECT_ROOT"
    # Only the evidence scripts, their loaders, the schemas, and the profiles.
    tar -cf "$temporary_bundle" \
      hardware_tests/frdm hardware_tests/schemas hardware_tests/README.md \
      scripts/structured_logging/structured_log.sh scripts/boards/board_profile.sh \
      scripts/file_integrity/sha256_file.sh config/boards
  )
  mv "$temporary_bundle" "$bundle"
  bundle_sha256=$(bhp_sha256_file "$bundle")
  printf '%s  %s\n' "$bundle_sha256" "${bundle##*/}" >"$bundle.sha256"
}

require_target()
{
  case "${BHP_BOARD_SSH_TARGET:-}" in
    ''|*placeholder*|*[!A-Za-z0-9_.@:-]*)
      bhp_fail FRDM_EVIDENCE_BUNDLE.SSH_TARGET_INVALID \
        "set BHP_BOARD_SSH_TARGET to the board's user@address" "$TRACE_ID"
      exit 1
      ;;
  esac
}

case "$command_name" in
  pack)
    pack_bundle
    printf '{"status":"packed","bundle":"%s","sha256":"%s"}\n' \
      "$(bhp_json_escape "$bundle")" "$bundle_sha256"
    ;;
  push)
    require_target
    pack_bundle
    if [ "$mode" = dry-run ]; then
      printf '{"status":"dry_run","action":"push","target":"%s","remote_directory":"%s","bundle_sha256":"%s","command":"ssh %s mkdir -p %s && tar -xf - -C %s < %s"}\n' \
        "$(bhp_json_escape "$BHP_BOARD_SSH_TARGET")" "$remote_directory" "$bundle_sha256" \
        "$(bhp_json_escape "$BHP_BOARD_SSH_TARGET")" "$remote_directory" "$remote_directory" \
        "$(bhp_json_escape "$bundle")"
      exit 0
    fi
    # The remote directory is expanded locally on purpose; its charset is validated above.
    # shellcheck disable=SC2029
    ssh "$BHP_BOARD_SSH_TARGET" "mkdir -p '$remote_directory' && tar -xf - -C '$remote_directory'" <"$bundle"
    bhp_log info frdm_evidence_bundle_pushed "$TRACE_ID" target "$BHP_BOARD_SSH_TARGET" \
      remote_directory "$remote_directory" bundle_sha256 "$bundle_sha256"
    ;;
  fetch)
    require_target
    local_results=$PROJECT_ROOT/hardware_tests/results
    if [ "$mode" = dry-run ]; then
      printf '{"status":"dry_run","action":"fetch","target":"%s","remote":"%s/hardware_tests/results/frdm","local":"%s/frdm"}\n' \
        "$(bhp_json_escape "$BHP_BOARD_SSH_TARGET")" "$remote_directory" \
        "$(bhp_json_escape "$local_results")"
      exit 0
    fi
    mkdir -p "$local_results/frdm"
    staging=$(mktemp -d "${TMPDIR:-/tmp}/bhp-frdm-fetch.XXXXXX")
    # shellcheck disable=SC2029
    ssh "$BHP_BOARD_SSH_TARGET" "tar -cf - -C '$remote_directory/hardware_tests/results' frdm" |
      tar -xf - -C "$staging"
    # An existing local record is never overwritten.
    copied=0
    skipped=0
    fetched_list=$staging.list
    (cd "$staging" && find frdm -type f) >"$fetched_list"
    while IFS= read -r relative_path; do
      if [ -e "$local_results/$relative_path" ]; then
        skipped=$((skipped + 1))
        continue
      fi
      mkdir -p "$local_results/$(dirname -- "$relative_path")"
      cp -p "$staging/$relative_path" "$local_results/$relative_path"
      copied=$((copied + 1))
    done <"$fetched_list"
    rm -f "$fetched_list"
    find "$staging" -type f -exec rm -f {} +
    find "$staging" -depth -type d -exec rmdir {} + 2>/dev/null || true
    bhp_log info frdm_evidence_files_copied "$TRACE_ID" copied "$copied" skipped_existing "$skipped"
    record_count=$(find "$local_results/frdm" -name '*.json' -type f | wc -l | tr -d ' ')
    bhp_log info frdm_evidence_fetched "$TRACE_ID" target "$BHP_BOARD_SSH_TARGET" \
      local_directory "$local_results/frdm" json_records "$record_count"
    ;;
esac
