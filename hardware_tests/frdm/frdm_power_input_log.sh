#!/bin/sh
# FRDM-GATE-POWER evidence hook: joins an external board-input meter trace with
# an on-board state snapshot and emits a power_measurement v2 record.
# The FRDM cannot measure its own J2 USB-C PD input; an inline USB-C PD meter
# between the charger and J2 records elapsed_seconds,volts,amps as CSV.
# Inputs:
#   --software-state active_day|active_night|backlight_zero|idle|freeze|mem
#   --mem-sleep-state s2idle|shallow|deep     (required for mem)
#   --board-revision TEXT --accessory-revision TEXT [--pir-revision TEXT]
#   --supply-description TEXT                  (e.g. the charger model and rating)
#   [--meter-csv FILE --instrument TEXT]       header: elapsed_seconds,volts,amps
#   common options (see --help)
# Output: hardware_tests/results/frdm/frdm-gate-power/<UTC>-frdm_power_input_log.json
#   and, with a meter trace, power_measurement.json
#   (hardware_tests/schemas/power_measurement.schema.json, v2).
# Side effects: none (reads sysfs and the CSV). Idempotent: yes.
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 --software-state STATE [--mem-sleep-state VARIANT] --board-revision TEXT" \
		"          --accessory-revision TEXT [--pir-revision TEXT] --supply-description TEXT" \
		"          [--meter-csv FILE --instrument TEXT] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
software_state=
mem_sleep_state=
board_revision=
accessory_revision=
pir_revision=
supply_description=
meter_csv=
instrument=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--software-state|--mem-sleep-state|--board-revision|--accessory-revision|--pir-revision|\
		--supply-description|--meter-csv|--instrument)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			case "$1" in
				--software-state) software_state=$2 ;;
				--mem-sleep-state) mem_sleep_state=$2 ;;
				--board-revision) board_revision=$2 ;;
				--accessory-revision) accessory_revision=$2 ;;
				--pir-revision) pir_revision=$2 ;;
				--supply-description) supply_description=$2 ;;
				--meter-csv) meter_csv=$2 ;;
				--instrument) instrument=$2 ;;
			esac
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
case "$software_state" in
	active_day|active_night|backlight_zero|idle|freeze) [ -z "$mem_sleep_state" ] || { usage >&2; exit 2; } ;;
	mem) case "$mem_sleep_state" in s2idle|shallow|deep) ;; *) usage >&2; exit 2 ;; esac ;;
	*) usage >&2; exit 2 ;;
esac
if [ -z "$board_revision" ] || [ -z "$accessory_revision" ] || [ -z "$supply_description" ]; then
	usage >&2
	exit 2
fi
if [ -n "$meter_csv" ] && [ -z "$instrument" ]; then usage >&2; exit 2; fi

bhp_evidence_begin FRDM-GATE-POWER frdm_power_input_log
bhp_evidence_observe software_state "$software_state${mem_sleep_state:+:$mem_sleep_state}"
bhp_evidence_observe supply_description "$supply_description"
bhp_evidence_observe power_input_requirement "$(bhp_evidence_profile_value BHP_BOARD_POWER_INPUT_REQUIREMENT)"
bhp_evidence_observe power_input_connector "$(bhp_evidence_profile_value BHP_BOARD_POWER_INPUT_CONNECTOR)"

snapshot=$BHP_EVIDENCE_ARTIFACTS/onboard_snapshot.txt
{
	for port_directory in "$(bhp_evidence_path /sys/class/typec)"/port*; do
		[ -d "$port_directory" ] || continue
		for attribute in data_role power_role power_operation_mode; do
			printf 'typec %s %s=%s\n' "${port_directory##*/}" "$attribute" \
				"$(sed -n '1p' "$port_directory/$attribute" 2>/dev/null)"
		done
	done
	for supply_directory in "$(bhp_evidence_path /sys/class/power_supply)"/*; do
		[ -r "$supply_directory/uevent" ] || continue
		sed "s#^#power_supply ${supply_directory##*/} #" "$supply_directory/uevent"
	done
	for zone_directory in "$(bhp_evidence_path /sys/class/thermal)"/thermal_zone*; do
		[ -d "$zone_directory" ] || continue
		printf 'thermal %s type=%s temp_millicelsius=%s\n' "${zone_directory##*/}" \
			"$(sed -n '1p' "$zone_directory/type" 2>/dev/null)" "$(sed -n '1p' "$zone_directory/temp" 2>/dev/null)"
	done
	for policy_directory in "$(bhp_evidence_path /sys/devices/system/cpu/cpufreq)"/policy*; do
		[ -d "$policy_directory" ] || continue
		printf 'cpufreq %s cur_khz=%s governor=%s\n' "${policy_directory##*/}" \
			"$(sed -n '1p' "$policy_directory/scaling_cur_freq" 2>/dev/null)" \
			"$(sed -n '1p' "$policy_directory/scaling_governor" 2>/dev/null)"
	done
} >"$snapshot"
bhp_evidence_register_artifact onboard_snapshot.txt 0
bhp_evidence_observe typec_power_operation_mode \
	"$(sed -n 's/^typec [^ ]* power_operation_mode=//p' "$snapshot" | sed -n '1p')"

if [ -z "$meter_csv" ]; then
	bhp_evidence_check board_input_power_measured "external meter trace for $software_state" "none" \
		unobserved "pass --meter-csv from an inline USB-C PD meter at J2"
	bhp_evidence_finish
	exit 0
fi
if [ ! -r "$meter_csv" ]; then
	bhp_evidence_check board_input_power_measured "readable meter CSV" "$meter_csv unreadable" fail
	bhp_evidence_finish
	exit 0
fi
cp "$meter_csv" "$BHP_EVIDENCE_ARTIFACTS/meter.csv"
bhp_evidence_register_artifact meter.csv 0
summary=$(awk -F, '
	NR == 1 { if ($0 != "elapsed_seconds,volts,amps") { bad = "header"; exit } next }
	/^$/ { next }
	NF != 3 || $1 !~ /^[0-9]+(\.[0-9]+)?$/ || $2 !~ /^[0-9]+(\.[0-9]+)?$/ || $3 !~ /^[0-9]+(\.[0-9]+)?$/ {
		bad = "row " NR; exit }
	{
		if (count > 0 && $1 + 0 <= last) { bad = "time not increasing at row " NR; exit }
		if (count == 0) first = $1
		last = $1; count++; volts += $2; amps += $3; watts += $2 * $3
	}
	END {
		if (bad != "") { print "invalid " bad; exit }
		if (count < 2) { print "invalid fewer than 2 samples"; exit }
		printf "ok %d %.3f %.3f %.1f %.1f\n", count, last - first, volts / count, amps * 1000 / count, watts * 1000 / count
	}' "$meter_csv")
case "$summary" in
	ok\ *) ;;
	*)
		bhp_evidence_check board_input_power_measured "valid elapsed_seconds,volts,amps trace" "$summary" fail
		bhp_evidence_finish
		exit 0
		;;
esac
# Word splitting of the numeric summary is intentional.
# shellcheck disable=SC2086
set -- $summary
sample_count=$2
duration=$3
mean_volts=$4
mean_milliamps=$5
mean_milliwatts=$6
if awk -v v="$mean_volts" 'BEGIN { exit !(v >= 11.4 && v <= 21.0) }'; then status=pass; else status=fail; fi
bhp_evidence_check pd_contract_voltage "12-20 V board input (+/-5 %)" "${mean_volts} V mean" "$status" \
	"a 5 V contract means the charger did not negotiate a supported PD profile"
bhp_evidence_check board_input_power_measured "external meter trace for $software_state" \
	"${mean_milliwatts} mW mean over ${duration} s (${sample_count} samples)" pass "$instrument"

image_commit=$(sed -n 's/^project_commit=//p' \
	"$(bhp_evidence_path /etc/beaglebone-house-panel/image-version)" 2>/dev/null)
case "$image_commit" in [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;; *) image_commit=not_a_project_image ;; esac
measurement=$BHP_EVIDENCE_ARTIFACTS/power_measurement.json
{
	printf '{"schema_version":2,"measurement_id":"%s","measured_at_utc":"%s",' \
		"$(bhp_json_escape "$BHP_EVIDENCE_TRACE_ID")" "$BHP_EVIDENCE_STARTED_AT"
	printf '"git_commit":"%s","board_id":"%s","board_revision":"%s","accessory_revision":"%s",' \
		"$image_commit" "$(bhp_json_escape "$BHP_BOARD_ID")" "$(bhp_json_escape "$board_revision")" \
		"$(bhp_json_escape "$accessory_revision")"
	[ -n "$pir_revision" ] && printf '"pir_revision":"%s",' "$(bhp_json_escape "$pir_revision")"
	printf '"kernel_release":"%s","software_state":"%s",' \
		"$(bhp_json_escape "$(bhp_evidence_read_line /proc/sys/kernel/osrelease || printf unobserved)")" \
		"$software_state"
	[ -n "$mem_sleep_state" ] && printf '"mem_sleep_state":"%s",' "$mem_sleep_state"
	printf '"supply_boundary":"%s","supply_voltage_volts":%s,"current_milliamps":%s,"power_milliwatts":%s,' \
		"$(bhp_evidence_profile_value BHP_BOARD_POWER_BOUNDARY)" "$mean_volts" "$mean_milliamps" "$mean_milliwatts"
	printf '"instrument":"%s","sample_duration_seconds":%s,"sample_count":%s,"notes":"%s"}\n' \
		"$(bhp_json_escape "$instrument")" "$duration" "$sample_count" \
		"$(bhp_json_escape "supply: $supply_description")"
} >"$measurement"
bhp_evidence_register_artifact power_measurement.json 0
bhp_evidence_finish
