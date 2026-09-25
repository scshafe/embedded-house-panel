#!/bin/sh
# FRDM-GATE-DISPLAY, FRDM-GATE-TOUCH, FRDM-GATE-BACKLIGHT evidence: DRM card,
# DSI connector and 1024x600 mode, fbdev fallback node, GT911 evdev identity
# and polling mode, and the Waveshare bridge backlight node.
# Inputs: common options (see --help). Optional, system-changing, off by default:
#   --execute-test-pattern SECONDS  show a modetest pattern on the connected DSI
#                                   connector, then ask the operator on stdin
#   --execute-backlight-off-trial SECONDS  write brightness 0, ask the operator
#                                   on stdin whether the panel is dark, restore
# Output: three JSON files, one per gate, under hardware_tests/results/frdm/.
#   Their paths on stdout, one per line.
# Side effects: none unless an --execute-* option is given; the backlight trial
#   always restores the previous brightness. Idempotent: yes.
# Exit: 0 evidence written (read each `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 [--execute-test-pattern SECONDS] [--execute-backlight-off-trial SECONDS] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
pattern_seconds=
backlight_trial_seconds=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--execute-test-pattern|--execute-backlight-off-trial)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			case "$2" in ''|*[!0-9]*|0) usage >&2; exit 2 ;; esac
			if [ "$1" = --execute-test-pattern ]; then
				pattern_seconds=$2
			else
				backlight_trial_seconds=$2
			fi
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
if { [ -n "$pattern_seconds" ] || [ -n "$backlight_trial_seconds" ]; } && [ -n "$BHP_EVIDENCE_ROOT" ]; then
	printf '%s\n' 'FRDM_EVIDENCE.EXECUTE_WITH_FIXTURE: --execute-* needs the real board' >&2
	exit 2
fi

# Reads one operator answer from stdin; prints not_observed on EOF.
ask_operator()
{
	printf '%s\n' "$1" >&2
	if IFS= read -r operator_answer; then
		case "$operator_answer" in
			"$2"|"$3") printf '%s\n' "$operator_answer" ;;
			*) printf '%s\n' invalid_answer ;;
		esac
	else
		printf '%s\n' not_observed
	fi
}

# ---------------------------------------------------------------- display
bhp_evidence_begin FRDM-GATE-DISPLAY frdm_display_touch_backlight
expected_resolution=$(bhp_evidence_profile_value BHP_BOARD_DISPLAY_RESOLUTION)
connector_report=$BHP_EVIDENCE_ARTIFACTS/drm_connectors.txt
connected_dsi=
for connector_directory in "$(bhp_evidence_path /sys/class/drm)"/card*-*; do
	[ -d "$connector_directory" ] || continue
	connector_name=${connector_directory##*/}
	connector_status=$(sed -n '1p' "$connector_directory/status" 2>/dev/null)
	connector_modes=$(tr '\n' ' ' <"$connector_directory/modes" 2>/dev/null)
	connector_id=$(sed -n '1p' "$connector_directory/connector_id" 2>/dev/null)
	printf '%s status=%s connector_id=%s modes=%s\n' "$connector_name" "${connector_status:-?}" \
		"${connector_id:-unexposed}" "$connector_modes" >>"$connector_report"
	case "$connector_name" in
		*-DSI-*)
			[ "$connector_status" = connected ] && [ -z "$connected_dsi" ] &&
				connected_dsi=$connector_name
			;;
	esac
done
[ -f "$connector_report" ] || : >"$connector_report"
bhp_evidence_register_artifact drm_connectors.txt 0
if [ -n "$connected_dsi" ]; then
	bhp_evidence_check dsi_connector_connected "a connected cardN-DSI-M connector" "$connected_dsi" pass
	drm_card=${connected_dsi%%-*}
	bhp_evidence_candidate BHP_BOARD_DRM_DEVICE "/dev/dri/$drm_card" "/sys/class/drm/$connected_dsi/status=connected"
	dsi_directory=$(bhp_evidence_path "/sys/class/drm/$connected_dsi")
	if grep -x -F "$expected_resolution" "$dsi_directory/modes" >/dev/null 2>&1; then
		status=pass
	else
		status=fail
	fi
	bhp_evidence_check dsi_mode "$expected_resolution" \
		"$(tr '\n' ' ' <"$dsi_directory/modes" 2>/dev/null)" "$status" "/sys/class/drm/<connector>/modes"
	connector_id=$(sed -n '1p' "$dsi_directory/connector_id" 2>/dev/null)
	if [ -z "$connector_id" ] && [ -z "$BHP_EVIDENCE_ROOT" ] && command -v modetest >/dev/null 2>&1; then
		bhp_evidence_capture modetest_connectors modetest -D "/dev/dri/$drm_card" -c
		connector_id=$(awk '$4 == "connected" && $5 ~ /^DSI-/ { print $1; exit }' \
			"$BHP_EVIDENCE_ARTIFACTS/modetest_connectors.txt")
	fi
	if [ -n "$connector_id" ]; then
		bhp_evidence_check dsi_connector_id "numeric DRM connector id" "$connector_id" pass
		bhp_evidence_candidate BHP_BOARD_DRM_CONNECTOR_ID "$connector_id" "connector_id of $connected_dsi"
	else
		bhp_evidence_check dsi_connector_id "numeric DRM connector id" "unexposed" unobserved \
			"no connector_id attribute and modetest unavailable"
	fi
	driver=$(bhp_evidence_resolve "/sys/class/drm/$drm_card/device/driver" | sed 's#.*/##')
	bhp_evidence_observe drm_card_driver "${driver:-unresolved}"
else
	bhp_evidence_check dsi_connector_connected "a connected cardN-DSI-M connector" "none" fail \
		"check the 22-pin J15 cable, the imx8mp-frdm-waveshare-7inch-c-panel overlay, and dmesg"
	bhp_evidence_check dsi_mode "$expected_resolution" "no connected DSI connector" unobserved
	bhp_evidence_check dsi_connector_id "numeric DRM connector id" "no connected DSI connector" unobserved
fi

fbdev_found=
for framebuffer_directory in "$(bhp_evidence_path /sys/class/graphics)"/fb[0-9]*; do
	[ -d "$framebuffer_directory" ] || continue
	fbdev_found=${framebuffer_directory##*/}
	bhp_evidence_observe "${fbdev_found}_name" "$(sed -n '1p' "$framebuffer_directory/name" 2>/dev/null)"
	bhp_evidence_observe "${fbdev_found}_virtual_size" \
		"$(sed -n '1p' "$framebuffer_directory/virtual_size" 2>/dev/null)"
	break
done
if [ -n "$fbdev_found" ]; then
	bhp_evidence_check fbdev_fallback_node "/dev/fbN from DRM fbdev emulation" "/dev/$fbdev_found" pass \
		"fallback only; DRM stays primary"
	bhp_evidence_candidate BHP_BOARD_FBDEV_DEVICE "/dev/$fbdev_found" "/sys/class/graphics/$fbdev_found"
else
	bhp_evidence_check fbdev_fallback_node "/dev/fbN from DRM fbdev emulation" none unobserved
fi

if [ -n "$pattern_seconds" ]; then
	if [ -n "$connected_dsi" ] && [ -n "${connector_id:-}" ] && command -v modetest >/dev/null 2>&1; then
		bhp_evidence_action modetest_pattern "modetest -D /dev/dri/$drm_card -s $connector_id:$expected_resolution for ${pattern_seconds}s"
		# modetest holds the mode until its stdin closes, i.e. until sleep ends.
		sleep "$pattern_seconds" | modetest -D "/dev/dri/$drm_card" \
			-s "$connector_id:$expected_resolution" \
			>"$BHP_EVIDENCE_ARTIFACTS/modetest_pattern.txt" 2>&1 &
		answer=$(ask_operator \
			"Within ${pattern_seconds}s: is a test pattern visible on the panel? type yes or no, then Enter" yes no)
		wait
		bhp_evidence_register_artifact modetest_pattern.txt 0
		case "$answer" in
			yes) status=pass ;;
			no) status=fail ;;
			*) status=unobserved ;;
		esac
		bhp_evidence_check first_frame_visible "operator sees the modetest pattern" "$answer" "$status" \
			"operator attestation"
	else
		bhp_evidence_check first_frame_visible "operator sees the modetest pattern" \
			"not attempted: no connector id or modetest" unobserved
	fi
else
	bhp_evidence_check first_frame_visible "operator sees a frame" "not attempted" unobserved \
		"rerun with --execute-test-pattern 30 at the console"
fi
bhp_evidence_finish

# ---------------------------------------------------------------- touch
bhp_evidence_begin FRDM-GATE-TOUCH frdm_display_touch_backlight
expected_driver=$(bhp_evidence_profile_value BHP_BOARD_TOUCH_EXPECTED_DRIVER)
expected_controller=$(bhp_evidence_profile_value BHP_BOARD_TOUCH_EXPECTED_I2C_CONTROLLER)
expected_address=$(bhp_evidence_profile_value BHP_BOARD_TOUCH_EXPECTED_I2C_ADDRESS)
expected_mode=$(bhp_evidence_profile_value BHP_BOARD_TOUCH_EXPECTED_MODE)
controller_node=$(bhp_evidence_read_dt_string "/proc/device-tree/__symbols__/$expected_controller" || true)
address_suffix=@${expected_address#0x}
touch_event=
touch_name=
touch_phys=
touch_i2c_client=
for event_directory in "$(bhp_evidence_path /sys/class/input)"/event[0-9]*; do
	[ -d "$event_directory/device" ] || continue
	event_name=${event_directory##*/}
	parent_driver=$(bhp_evidence_resolve "/sys/class/input/$event_name/device/device/driver" | sed 's#.*/##')
	[ "$parent_driver" = "$expected_driver" ] || continue
	of_node=$(bhp_evidence_resolve "/sys/class/input/$event_name/device/device/of_node" || true)
	of_node=${of_node#/sys/firmware/devicetree/base}
	case "$of_node" in
		"$controller_node"/*"$address_suffix") ;;
		*) [ -n "$controller_node" ] && continue ;;
	esac
	touch_event=$event_name
	touch_name=$(sed -n '1p' "$event_directory/device/name" 2>/dev/null)
	touch_phys=$(sed -n '1p' "$event_directory/device/phys" 2>/dev/null)
	touch_i2c_client=$(bhp_evidence_resolve "/sys/class/input/$event_name/device/device" | sed 's#.*/##')
	bhp_evidence_observe touch_of_node "${of_node:-unresolved}"
	break
done
if [ -n "$touch_event" ]; then
	bhp_evidence_check touch_evdev "$expected_driver on $expected_controller at $expected_address" \
		"/dev/input/$touch_event ($touch_i2c_client)" pass "driver + of_node under __symbols__/$expected_controller"
	bhp_evidence_candidate BHP_BOARD_TOUCH_INPUT_NAME "$(bhp_evidence_profile_encode "$touch_name")" \
		"/sys/class/input/$touch_event/device/name=$touch_name"
	if [ -n "$touch_phys" ]; then
		bhp_evidence_check touch_phys_nonempty "nonempty phys for exact mdev classification" "$touch_phys" pass
		bhp_evidence_candidate BHP_BOARD_TOUCH_INPUT_PHYS "$(bhp_evidence_profile_encode "$touch_phys")" \
			"/sys/class/input/$touch_event/device/phys"
	else
		bhp_evidence_check touch_phys_nonempty "nonempty phys for exact mdev classification" "empty" fail \
			"classify-touch-input keeps a phys-less device root-only"
	fi
	interrupts_file=$(bhp_evidence_path /proc/interrupts)
	touch_irq_line=
	[ -r "$interrupts_file" ] && touch_irq_line=$(grep -F "$touch_i2c_client" "$interrupts_file" | sed -n '1p')
	[ -z "$touch_irq_line" ] && [ -r "$interrupts_file" ] &&
		touch_irq_line=$(grep -i -F "$expected_driver" "$interrupts_file" | sed -n '1p')
	if [ -n "$touch_irq_line" ]; then
		observed_mode=interrupt
	else
		observed_mode=polled
	fi
	[ "$observed_mode" = "$expected_mode" ] && status=pass || status=fail
	bhp_evidence_check touch_mode "$expected_mode" "$observed_mode" "$status" \
		"no /proc/interrupts line for the client means goodix.c polls"
	if [ "$observed_mode" = polled ]; then
		bhp_evidence_candidate BHP_BOARD_TOUCH_INTERRUPT polled "no IRQ requested by $touch_i2c_client"
	fi
else
	bhp_evidence_check touch_evdev "$expected_driver on $expected_controller at $expected_address" none fail \
		"UM12340 requires the separate display I2C wiring to the board; check it and dmesg for goodix"
	bhp_evidence_check touch_phys_nonempty "nonempty phys" "no touch device" unobserved
	bhp_evidence_check touch_mode "$expected_mode" "no touch device" unobserved
fi
bhp_evidence_check touch_provenance_adr_0005 "ADR 0005 touch provenance exercised" "not in scope of discovery" \
	unobserved "requires the panel UI touch path; recorded so the gate cannot close on discovery alone"
bhp_evidence_finish

# ---------------------------------------------------------------- backlight
bhp_evidence_begin FRDM-GATE-BACKLIGHT frdm_display_touch_backlight
name_glob=$(bhp_evidence_profile_value BHP_BOARD_BACKLIGHT_EXPECTED_NAME_GLOB)
expected_max=$(bhp_evidence_profile_value BHP_BOARD_BACKLIGHT_EXPECTED_MAX_BRIGHTNESS)
backlight_match=
backlight_count=0
for backlight_directory in "$(bhp_evidence_path /sys/class/backlight)"/*; do
	[ -d "$backlight_directory" ] || continue
	backlight_name=${backlight_directory##*/}
	bhp_evidence_observe "backlight.$backlight_name" \
		"type=$(sed -n '1p' "$backlight_directory/type" 2>/dev/null) max=$(sed -n '1p' "$backlight_directory/max_brightness" 2>/dev/null) brightness=$(sed -n '1p' "$backlight_directory/brightness" 2>/dev/null)"
	# The glob is a shell pattern from the profile by design.
	# shellcheck disable=SC2254
	case "$backlight_name" in
		$name_glob) backlight_count=$((backlight_count + 1)); backlight_match=$backlight_name ;;
	esac
done
if [ "$backlight_count" -eq 1 ]; then
	bhp_evidence_check backlight_node "exactly one /sys/class/backlight/$name_glob" "$backlight_match" pass \
		"waveshare-dsi bridge registers BACKLIGHT_RAW named after its I2C client"
	bhp_evidence_candidate BHP_BOARD_BACKLIGHT_SYSFS_DIR "/sys/class/backlight/$backlight_match" \
		"single node matching $name_glob"
	observed_max=$(bhp_evidence_read_line "/sys/class/backlight/$backlight_match/max_brightness" || true)
	[ "$observed_max" = "$expected_max" ] && status=pass || status=fail
	bhp_evidence_check backlight_max_brightness "$expected_max" "${observed_max:-unreadable}" "$status"
else
	bhp_evidence_check backlight_node "exactly one /sys/class/backlight/$name_glob" \
		"$backlight_count matching" fail "FRDM-GATE-BACKLIGHT stays open; see the project overlay note"
	bhp_evidence_check backlight_max_brightness "$expected_max" "no unique node" unobserved
fi

if [ -n "$backlight_trial_seconds" ] && [ "$backlight_count" -eq 1 ]; then
	brightness_path=/sys/class/backlight/$backlight_match/brightness
	previous_brightness=$(sed -n '1p' "$brightness_path")
	bhp_evidence_action backlight_write "0 to $brightness_path for ${backlight_trial_seconds}s (was $previous_brightness)"
	if printf '0\n' >"$brightness_path"; then
		answer=$(ask_operator "Is the panel backlight completely dark? type dark or lit, then Enter" dark lit)
		sleep "$backlight_trial_seconds"
	else
		answer=write_failed
	fi
	printf '%s\n' "$previous_brightness" >"$brightness_path"
	bhp_evidence_action backlight_restore "$previous_brightness to $brightness_path"
	case "$answer" in
		dark) status=pass; bhp_evidence_candidate BHP_BOARD_BACKLIGHT_OFF_VALUE 0 "operator saw a dark panel at brightness 0" ;;
		lit) status=fail ;;
		*) status=unobserved ;;
	esac
	bhp_evidence_check backlight_zero_is_off "operator sees a dark panel at 0" "$answer" "$status" \
		"operator attestation"
else
	bhp_evidence_check backlight_zero_is_off "operator sees a dark panel at 0" "not attempted" unobserved \
		"rerun with --execute-backlight-off-trial 5 at the console"
fi
bhp_evidence_finish
