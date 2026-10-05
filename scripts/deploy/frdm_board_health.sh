#!/bin/sh
# Read-only FRDM health check from the development host: the hardware gate
# (dev.toml [hardware] verify, `make verify-board`).
# Usage:
#   frdm_board_health.sh [--output-dir DIR]
# Inputs: BHP_BOARD_SSH_TARGET (user@address, required); optional
#   BHP_BOARD_CONSOLE (the serial console device on this host, checked for
#   presence and permission only, never opened) and BHP_FRDM_FACTORY_BACKUP_DIR
#   (the ARRIVAL-05 backup: SHA256SUMS, and frdm-factory-emmc.img.zst for the
#   eMMC head regions). Export them from config/device.env first.
# Output: raw captures and one JSON record under
#   hardware_tests/results/frdm/health-check/<UTC>/ (ignored); prints the record
#   path. Exit 0 when the board is reachable and nothing compared differs.
# Side effects: none on the board. Every board command reads (cat, uname, dd
#   from a block device into sha256sum, systemctl --failed, sysfs); nothing is
#   written there, nothing is installed, the console is never opened.
#   Idempotent: yes (each run writes a new timestamped directory).

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../file_integrity/sha256_file.sh
. "$PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.frdm_board_health
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

usage()
{
  printf '%s\n' "Usage: $0 [--output-dir DIR]  (env: BHP_BOARD_SSH_TARGET, BHP_BOARD_CONSOLE, BHP_FRDM_FACTORY_BACKUP_DIR)"
}

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_directory=$PROJECT_ROOT/hardware_tests/results/frdm/health-check/$timestamp
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; output_directory=$2; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

case "${BHP_BOARD_SSH_TARGET:-}" in
  ''|*placeholder*|*[!A-Za-z0-9_.@:-]*)
    bhp_fail FRDM_BOARD_HEALTH.SSH_TARGET_INVALID \
      "set BHP_BOARD_SSH_TARGET to the board's user@address (set -a; . ./config/device.env; set +a)" "$TRACE_ID"
    exit 1
    ;;
esac
backup_directory=${BHP_FRDM_FACTORY_BACKUP_DIR:-}
console_device=${BHP_BOARD_CONSOLE:-}

mkdir -p "$output_directory"
record=$output_directory/frdm_board_health.json
checks=
failures=0
unobserved=0

add_check()
{
  # name status detail
  case "$2" in fail) failures=$((failures + 1)) ;; unobserved) unobserved=$((unobserved + 1)) ;; esac
  entry=$(printf '{"name":"%s","status":"%s","detail":"%s"}' \
    "$(bhp_json_escape "$1")" "$2" "$(bhp_json_escape "$3")")
  checks=${checks:+$checks,}$entry
}

board()
{
  ssh -o BatchMode=yes -o ConnectTimeout=10 "$BHP_BOARD_SSH_TARGET" "$1"
}

# 1. Reachability and identity (read-only). The command expands on the board.
# shellcheck disable=SC2016
if board 'echo "os=$(. /etc/os-release && echo "$PRETTY_NAME")"; echo "kernel=$(uname -r)";
  echo "model=$(tr -d "\000" </proc/device-tree/model)"; echo "uptime_seconds=$(cut -d" " -f1 /proc/uptime)";
  echo "cmdline=$(cat /proc/cmdline)"; echo "failed_units=$(systemctl --failed --no-legend --plain 2>/dev/null |
  awk "{print \$1}" | tr "\n" " " | sed "s/ \$//")"; t=0; for z in /sys/class/thermal/thermal_zone*/temp; do v=$(cat "$z");
  [ "$v" -gt "$t" ] && t=$v; done; echo "thermal_max_millicelsius=$t";
  for p in mmcblk2p1 mmcblk2p2; do echo "${p}_start=$(cat /sys/block/mmcblk2/$p/start)";
  echo "${p}_sectors=$(cat /sys/block/mmcblk2/$p/size)"; done' >"$output_directory/system.txt" 2>&1; then
  add_check ssh_reachable pass "$BHP_BOARD_SSH_TARGET"
else
  add_check ssh_reachable fail "ssh $BHP_BOARD_SSH_TARGET failed; see system.txt"
fi
value()
{
  sed -n "s/^$1=//p" "$output_directory/system.txt" | head -n 1
}
model=$(value model)
case "$model" in
  *FRDM-IMX8MPLUS*) add_check board_model pass "$model" ;;
  '') add_check board_model unobserved "no model read" ;;
  *) add_check board_model fail "$model is not an FRDM-IMX8MPLUS" ;;
esac

# 2. The console on this host: present and openable by this user (never opened).
if [ -z "$console_device" ]; then
  add_check console_device unobserved "BHP_BOARD_CONSOLE not set"
elif [ ! -c "$console_device" ]; then
  add_check console_device fail "$console_device is not attached"
elif [ -r "$console_device" ] && [ -w "$console_device" ]; then
  add_check console_device pass "$console_device present; this user may open it"
else
  add_check console_device fail "$console_device present; this user may not open it"
fi

# 3. Factory storage against the ARRIVAL-05 backup. p2 (the root file system)
#    changes in normal operation and is not compared.
p1_start=$(value mmcblk2p1_start)
p1_sectors=$(value mmcblk2p1_sectors)
case "$p1_start$p1_sectors" in ''|*[!0-9]*) p1_start=; p1_sectors= ;; esac
if [ -n "$p1_start" ]; then
  board "for d in mmcblk2boot0 mmcblk2boot1 mtdblock0; do echo \"\$d=\$(dd if=/dev/\$d bs=1M 2>/dev/null | sha256sum | cut -d' ' -f1)\"; done
    echo \"mbr=\$(dd if=/dev/mmcblk2 bs=512 count=1 2>/dev/null | sha256sum | cut -d' ' -f1)\"
    echo \"bootloader_region=\$(dd if=/dev/mmcblk2 bs=512 count=$p1_start 2>/dev/null | sha256sum | cut -d' ' -f1)\"
    echo \"boot_partition=\$(dd if=/dev/mmcblk2p1 bs=1M 2>/dev/null | sha256sum | cut -d' ' -f1)\"" \
    >"$output_directory/storage-board.txt" 2>&1 || true
fi
board_hash()
{
  sed -n "s/^$1=//p" "$output_directory/storage-board.txt" 2>/dev/null | head -n 1
}
compare()
{
  # name board_hash backup_hash
  if [ -z "$2" ] || [ -z "$3" ]; then
    add_check "$1" unobserved "board ${2:-unread}; backup ${3:-unavailable}"
  elif [ "$2" = "$3" ]; then
    add_check "$1" pass "$2"
  else
    add_check "$1" fail "board $2 differs from the factory backup $3"
  fi
}
backup_sums=
if [ -n "$backup_directory" ] && [ -f "$backup_directory/SHA256SUMS" ]; then
  backup_sums=$backup_directory/SHA256SUMS
fi
backup_sum()
{
  [ -n "$backup_sums" ] || return 0
  awk -v f="$1" '$2 == f {print $1; exit}' "$backup_sums"
}
compare storage_boot0 "$(board_hash mmcblk2boot0)" "$(backup_sum mmcblk2boot0.img)"
compare storage_boot1 "$(board_hash mmcblk2boot1)" "$(backup_sum mmcblk2boot1.img)"
compare storage_qspi "$(board_hash mtdblock0)" "$(backup_sum mtdblock0.img)"
image=${backup_directory:+$backup_directory/frdm-factory-emmc.img.zst}
if [ -n "$image" ] && [ -f "$image" ] && [ -n "$p1_start" ] && command -v zstd >/dev/null 2>&1; then
  head_file=$(mktemp "${TMPDIR:-/tmp}/bhp-frdm-head.XXXXXX")
  zstd -dc "$image" 2>/dev/null | head -c $(((p1_start + p1_sectors) * 512)) >"$head_file" || true
  mbr_backup=$(head -c 512 "$head_file" | sha256sum | cut -d' ' -f1)
  region_backup=$(head -c $((p1_start * 512)) "$head_file" | sha256sum | cut -d' ' -f1)
  p1_backup=$(tail -c $((p1_sectors * 512)) "$head_file" | sha256sum | cut -d' ' -f1)
  rm -f "$head_file"
  printf 'mbr=%s\nbootloader_region=%s\nboot_partition=%s\n' "$mbr_backup" "$region_backup" "$p1_backup" \
    >"$output_directory/storage-backup.txt"
else
  mbr_backup=; region_backup=; p1_backup=
fi
compare storage_mbr_partition_table "$(board_hash mbr)" "$mbr_backup"
compare storage_bootloader_region "$(board_hash bootloader_region)" "$region_backup"
compare storage_boot_partition "$(board_hash boot_partition)" "$p1_backup"

if [ "$failures" -gt 0 ]; then result=mismatch; elif [ "$unobserved" -gt 0 ]; then result=incomplete; else result=complete; fi
(cd "$output_directory" && for f in *.txt; do printf '%s  %s\n' "$(bhp_sha256_file "$f")" "$f"; done) \
  >"$output_directory/SHA256SUMS"
printf '{"schema":"frdm_board_health.v1","captured_utc":"%s","target":"%s","result":"%s","os":"%s","kernel":"%s","uptime_seconds":"%s","failed_units":"%s","thermal_max_millicelsius":"%s","checks":[%s]}\n' \
  "$timestamp" "$(bhp_json_escape "$BHP_BOARD_SSH_TARGET")" "$result" "$(bhp_json_escape "$(value os)")" \
  "$(bhp_json_escape "$(value kernel)")" "$(bhp_json_escape "$(value uptime_seconds)")" \
  "$(bhp_json_escape "$(value failed_units)")" "$(bhp_json_escape "$(value thermal_max_millicelsius)")" \
  "$checks" >"$record"
bhp_log info frdm_board_health_recorded "$TRACE_ID" result "$result" record "$record" >&2
printf '%s\n' "$record"
[ "$failures" -eq 0 ]
