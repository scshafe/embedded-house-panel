#!/bin/sh
# FRDM-GATE-PIR evidence, part 2: PIR edges on the candidate native GPIO.
# Two modes, run in order during bring-up:
#   --mode gpio  --controller gpioN --line L  (before the project DTB exists)
#       reads the raw line with libgpiod (v1 or v2 CLI) as an input: idle level,
#       then every edge for --duration-seconds while the operator walks past.
#       Derives the BHP_BOARD_PIR_ACTIVE_LEVEL candidate. The pad must already be
#       muxed as GPIO (frdm_j18_gpio_inventory.sh pad owner "none").
#   --mode evdev  (after booting imx8mp-frdm-bhp-panel.dtb)
#       captures EV_SW SW_FRONT_PROXIMITY from the gpio-keys input named
#       BHP_BOARD_PIR_INPUT_NAME with evtest and checks wakeup capability and
#       the /proc/interrupts label BHP_BOARD_PIR_WAKE_INTERRUPT_LABEL.
# Inputs: --mode, --duration-seconds N (1..600), common options (see --help).
# Output: hardware_tests/results/frdm/frdm-gate-pir/<UTC>-frdm_pir_edge_capture.json
# Side effects: requests the GPIO line as an input for the capture window
#   (gpio mode); nothing else. Idempotent: yes.
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 --mode gpio --controller gpio<1-5> --line <0-31> --duration-seconds N [common options]" \
		"       $0 --mode evdev --duration-seconds N [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
mode=
controller=
line=
duration=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--mode|--controller|--line|--duration-seconds)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			case "$1" in
				--mode) mode=$2 ;;
				--controller) controller=$2 ;;
				--line) line=$2 ;;
				--duration-seconds) duration=$2 ;;
			esac
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
case "$duration" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 600 ]; then usage >&2; exit 2; fi
case "$mode" in
	gpio)
		case "$controller" in gpio[1-5]) ;; *) usage >&2; exit 2 ;; esac
		case "$line" in [0-9]|[12][0-9]|3[01]) ;; *) usage >&2; exit 2 ;; esac
		;;
	evdev) ;;
	*) usage >&2; exit 2 ;;
esac
if [ -n "$BHP_EVIDENCE_ROOT" ]; then
	printf '%s\n' 'FRDM_EVIDENCE.CAPTURE_WITH_FIXTURE: edge capture needs the real board' >&2
	exit 2
fi

bhp_evidence_begin FRDM-GATE-PIR frdm_pir_edge_capture
bhp_evidence_observe mode "$mode"
bhp_evidence_observe capture_window_seconds "$duration"
printf '%s\n' "Capturing PIR edges for ${duration}s: stay still first, then walk past the sensor." >&2

if [ "$mode" = gpio ]; then
	controller_node=$(bhp_evidence_read_dt_string "/proc/device-tree/__symbols__/$controller" || true)
	gpiochip=
	if [ -n "$controller_node" ]; then
		for chip_directory in /sys/bus/platform/devices/"${controller_node##*@}".gpio/gpiochip*; do
			[ -d "$chip_directory" ] && gpiochip=${chip_directory##*/}
		done
	fi
	if [ -z "$gpiochip" ] || ! command -v gpiomon >/dev/null 2>&1 || ! command -v gpioget >/dev/null 2>&1; then
		bhp_evidence_check gpio_capture_ready "gpiochip for $controller and libgpiod tools" \
			"chip=${gpiochip:-unresolved} gpiomon=$(command -v gpiomon || printf absent)" fail
		bhp_evidence_finish
		exit 0
	fi
	bhp_evidence_observe gpiochip "$gpiochip"
	libgpiod_major=1
	gpiomon --version 2>/dev/null | grep -q 'v2\.' && libgpiod_major=2
	bhp_evidence_observe libgpiod_cli_major "$libgpiod_major"
	if [ "$libgpiod_major" = 2 ]; then
		idle_value=$(gpioget --numeric --chip "$gpiochip" "$line" 2>"$BHP_EVIDENCE_ARTIFACTS/gpioget.err")
		timeout "$duration" gpiomon --chip "$gpiochip" --format '%e %S' "$line" \
			>"$BHP_EVIDENCE_ARTIFACTS/edges.txt" 2>&1
		rising=1
		falling=2
	else
		idle_value=$(gpioget "$gpiochip" "$line" 2>"$BHP_EVIDENCE_ARTIFACTS/gpioget.err")
		timeout "$duration" gpiomon --line-buffered --format='%e %s.%n' "$gpiochip" "$line" \
			>"$BHP_EVIDENCE_ARTIFACTS/edges.txt" 2>&1
		rising=1
		falling=0
	fi
	bhp_evidence_register_artifact edges.txt 0
	idle_value=$(printf '%s' "$idle_value" | tr -d '[:space:]')
	rising_count=$(awk -v code="$rising" '$1 == code' "$BHP_EVIDENCE_ARTIFACTS/edges.txt" | wc -l | tr -d ' ')
	falling_count=$(awk -v code="$falling" '$1 == code' "$BHP_EVIDENCE_ARTIFACTS/edges.txt" | wc -l | tr -d ' ')
	case "$idle_value" in
		0|1) bhp_evidence_check idle_level_read "0 or 1" "$idle_value" pass "gpioget before the window" ;;
		*) bhp_evidence_check idle_level_read "0 or 1" "${idle_value:-none}" fail "see gpioget.err" ;;
	esac
	if [ "$rising_count" -ge 1 ] && [ "$falling_count" -ge 1 ]; then
		status=pass
	else
		status=fail
	fi
	bhp_evidence_check edges_observed ">=1 rising and >=1 falling edge" \
		"rising=$rising_count falling=$falling_count" "$status" "operator walked past during the window"
	if [ "$status" = pass ]; then
		case "$idle_value" in
			0) bhp_evidence_candidate BHP_BOARD_PIR_ACTIVE_LEVEL high "idle 0, motion drives the line high" ;;
			1) bhp_evidence_candidate BHP_BOARD_PIR_ACTIVE_LEVEL low "idle 1, motion drives the line low" ;;
		esac
	fi
	bhp_evidence_check wake_capable_proof "gpio-keys wakeup-source + attributed resume" \
		"raw GPIO mode cannot prove wake" unobserved "rerun with --mode evdev on the project DTB, then frdm_suspend_wake.sh"
	bhp_evidence_finish
	exit 0
fi

# evdev mode: the project DTB's gpio-keys node.
input_name=$(bhp_evidence_profile_value BHP_BOARD_PIR_INPUT_NAME)
wake_label=$(bhp_evidence_profile_value BHP_BOARD_PIR_WAKE_INTERRUPT_LABEL)
pir_event=
for event_directory in /sys/class/input/event[0-9]*; do
	[ "$(sed -n '1p' "$event_directory/device/name" 2>/dev/null)" = "$input_name" ] || continue
	[ -z "$pir_event" ] || { pir_event=ambiguous; break; }
	pir_event=${event_directory##*/}
done
case "$pir_event" in
	''|ambiguous)
		bhp_evidence_check pir_input_device "exactly one input named $input_name" "${pir_event:-none}" fail \
			"boot imx8mp-frdm-bhp-panel.dtb built with measured FRDM-GATE-PIR values"
		bhp_evidence_finish
		exit 0
		;;
esac
bhp_evidence_check pir_input_device "exactly one input named $input_name" "/dev/input/$pir_event" pass
wakeup_state=$(sed -n '1p' "/sys/class/input/$pir_event/device/device/power/wakeup" 2>/dev/null)
[ "$wakeup_state" = enabled ] && status=pass || status=fail
bhp_evidence_check gpio_keys_wakeup "enabled" "${wakeup_state:-absent}" "$status" \
	"/sys/class/input/<event>/device/device/power/wakeup from wakeup-source"
irq_line=$(grep -F " $wake_label" /proc/interrupts | sed -n '1p')
if [ -n "$irq_line" ]; then
	bhp_evidence_check pir_irq_label "$wake_label in /proc/interrupts" "$(printf '%s' "$irq_line" | tr -s ' ' | cut -c1-160)" pass
	irq_number=$(printf '%s\n' "$irq_line" | sed 's/^ *\([0-9]*\):.*/\1/')
	bhp_evidence_observe pir_irq_number "$irq_number"
	irq_count_before=$(awk -v irq="$irq_number:" '$1 == irq { total = 0; for (i = 2; i <= NF && $i ~ /^[0-9]+$/; i++) total += $i; print total }' /proc/interrupts)
else
	bhp_evidence_check pir_irq_label "$wake_label in /proc/interrupts" none fail
	irq_count_before=
fi
initial_state=unknown
if command -v evtest >/dev/null 2>&1; then
	evtest --query "/dev/input/$pir_event" EV_SW SW_FRONT_PROXIMITY
	case "$?" in 0) initial_state=0 ;; 10) initial_state=1 ;; esac
	timeout "$duration" evtest "/dev/input/$pir_event" >"$BHP_EVIDENCE_ARTIFACTS/evtest.txt" 2>&1
	bhp_evidence_register_artifact evtest.txt 0
	event_count=$(grep -c 'type 5 (EV_SW), code 11 (SW_FRONT_PROXIMITY)' "$BHP_EVIDENCE_ARTIFACTS/evtest.txt")
	assert_count=$(grep -c 'code 11 (SW_FRONT_PROXIMITY), value 1' "$BHP_EVIDENCE_ARTIFACTS/evtest.txt")
	release_count=$(grep -c 'code 11 (SW_FRONT_PROXIMITY), value 0' "$BHP_EVIDENCE_ARTIFACTS/evtest.txt")
	if [ "$assert_count" -ge 1 ] && [ "$release_count" -ge 1 ]; then status=pass; else status=fail; fi
	bhp_evidence_check sw_front_proximity_events ">=1 value 1 and >=1 value 0" \
		"events=$event_count asserted=$assert_count released=$release_count initial=$initial_state" "$status"
else
	bhp_evidence_check sw_front_proximity_events "evtest capture" "evtest absent" unobserved
fi
if [ -n "$irq_count_before" ]; then
	irq_count_after=$(awk -v irq="$irq_number:" '$1 == irq { total = 0; for (i = 2; i <= NF && $i ~ /^[0-9]+$/; i++) total += $i; print total }' /proc/interrupts)
	bhp_evidence_observe pir_irq_count_delta "$((irq_count_after - irq_count_before))"
fi
bhp_evidence_finish
