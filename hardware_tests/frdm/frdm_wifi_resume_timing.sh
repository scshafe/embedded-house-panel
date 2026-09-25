#!/bin/sh
# FRDM-GATE-WIFI evidence: IW612 driver/firmware presence and recovery timing
# of Wi-Fi link, IPv4, default route, tailnet, and (optionally) Lexinet.
# Modes:
#   default           time from now (a baseline after boot or reconnect)
#   --after-next-resume  wait up to --observe-seconds for suspend_stats/success
#                     to increment (start this, then suspend with
#                     frdm_suspend_wake.sh --execute from a second console),
#                     and time from the detected resume
# Inputs: --observe-seconds N (5..900); optional --tailnet-probe HOST (tailscale
#   ping) and --lexinet-url URL (unauthenticated HTTPS GET; any HTTP status
#   counts as reachable); common options.
#   Wi-Fi credentials are provisioned outside the repository (wpa_supplicant).
# Output: hardware_tests/results/frdm/frdm-gate-wifi/<UTC>-frdm_wifi_resume_timing.json
#   One run = one sample; the gate needs repeated samples.
# Side effects: none (read-only observer; tailscale ping and one HTTPS GET when
#   asked). Timing resolution is one second (BusyBox sleep).
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 --observe-seconds N [--after-next-resume] [--tailnet-probe HOST] [--lexinet-url URL] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
observe_seconds=
after_resume=0
tailnet_probe=
lexinet_url=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--after-next-resume) after_resume=1; shift ;;
		--observe-seconds|--tailnet-probe|--lexinet-url)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			case "$1" in
				--observe-seconds) observe_seconds=$2 ;;
				--tailnet-probe) tailnet_probe=$2 ;;
				--lexinet-url) lexinet_url=$2 ;;
			esac
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
case "$observe_seconds" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
if [ "$observe_seconds" -lt 5 ] || [ "$observe_seconds" -gt 900 ]; then usage >&2; exit 2; fi
case "$lexinet_url" in ''|https://*) ;; *) usage >&2; exit 2 ;; esac
if [ -n "$BHP_EVIDENCE_ROOT" ]; then
	printf '%s\n' 'FRDM_EVIDENCE.CAPTURE_WITH_FIXTURE: timing needs the real board' >&2
	exit 2
fi

bhp_evidence_begin FRDM-GATE-WIFI frdm_wifi_resume_timing
expected_driver=$(bhp_evidence_profile_value BHP_BOARD_WIFI_EXPECTED_DRIVER)

uptime_seconds()
{
	sed -n 's/^\([0-9]*\).*/\1/p' /proc/uptime
}

wireless_interface()
{
	for net_directory in /sys/class/net/*; do
		[ -d "$net_directory/wireless" ] || [ -e "$net_directory/phy80211" ] || continue
		printf '%s\n' "${net_directory##*/}"
		return 0
	done
	return 1
}

driver_loaded=absent
[ -d "/sys/module/$expected_driver" ] && driver_loaded=loaded
[ "$driver_loaded" = loaded ] && status=pass || status=fail
bhp_evidence_check wifi_driver_module "$expected_driver loaded" "$driver_loaded" "$status" \
	"out-of-tree NXP moal from package bhp-nxp-mwifiex"
firmware_found=$(find /lib/firmware/nxp -name 'sd*iw612*' -o -name '*IW612*' 2>/dev/null | sed -n '1p')
if [ -n "$firmware_found" ]; then
	bhp_evidence_check wifi_firmware_present "IW612 firmware under /lib/firmware/nxp" "$firmware_found" pass
else
	bhp_evidence_check wifi_firmware_present "IW612 firmware under /lib/firmware/nxp" "absent" fail \
		"NXP firmware is installed only after the NXP-FIRMWARE-EULA human decision"
fi

if [ "$after_resume" -eq 1 ]; then
	success_start=$(sed -n '1p' /sys/power/suspend_stats/success 2>/dev/null)
	deadline=$(($(uptime_seconds) + observe_seconds))
	printf '%s\n' "Waiting up to ${observe_seconds}s for a resume (suspend_stats/success=$success_start)." >&2
	resumed=0
	while [ "$(uptime_seconds)" -lt "$deadline" ]; do
		if [ "$(sed -n '1p' /sys/power/suspend_stats/success 2>/dev/null)" != "$success_start" ]; then
			resumed=1
			break
		fi
		sleep 1
	done
	if [ "$resumed" -eq 0 ]; then
		bhp_evidence_check resume_detected "suspend_stats/success increments" "no resume in ${observe_seconds}s" unobserved
		bhp_evidence_finish
		exit 0
	fi
	bhp_evidence_check resume_detected "suspend_stats/success increments" "resume detected" pass
fi
start_time=$(uptime_seconds)
deadline=$((start_time + observe_seconds))
interface=
t_link=
t_ipv4=
t_route=
t_tailnet=
t_probe=
t_lexinet=
lexinet_status=
while [ "$(uptime_seconds)" -le "$deadline" ]; do
	now=$(( $(uptime_seconds) - start_time ))
	[ -n "$interface" ] || interface=$(wireless_interface || true)
	if [ -n "$interface" ]; then
		[ -z "$t_link" ] && [ "$(sed -n '1p' "/sys/class/net/$interface/operstate" 2>/dev/null)" = up ] && t_link=$now
		[ -z "$t_ipv4" ] && ip -4 addr show dev "$interface" 2>/dev/null | grep -q 'inet ' && t_ipv4=$now
		[ -z "$t_route" ] && awk -v iface="$interface" '$1 == iface && $2 == "00000000" { found = 1 } END { exit !found }' \
			/proc/net/route && t_route=$now
	fi
	if [ -z "$t_tailnet" ] && command -v tailscale >/dev/null 2>&1 &&
	   tailscale status --json 2>/dev/null | grep -q '"BackendState": *"Running"'; then
		t_tailnet=$now
	fi
	if [ -n "$t_tailnet" ] && [ -n "$tailnet_probe" ] && [ -z "$t_probe" ] &&
	   tailscale ping --c 1 --timeout 2s "$tailnet_probe" >/dev/null 2>&1; then
		t_probe=$now
	fi
	if [ -n "$lexinet_url" ] && [ -z "$t_lexinet" ] && [ -n "$t_route$t_tailnet" ] &&
	   command -v curl >/dev/null 2>&1; then
		# Any HTTP status proves reachability; credentials are never sent.
		http_code=$(curl -sS -o /dev/null -w '%{http_code}' --max-time 3 "$lexinet_url" 2>/dev/null)
		case "$http_code" in ''|000) ;; *) t_lexinet=$now; lexinet_status=$http_code ;; esac
	fi
	if [ -n "$t_link" ] && [ -n "$t_ipv4" ] && [ -n "$t_route" ] && [ -n "$t_tailnet" ] &&
	   { [ -z "$tailnet_probe" ] || [ -n "$t_probe" ]; } && { [ -z "$lexinet_url" ] || [ -n "$t_lexinet" ]; }; then
		break
	fi
	sleep 1
done

if [ -n "$interface" ]; then
	bhp_evidence_check wifi_interface "a wireless netdev" "$interface" pass
	bhp_evidence_candidate BHP_BOARD_WIFI_INTERFACE "$interface" "/sys/class/net/$interface/wireless"
else
	bhp_evidence_check wifi_interface "a wireless netdev" none fail
fi
record_timing()
{
	if [ -n "$2" ]; then
		bhp_evidence_check "$1" "reached within ${observe_seconds}s" "${2}s" pass "$3"
	else
		bhp_evidence_check "$1" "reached within ${observe_seconds}s" "not reached" fail "$3"
	fi
}
record_timing wifi_link_up_seconds "$t_link" "operstate up"
record_timing ipv4_address_seconds "$t_ipv4" "ip -4 addr"
record_timing default_route_seconds "$t_route" "/proc/net/route"
if command -v tailscale >/dev/null 2>&1; then
	record_timing tailnet_running_seconds "$t_tailnet" "tailscale status BackendState Running"
else
	bhp_evidence_check tailnet_running_seconds "reached" "tailscale absent" unobserved
fi
[ -n "$tailnet_probe" ] && record_timing tailnet_probe_seconds "$t_probe" "tailscale ping $tailnet_probe"
[ -n "$lexinet_url" ] && record_timing lexinet_reachable_seconds "$t_lexinet" \
	"unauthenticated HTTPS GET answered (status ${lexinet_status:-none})"
bhp_evidence_observe timing_origin "$([ "$after_resume" -eq 1 ] && printf resume_detected || printf script_start)"
bhp_evidence_observe timing_resolution_seconds 1
bhp_evidence_finish
