#!/bin/sh
# FRDM-GATE-PIR evidence, part 1: J18 pad ownership and header I/O voltage.
# Software cannot measure the header voltage; the operator measures it (J18 pin
# to GND with a meter, board powered, nothing connected) and passes the value.
# Inputs: common options (see --help) and, to evaluate one candidate pin,
#   --candidate J18-<pin>:<PAD_MUX>   e.g. J18-7:MX8MP_IOMUXC_SAI1_RXD1__GPIO4_IO03
#     (the pin/pad pairing comes from the FRDM-IMX8MPLUS schematic or UM12340)
#   --measured-header-voltage-mv <mV> --voltage-method <text>
# Output: hardware_tests/results/frdm/frdm-gate-pir/<UTC>-frdm_j18_gpio_inventory.json
#   plus raw pinmux, GPIO, and regulator captures. Path on stdout.
# Side effects: none (read-only; reads debugfs when mounted). Idempotent: yes.
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' \
		"Usage: $0 [--candidate J18-<pin>:<PAD_MUX>] [--measured-header-voltage-mv MV --voltage-method TEXT] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
candidate=
measured_mv=
voltage_method=
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--candidate|--measured-header-voltage-mv|--voltage-method)
			[ "$#" -ge 2 ] || { usage >&2; exit 2; }
			case "$1" in
				--candidate) candidate=$2 ;;
				--measured-header-voltage-mv) measured_mv=$2 ;;
				--voltage-method) voltage_method=$2 ;;
			esac
			shift 2
			;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
if [ -n "$measured_mv" ]; then
	case "$measured_mv" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
	[ -n "$voltage_method" ] || { usage >&2; exit 2; }
fi

bhp_evidence_begin FRDM-GATE-PIR frdm_j18_gpio_inventory

# Raw inventories. debugfs is optional; gpioinfo (libgpiod) is the fallback.
pinmux_file=
for candidate_file in "$(bhp_evidence_path /sys/kernel/debug/pinctrl)"/*/pinmux-pins; do
	[ -r "$candidate_file" ] || continue
	grep -q 'MX8MP_IOMUXC_' "$candidate_file" || continue
	pinmux_file=$candidate_file
	break
done
if [ -n "$pinmux_file" ]; then
	bhp_evidence_capture pinmux_pins cat "$pinmux_file"
	bhp_evidence_check pinmux_inventory "readable i.MX 8M Plus pinmux-pins" "${pinmux_file#"$BHP_EVIDENCE_ROOT"}" pass
else
	bhp_evidence_check pinmux_inventory "readable i.MX 8M Plus pinmux-pins" \
		"missing (mount -t debugfs none /sys/kernel/debug)" unobserved
fi
gpio_debug_file=$(bhp_evidence_path /sys/kernel/debug/gpio)
if [ -r "$gpio_debug_file" ]; then
	bhp_evidence_capture gpio_debugfs cat "$gpio_debug_file"
elif [ -z "$BHP_EVIDENCE_ROOT" ] && command -v gpioinfo >/dev/null 2>&1; then
	bhp_evidence_capture gpioinfo gpioinfo
fi
regulator_capture=$BHP_EVIDENCE_ARTIFACTS/regulators.txt
for regulator_directory in "$(bhp_evidence_path /sys/class/regulator)"/regulator.*; do
	[ -d "$regulator_directory" ] || continue
	printf '%s name=%s microvolts=%s state=%s\n' "${regulator_directory##*/}" \
		"$(sed -n '1p' "$regulator_directory/name" 2>/dev/null)" \
		"$(sed -n '1p' "$regulator_directory/microvolts" 2>/dev/null)" \
		"$(sed -n '1p' "$regulator_directory/state" 2>/dev/null)"
done >"$regulator_capture"
bhp_evidence_register_artifact regulators.txt 0

if [ -n "$pinmux_file" ]; then
	unclaimed_count=$(grep -c '(MUX UNCLAIMED) (GPIO UNCLAIMED)' "$pinmux_file" || true)
	bhp_evidence_observe unclaimed_pad_count "$unclaimed_count"
fi

# Resolves a DT label (gpio4, pwm3, ...) to its node path via __symbols__.
symbol_path()
{
	bhp_evidence_read_dt_string "/proc/device-tree/__symbols__/$1"
}

# Prints the DT label whose node has unit address <address> (reverse __symbols__).
label_for_unit_address()
{
	for symbol_file in "$(bhp_evidence_path /proc/device-tree/__symbols__)"/*; do
		[ -f "$symbol_file" ] || continue
		case "$(tr -d '\000' <"$symbol_file")" in
			*"@$1") printf '%s\n' "${symbol_file##*/}"; return 0 ;;
		esac
	done
	return 1
}

if [ -z "$candidate" ]; then
	bhp_evidence_check candidate_pin "a J18 pin paired with a native GPIO pad" "not provided" \
		unobserved "pass --candidate J18-<pin>:<PAD_MUX> from the schematic header table"
else
	header_pin=${candidate%%:*}
	pad_mux=${candidate#*:}
	pad_name=${pad_mux%%__*}
	gpio_function=${pad_mux##*__}
	valid=1
	case "$header_pin" in J18-[1-9]|J18-[1-3][0-9]|J18-40) ;; *) valid=0 ;; esac
	case "$pad_mux" in MX8MP_IOMUXC_*__GPIO[1-5]_IO[0-9][0-9]) ;; *) valid=0 ;; esac
	if [ "$valid" -eq 0 ]; then
		bhp_evidence_check candidate_format "J18-<1..40>:MX8MP_IOMUXC_<PAD>__GPIO<1-5>_IO<nn>" \
			"$candidate" fail "only native SoC GPIO banks 1-5 can wake suspend-to-RAM via the GPC"
	else
		bank=${gpio_function#GPIO}
		bank=${bank%%_*}
		line=${gpio_function##*_IO}
		line=${line#0}
		controller=gpio$bank
		bhp_evidence_check candidate_format "native SoC GPIO pad" "$pad_mux" pass \
			"not the PCAL6416A expander"
		controller_path=$(symbol_path "$controller" || true)
		if [ -n "$controller_path" ]; then
			status=pass
		elif [ -d "$(bhp_evidence_path /proc/device-tree/__symbols__)" ]; then
			status=fail
		else
			status=unobserved
		fi
		bhp_evidence_check gpio_controller_present "$controller in __symbols__" \
			"${controller_path:-absent}" "$status"

		pad_line=
		[ -n "$pinmux_file" ] && pad_line=$(grep -F "($pad_name)" "$pinmux_file" | sed -n '1p')
		pad_owner=unobserved
		owner_label=
		if [ -n "$pad_line" ]; then
			case "$pad_line" in
				*'(MUX UNCLAIMED)'*) pad_owner=none ;;
				*)
					pad_owner=$(printf '%s\n' "$pad_line" | sed 's/^pin [0-9]* ([^)]*): \([^ ]*\).*/\1/')
					owner_label=$(label_for_unit_address "${pad_owner%%.*}" || true)
					;;
			esac
		fi
		case "$pad_owner" in
			none)
				bhp_evidence_check pad_mux_owner "unclaimed, or a node the overlay disables" none pass
				bhp_evidence_candidate BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE none "pinmux-pins: $pad_line"
				;;
			unobserved)
				bhp_evidence_check pad_mux_owner "unclaimed, or a node the overlay disables" \
					"pad not found in pinmux-pins" unobserved
				;;
			*)
				if [ -n "$owner_label" ]; then
					bhp_evidence_check pad_mux_owner "unclaimed, or a node the overlay disables" \
						"$pad_owner (&$owner_label)" pass \
						"review: disabling &$owner_label must not remove a function this assembly uses"
					bhp_evidence_candidate BHP_BOARD_PIR_PAD_OWNER_TO_DISABLE "$owner_label" \
						"pinmux-pins: $pad_line"
				else
					bhp_evidence_check pad_mux_owner "unclaimed, or a node the overlay disables" \
						"$pad_owner (no __symbols__ label)" fail \
						"the overlay can only disable a labelled node"
				fi
				;;
		esac

		gpio_consumer=unobserved
		if [ -r "$gpio_debug_file" ] && [ -n "$controller_path" ]; then
			controller_address=${controller_path##*@}
			gpio_consumer=$(awk -v address="$controller_address" -v line="$line" '
				/^gpiochip/ { in_chip = (index($0, address ".gpio") > 0); base = -1
					if (in_chip && match($0, /GPIOs [0-9]+/)) base = substr($0, RSTART + 6, RLENGTH - 6) + 0
					next }
				in_chip && base >= 0 && $1 == "gpio-" (base + line) {
					if (match($0, /\|[^)]*\)/)) { value = substr($0, RSTART + 1, RLENGTH - 2); gsub(/^ +| +$/, "", value); print value; found = 1; exit }
				}
				END { if (!found) print "none" }' "$gpio_debug_file")
		fi
		case "$gpio_consumer" in
			none|'') status=pass; gpio_consumer=none ;;
			unobserved) status=unobserved ;;
			"$(bhp_evidence_profile_value BHP_BOARD_PIR_WAKE_INTERRUPT_LABEL)") status=pass ;;
			*) status=fail ;;
		esac
		bhp_evidence_check gpio_line_consumer "none (or the project PIR node)" \
			"$controller line $line: $gpio_consumer" "$status" "/sys/kernel/debug/gpio"

		bhp_evidence_candidate BHP_BOARD_PIR_HEADER_PIN "$header_pin" "operator-supplied schematic pairing"
		bhp_evidence_candidate BHP_BOARD_PIR_PAD_MUX "$pad_mux" "operator-supplied schematic pairing"
		bhp_evidence_candidate BHP_BOARD_PIR_GPIO_CONTROLLER "$controller" "derived from $pad_mux"
		bhp_evidence_candidate BHP_BOARD_PIR_GPIO_LINE "$line" "derived from $pad_mux"
	fi
fi

# Header voltage: the PIR output is 3.3 V, so the pad bank must be 3.3 V. A
# 1.8 V bank would be overdriven and must not be used.
if [ -z "$measured_mv" ]; then
	bhp_evidence_check header_io_voltage "3300 mV (+/-10 %)" "not measured" unobserved \
		"measure J18 I/O voltage with a meter and pass --measured-header-voltage-mv"
else
	if [ "$measured_mv" -ge 2970 ] && [ "$measured_mv" -le 3630 ]; then
		status=pass
		bhp_evidence_candidate BHP_BOARD_HEADER_IO_VOLTAGE_MV 3300 \
			"operator measurement ${measured_mv} mV via $voltage_method"
	else
		status=fail
		case "$measured_mv" in
			1[6-9][0-9][0-9]) bhp_evidence_candidate BHP_BOARD_HEADER_IO_VOLTAGE_MV 1800 \
				"operator measurement ${measured_mv} mV via $voltage_method; 3.3 V PIR needs a level shifter" ;;
		esac
	fi
	bhp_evidence_check header_io_voltage "3300 mV (+/-10 %)" "${measured_mv} mV ($voltage_method)" \
		"$status" "operator attestation"
fi

bhp_evidence_finish
