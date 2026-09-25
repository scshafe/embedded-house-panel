#!/bin/sh
# FRDM-GATE-SUSPEND evidence: suspend-to-RAM entry and wake attributed to the
# PIR gpio-keys IRQ (the bring-up smoke; acceptance counts later use
# hardware_tests/scripts/run_suspend_wake_trials.sh with FRDM expectations).
# Default (no --execute): read-only preflight of /sys/power/state, mem_sleep,
#   the PIR wakeup source, /sys/power/pm_wakeup_irq, the RTC safety alarm, and
#   suspend_stats.
# --execute --mem-sleep s2idle|deep --safety-rtc-seconds N (30..3600):
#   selects the mem_sleep variant, arms the SNVS RTC as a recovery wake, writes
#   `mem` to /sys/power/state (returns after resume), clears the alarm, and
#   attributes the wake via /sys/power/pm_wakeup_irq -> /proc/interrupts label.
#   Keep a serial console attached; walk past the PIR after entry.
# Output: hardware_tests/results/frdm/frdm-gate-suspend/<UTC>-frdm_suspend_wake.json
# Side effects: none without --execute. With it: mem_sleep selection (until
#   reboot), RTC wakealarm set then cleared, one suspend/resume cycle.
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 [--execute --mem-sleep s2idle|deep --safety-rtc-seconds N] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
execute=0
mem_sleep_request=
rtc_seconds=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--execute) execute=1; shift ;;
		--mem-sleep|--safety-rtc-seconds)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			if [ "$1" = --mem-sleep ]; then mem_sleep_request=$2; else rtc_seconds=$2; fi
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
if [ "$execute" -eq 1 ]; then
	case "$mem_sleep_request" in s2idle|deep) ;; *) usage >&2; exit 2 ;; esac
	case "$rtc_seconds" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
	if [ "$rtc_seconds" -lt 30 ] || [ "$rtc_seconds" -gt 3600 ]; then usage >&2; exit 2; fi
	if [ -n "$BHP_EVIDENCE_ROOT" ]; then
		printf '%s\n' 'FRDM_EVIDENCE.EXECUTE_WITH_FIXTURE: --execute needs the real board' >&2
		exit 2
	fi
elif [ -n "$mem_sleep_request$rtc_seconds" ]; then
	usage >&2
	exit 2
fi

bhp_evidence_begin FRDM-GATE-SUSPEND frdm_suspend_wake
input_name=$(bhp_evidence_profile_value BHP_BOARD_PIR_INPUT_NAME)
wake_label=$(bhp_evidence_profile_value BHP_BOARD_PIR_WAKE_INTERRUPT_LABEL)
wake_irq_path=$(bhp_evidence_profile_value BHP_BOARD_WAKE_IRQ_PATH)

power_states=$(bhp_evidence_read_line /sys/power/state || true)
case " $power_states " in
	*" mem "*) status=pass ;;
	"  ") status=unobserved ;;
	*) status=fail ;;
esac
bhp_evidence_check power_state_mem "mem listed" "${power_states:-unreadable}" "$status" "/sys/power/state"

mem_sleep_text=$(bhp_evidence_read_line /sys/power/mem_sleep || true)
mem_sleep_selected=$(printf '%s\n' "$mem_sleep_text" | sed -n 's/.*\[\([a-z0-9]*\)\].*/\1/p')
bhp_evidence_observe mem_sleep_before "${mem_sleep_text:-unreadable}"
case " $(printf '%s' "$mem_sleep_text" | tr -d '[]') " in
	*" deep "*) status=pass ;;
	"  ") status=unobserved ;;
	*) status=fail ;;
esac
bhp_evidence_check mem_sleep_deep_offered "deep listed (PSCI system suspend)" "${mem_sleep_text:-unreadable}" \
	"$status" "s2idle alone keeps the SoC powered; deep is the low-power target"

pir_event=
for event_directory in "$(bhp_evidence_path /sys/class/input)"/event[0-9]*; do
	[ "$(sed -n '1p' "$event_directory/device/name" 2>/dev/null)" = "$input_name" ] || continue
	pir_event=${event_directory##*/}
done
if [ -n "$pir_event" ]; then
	wakeup_state=$(bhp_evidence_read_line "/sys/class/input/$pir_event/device/device/power/wakeup" || true)
	[ "$wakeup_state" = enabled ] && status=pass || status=fail
	bhp_evidence_check pir_wakeup_enabled "enabled" "$pir_event: ${wakeup_state:-absent}" "$status"
else
	bhp_evidence_check pir_wakeup_enabled "enabled" "no input named $input_name" fail \
		"boot the project DTB built from measured FRDM-GATE-PIR values"
fi
[ -e "$(bhp_evidence_path "$wake_irq_path")" ] && status=pass || status=fail
bhp_evidence_check wake_irq_interface "$wake_irq_path exists" \
	"$([ "$status" = pass ] && printf present || printf absent)" "$status" "CONFIG_PM_SLEEP_DEBUG"
rtc_alarm=$(bhp_evidence_path /sys/class/rtc/rtc0/wakealarm)
[ -e "$rtc_alarm" ] && status=pass || status=fail
bhp_evidence_check rtc_safety_wake "/sys/class/rtc/rtc0/wakealarm" \
	"$([ "$status" = pass ] && printf present || printf absent)" "$status" \
	"a recovery wake is required before any unattended suspend"
bhp_evidence_observe rtc0_name "$(bhp_evidence_read_line /sys/class/rtc/rtc0/name || printf absent)"
success_before=$(bhp_evidence_read_line /sys/power/suspend_stats/success || true)
fail_before=$(bhp_evidence_read_line /sys/power/suspend_stats/fail || true)
bhp_evidence_observe suspend_stats_before "success=${success_before:-?} fail=${fail_before:-?}"
bhp_evidence_observe kernel_command_line "$(bhp_evidence_read_line /proc/cmdline || printf unreadable)"

if [ "$execute" -eq 0 ]; then
	bhp_evidence_check attributed_pir_wake "suspend entered and woken by $wake_label" "not attempted" \
		unobserved "rerun with --execute --mem-sleep deep --safety-rtc-seconds 120 at the serial console"
	bhp_evidence_finish
	exit 0
fi

if [ -z "$pir_event" ] || [ ! -e "$rtc_alarm" ]; then
	bhp_evidence_check attributed_pir_wake "suspend entered and woken by $wake_label" \
		"refused: PIR wakeup source or RTC safety alarm missing" fail
	bhp_evidence_finish
	exit 0
fi

if [ "$mem_sleep_selected" != "$mem_sleep_request" ]; then
	bhp_evidence_action mem_sleep_select "$mem_sleep_request > /sys/power/mem_sleep (was ${mem_sleep_selected:-unknown})"
	printf '%s\n' "$mem_sleep_request" >/sys/power/mem_sleep 2>/dev/null
fi
mem_sleep_now=$(sed -n 's/.*\[\([a-z0-9]*\)\].*/\1/p' /sys/power/mem_sleep)
if [ "$mem_sleep_now" != "$mem_sleep_request" ]; then
	bhp_evidence_check mem_sleep_selected "$mem_sleep_request" "${mem_sleep_now:-unknown}" fail
	bhp_evidence_finish
	exit 0
fi
bhp_evidence_check mem_sleep_selected "$mem_sleep_request" "$mem_sleep_now" pass

cp /proc/interrupts "$BHP_EVIDENCE_ARTIFACTS/interrupts_before.txt"
bhp_evidence_register_artifact interrupts_before.txt 0
printf '0\n' >"$rtc_alarm"
printf '+%s\n' "$rtc_seconds" >"$rtc_alarm"
bhp_evidence_action rtc_alarm_set "+${rtc_seconds}s on rtc0"
printf '%s\n' "Suspending now. Stay out of the PIR field for ~10 s, then walk past it. RTC recovery in ${rtc_seconds}s." >&2
sleep 2
entered_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
bhp_evidence_action suspend_write "mem > /sys/power/state at $entered_at"
if printf 'mem\n' >/sys/power/state 2>"$BHP_EVIDENCE_ARTIFACTS/suspend_write.err"; then
	write_result=returned_after_resume
else
	write_result=rejected
fi
returned_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
wake_irq=$(sed -n '1p' /sys/power/pm_wakeup_irq 2>/dev/null)
printf '0\n' >"$rtc_alarm"
bhp_evidence_action rtc_alarm_cleared "rtc0"
cp /proc/interrupts "$BHP_EVIDENCE_ARTIFACTS/interrupts_after.txt"
bhp_evidence_register_artifact interrupts_after.txt 0
dmesg 2>/dev/null | tail -n 200 >"$BHP_EVIDENCE_ARTIFACTS/kernel_log_tail.txt"
bhp_evidence_register_artifact kernel_log_tail.txt 0
bhp_evidence_register_artifact suspend_write.err 0
success_after=$(sed -n '1p' /sys/power/suspend_stats/success 2>/dev/null)
fail_after=$(sed -n '1p' /sys/power/suspend_stats/fail 2>/dev/null)
bhp_evidence_observe suspend_write_result "$write_result"
bhp_evidence_observe suspend_window_utc "$entered_at..$returned_at"
bhp_evidence_observe suspend_stats_after "success=${success_after:-?} fail=${fail_after:-?}"

if [ "$write_result" = returned_after_resume ] && [ -n "$success_before" ] && [ -n "$success_after" ] &&
   [ "$((success_after - success_before))" -eq 1 ] && [ "${fail_after:-x}" = "${fail_before:-y}" ]; then
	entered_status=pass
else
	entered_status=fail
fi
bhp_evidence_check suspend_entered "success +1, fail +0, write returned" \
	"write=$write_result success=${success_before:-?}->${success_after:-?} fail=${fail_before:-?}->${fail_after:-?}" \
	"$entered_status" "/sys/power/suspend_stats"

wake_line=
[ -n "$wake_irq" ] && wake_line=$(awk -v irq="$wake_irq:" '$1 == irq' /proc/interrupts | tr -s ' ' | sed -n '1p')
bhp_evidence_observe pm_wakeup_irq "${wake_irq:-none}"
bhp_evidence_observe wake_irq_line "${wake_line:-none}"
case "$wake_line" in
	*" $wake_label"*) attribution=pir ;;
	*rtc*|*snvs*) attribution=rtc_safety_alarm ;;
	'') attribution=unrecorded ;;
	*) attribution=other ;;
esac
if [ "$entered_status" = pass ] && [ "$attribution" = pir ]; then
	status=pass
	bhp_evidence_candidate BHP_BOARD_MEM_SLEEP_STATE "$mem_sleep_request" \
		"entered $mem_sleep_request and woke on $wake_label (IRQ $wake_irq)"
else
	status=fail
fi
bhp_evidence_check attributed_pir_wake "suspend entered and woken by $wake_label" \
	"attribution=$attribution" "$status" "/sys/power/pm_wakeup_irq -> /proc/interrupts"
bhp_evidence_check board_input_power_in_suspend "board-input power while suspended" \
	"measure with frdm_power_input_log.sh --software-state mem" unobserved \
	"FRDM-GATE-SUSPEND also requires board-input power"
bhp_evidence_finish
