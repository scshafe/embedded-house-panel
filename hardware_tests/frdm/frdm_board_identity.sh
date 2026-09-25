#!/bin/sh
# FRDM-GATE-IDENTITY evidence: SoC, CPU count, RAM, eMMC capacity, and the
# onboard IW612 SDIO probe compared with the board profile's EXPECTED_* hints.
# Inputs: common options (see --help); optional --board-marking TEXT (what is
#   printed on the received board, typed by the operator).
# Output: hardware_tests/results/frdm/frdm-gate-identity/<UTC>-frdm_board_identity.json
#   (schema hardware_tests/schemas/frdm_gate_evidence.schema.json); path on stdout.
# Side effects: none on the system (read-only). Idempotent: yes (new file per run).
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' "Usage: $0 [--board-marking TEXT] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
board_marking=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--board-marking) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; board_marking=$2; shift 2 ;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done

bhp_evidence_begin FRDM-GATE-IDENTITY frdm_board_identity

# Device tree and SoC.
expected_compatible=$(bhp_evidence_profile_value BHP_BOARD_DT_COMPATIBLE)
observed_compatible=$(bhp_evidence_read_dt_string /proc/device-tree/compatible || true)
case ",$observed_compatible," in
	*",$expected_compatible,"*) status=pass ;;
	,,) status=unobserved ;;
	*) status=fail ;;
esac
bhp_evidence_check dt_compatible "$expected_compatible" "${observed_compatible:-unreadable}" \
	"$status" "/proc/device-tree/compatible contains BHP_BOARD_DT_COMPATIBLE"
bhp_evidence_observe dt_model "$(bhp_evidence_read_dt_string /proc/device-tree/model || printf unreadable)"

expected_soc=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_SOC_ID)
observed_soc=$(bhp_evidence_read_line /sys/devices/soc0/soc_id || true)
if [ -z "$observed_soc" ]; then status=unobserved
elif [ "$observed_soc" = "$expected_soc" ]; then status=pass
else status=fail
fi
bhp_evidence_check soc_id "$expected_soc" "${observed_soc:-unreadable}" "$status" \
	"/sys/devices/soc0/soc_id (drivers/soc/imx/soc-imx8m.c)"
bhp_evidence_observe soc_revision "$(bhp_evidence_read_line /sys/devices/soc0/revision || printf unreadable)"

expected_cpus=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_CPU_COUNT)
cpuinfo_path=$(bhp_evidence_path /proc/cpuinfo)
if [ -r "$cpuinfo_path" ]; then
	observed_cpus=$(grep -c '^processor[[:space:]]*:' "$cpuinfo_path")
	[ "$observed_cpus" = "$expected_cpus" ] && status=pass || status=fail
else
	observed_cpus=unreadable
	status=unobserved
fi
bhp_evidence_check cpu_count "$expected_cpus" "$observed_cpus" "$status" "/proc/cpuinfo processor entries"

# RAM: MemTotal excludes firmware and early kernel reservations, so accept
# 80-100 % of the expected size. A 2 GiB variant cannot pass a 4 GiB check.
expected_ram_gib=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_RAM_GIB)
meminfo_path=$(bhp_evidence_path /proc/meminfo)
memtotal_kib=
[ -r "$meminfo_path" ] && memtotal_kib=$(awk '$1 == "MemTotal:" { print $2 }' "$meminfo_path")
if [ -z "$memtotal_kib" ] || [ -z "$expected_ram_gib" ]; then
	status=unobserved
else
	status=$(awk -v kib="$memtotal_kib" -v gib="$expected_ram_gib" 'BEGIN {
		full = gib * 1024 * 1024
		print (kib >= full * 0.80 && kib <= full) ? "pass" : "fail" }')
fi
bhp_evidence_check ram_capacity "${expected_ram_gib} GiB (MemTotal 80-100 %)" \
	"${memtotal_kib:-unreadable} kB" "$status" "/proc/meminfo MemTotal"

# eMMC on the expected SDHC host.
emmc_host=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_EMMC_MMC_HOST)
expected_emmc_gb=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_EMMC_GB)
emmc_bytes=
emmc_card=
for card_directory in "$(bhp_evidence_path "/sys/bus/platform/devices/$emmc_host")"/mmc_host/mmc*/mmc*:*; do
	[ -d "$card_directory" ] || continue
	[ "$(sed -n '1p' "$card_directory/type" 2>/dev/null)" = MMC ] || continue
	emmc_card=${card_directory##*/}
	for block_directory in "$card_directory"/block/mmcblk*; do
		[ -r "$block_directory/size" ] || continue
		emmc_sectors=$(sed -n '1p' "$block_directory/size")
		emmc_bytes=$(awk -v sectors="$emmc_sectors" 'BEGIN { printf "%.0f", sectors * 512 }')
		bhp_evidence_observe emmc_block_device "${block_directory##*/}"
		break
	done
	bhp_evidence_observe emmc_name "$(sed -n '1p' "$card_directory/name" 2>/dev/null)"
	bhp_evidence_observe emmc_manfid "$(sed -n '1p' "$card_directory/manfid" 2>/dev/null)"
	bhp_evidence_observe emmc_date "$(sed -n '1p' "$card_directory/date" 2>/dev/null)"
	break
done
if [ -z "$emmc_bytes" ]; then
	status=unobserved
else
	# Marketed GB are 10^9 bytes; user area is 85-100 % after boot/RPMB partitions.
	status=$(awk -v bytes="$emmc_bytes" -v gb="$expected_emmc_gb" 'BEGIN {
		full = gb * 1000000000
		print (bytes >= full * 0.85 && bytes <= full) ? "pass" : "fail" }')
fi
bhp_evidence_check emmc_capacity "${expected_emmc_gb} GB on $emmc_host (85-100 %)" \
	"${emmc_bytes:-no MMC card on $emmc_host} bytes${emmc_card:+ ($emmc_card)}" "$status" \
	"/sys/bus/platform/devices/<host>/mmc_host/*/<card>/block/*/size"

# Onboard IW612: an SDIO card on the expected host proves the onboard module;
# the chip name comes from the NXP driver log once its firmware is installed.
wifi_host=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_WIFI_SDIO_HOST)
expected_chip=$(bhp_evidence_profile_value BHP_BOARD_EXPECTED_WIFI_CHIP)
sdio_card=
for card_directory in "$(bhp_evidence_path "/sys/bus/platform/devices/$wifi_host")"/mmc_host/mmc*/mmc*:*; do
	[ -d "$card_directory" ] || continue
	[ "$(sed -n '1p' "$card_directory/type" 2>/dev/null)" = SDIO ] || continue
	sdio_card=${card_directory##*/}
	bhp_evidence_observe wifi_sdio_vendor "$(sed -n '1p' "$card_directory/vendor" 2>/dev/null)"
	bhp_evidence_observe wifi_sdio_device "$(sed -n '1p' "$card_directory/device" 2>/dev/null)"
	for function_directory in "$card_directory"/"$sdio_card":*; do
		[ -e "$function_directory/driver" ] || continue
		bhp_evidence_observe wifi_sdio_function_driver \
			"${function_directory##*/}=$(bhp_evidence_resolve "${function_directory#"$BHP_EVIDENCE_ROOT"}/driver" | sed 's#.*/##')"
	done
	break
done
[ -n "$sdio_card" ] && status=pass || status=fail
bhp_evidence_check wifi_sdio_card "SDIO card on $wifi_host" "${sdio_card:-none}" "$status" \
	"onboard module enumerates on the SoC SDHC host, not M.2"

kernel_log=$BHP_EVIDENCE_ARTIFACTS/kernel_log.txt
if [ -n "$BHP_EVIDENCE_ROOT" ]; then
	cat "$BHP_EVIDENCE_ROOT/fixture-dmesg.txt" >"$kernel_log" 2>/dev/null
else
	dmesg >"$kernel_log" 2>/dev/null
fi
bhp_evidence_register_artifact kernel_log.txt 0
if [ ! -s "$kernel_log" ]; then
	status=unobserved
	observed_chip='kernel log unreadable'
elif grep -i -E '(moal|mlan|wlan|sdio).*'"$expected_chip"'|'"$expected_chip"'.*(moal|mlan|wlan|firmware)' \
	"$kernel_log" >/dev/null; then
	status=pass
	observed_chip=$(grep -i -m 1 "$expected_chip" "$kernel_log" | cut -c1-160)
else
	status=unobserved
	observed_chip='no driver line names the chip (driver or NXP firmware not loaded)'
fi
bhp_evidence_check wifi_chip_name "$expected_chip" "$observed_chip" "$status" \
	"NXP moal/mlan kernel messages"

if [ -n "$board_marking" ]; then
	bhp_evidence_check board_markings "operator-recorded" "$board_marking" pass "operator attestation"
else
	bhp_evidence_check board_markings "operator-recorded" "not provided" unobserved \
		"pass --board-marking with the silkscreen/label text of the received board"
fi
bhp_evidence_observe kernel_command_line "$(bhp_evidence_read_line /proc/cmdline || printf unreadable)"

bhp_evidence_finish
