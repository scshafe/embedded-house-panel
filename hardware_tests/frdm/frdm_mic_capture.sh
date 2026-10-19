#!/bin/sh
# FRDM-GATE-AUDIO evidence: ALSA card identity and a bounded capture with
# signal statistics, from one of two sources:
#   --source headset (default): the WM8962 codec and the CTIA headset
#     microphone in the 3.5 mm jack, S16_LE 48 kHz stereo (bring-up only);
#   --source array: the USB XVF3800 array (USB vendor 2886), S16_LE 16 kHz
#     6 channels, the one capture mode its 6-channel firmware offers, with
#     per-channel statistics.
# Inputs: --duration-seconds N (1..30) [--source headset|array]; common options
#   (see --help). Speak or tap near the microphone during the window.
# Output: hardware_tests/results/frdm/frdm-gate-audio/<UTC>-frdm_mic_capture.json
#   plus capture.wav and the full amixer state as artifacts.
# Side effects: none on the system; mixer controls are read, never changed. A
#   silent capture means routing needs an operator decision (see the amixer
#   artifact), not an automatic mixer change.
# Exit: 0 evidence written (read `result`); 1 setup failure; 2 usage.

set -u

BHP_EVIDENCE_SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=lib/frdm_evidence.sh
. "$BHP_EVIDENCE_SCRIPT_DIRECTORY/lib/frdm_evidence.sh"

usage()
{
	printf '%s\n' "Usage: $0 --duration-seconds N [--source headset|array] [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
duration=
source=headset
set -f
IFS='
'
# shellcheck disable=SC2086
set -- $BHP_EVIDENCE_REMAINING_ARGUMENTS
unset IFS
set +f
while [ "$#" -gt 0 ]; do
	case "$1" in
		--duration-seconds) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; duration=$2; shift 2 ;;
		--source) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; source=$2; shift 2 ;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
case "$duration" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 30 ]; then usage >&2; exit 2; fi
case "$source" in headset|array) ;; *) usage >&2; exit 2 ;; esac

# Records CHANNELS-channel S16_LE at RATE Hz from DEVICE for $duration seconds,
# then checks the loudest channel (by RMS) against the silence thresholds.
capture_and_measure()
{
	capture_device=$1
	rate=$2
	channels=$3
	mode="$capture_device S16_LE $rate Hz $channels ch"
	wav_file=$BHP_EVIDENCE_ARTIFACTS/capture.wav
	printf '%s\n' "Recording ${duration}s from $capture_device: speak near the microphone now." >&2
	if arecord -D "$capture_device" -f S16_LE -r "$rate" -c "$channels" -d "$duration" "$wav_file" \
		>"$BHP_EVIDENCE_ARTIFACTS/arecord.txt" 2>&1; then
		bhp_evidence_check capture_opened "$mode" "captured" pass
	else
		bhp_evidence_check capture_opened "$mode" \
			"$(sed -n '1p' "$BHP_EVIDENCE_ARTIFACTS/arecord.txt")" fail
	fi
	bhp_evidence_register_artifact arecord.txt 0
	if [ ! -s "$wav_file" ]; then
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "no samples" fail
		return
	fi
	bhp_evidence_register_artifact capture.wav 0
	# Canonical 44-byte WAV header from arecord; samples are interleaved, so
	# sample n belongs to channel (n mod channels) + 1. One output line per
	# channel: channel, sample count, peak dBFS, RMS dBFS.
	od -An -v -td2 -j44 "$wav_file" | awk -v channels="$channels" '
		{ for (i = 1; i <= NF; i++) { c = n % channels + 1; n++; value = $i
			if (value < 0) value = -value
			if (value > peak[c]) peak[c] = value; sum[c] += $i * $i; count[c]++ } }
		END {
			for (c = 1; c <= channels; c++) {
				if (count[c] == 0) { printf "%d 0 -999.0 -999.0\n", c; continue }
				rms = sqrt(sum[c] / count[c])
				peak_db = (peak[c] > 0) ? 20 * log(peak[c] / 32768) / log(10) : -999
				rms_db = (rms > 0) ? 20 * log(rms / 32768) / log(10) : -999
				printf "%d %d %.1f %.1f\n", c, count[c], peak_db, rms_db
			}
		}' >"$BHP_EVIDENCE_WORK/channel-statistics"
	sample_count=0
	loudest=
	while read -r channel count peak_db rms_db; do
		sample_count=$((sample_count + count))
		if [ "$channels" -gt 1 ]; then
			bhp_evidence_observe "channel_${channel}_peak_dbfs" "$peak_db"
			bhp_evidence_observe "channel_${channel}_rms_dbfs" "$rms_db"
		fi
		if [ -z "$loudest" ] || awk -v a="$rms_db" -v b="$loudest_rms" 'BEGIN { exit !(a > b) }'; then
			loudest=$channel
			loudest_peak=$peak_db
			loudest_rms=$rms_db
		fi
	done <"$BHP_EVIDENCE_WORK/channel-statistics"
	bhp_evidence_observe sample_count "$sample_count"
	bhp_evidence_observe peak_dbfs "$loudest_peak"
	bhp_evidence_observe rms_dbfs "$loudest_rms"
	[ "$channels" -gt 1 ] && bhp_evidence_observe loudest_channel "$loudest"
	if awk -v peak="$loudest_peak" -v rms="$loudest_rms" 'BEGIN { exit !(peak > -50 && rms > -70) }'; then
		status=pass
		bhp_evidence_candidate BHP_BOARD_AUDIO_CAPTURE_DEVICE "$capture_device" \
			"non-silent capture: peak $loudest_peak dBFS, RMS $loudest_rms dBFS"
	else
		status=fail
	fi
	bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" \
		"peak $loudest_peak dBFS, RMS $loudest_rms dBFS" "$status" \
		"thresholds are a silence detector, not an audio-quality claim"
}

# The headset path: the WM8962 codec on I2C and its ALSA card.
headset()
{
	expected_card=$(bhp_evidence_profile_value BHP_BOARD_AUDIO_EXPECTED_CARD_ID)
	expected_address=$(bhp_evidence_profile_value BHP_BOARD_AUDIO_EXPECTED_CODEC_I2C_ADDRESS)
	bhp_evidence_observe mic_source "$(bhp_evidence_profile_value BHP_BOARD_AUDIO_MIC_SOURCE)"

	cards_file=$(bhp_evidence_path /proc/asound/cards)
	card_id=
	if [ -r "$cards_file" ]; then
		bhp_evidence_capture asound_cards cat "$cards_file"
		card_id=$(sed -n 's/^ *[0-9][0-9]* \[\([^] ]*\) *\]:.*/\1/p' "$cards_file" |
			grep -x -F "$expected_card" | sed -n '1p')
	fi
	if [ -n "$card_id" ]; then
		bhp_evidence_check alsa_card "$expected_card" "$card_id" pass "/proc/asound/cards"
	else
		bhp_evidence_check alsa_card "$expected_card" "absent" fail "check imx-wm8962 machine driver in dmesg"
	fi

	codec_client=
	for client_directory in "$(bhp_evidence_path /sys/bus/i2c/devices)"/*-00"${expected_address#0x}"; do
		[ -d "$client_directory" ] || continue
		[ "$(sed -n '1p' "$client_directory/name" 2>/dev/null)" = wm8962 ] || continue
		codec_client=${client_directory##*/}
	done
	if [ -n "$codec_client" ]; then
		bhp_evidence_check codec_i2c "wm8962 at $expected_address" "$codec_client" pass
	else
		bhp_evidence_check codec_i2c "wm8962 at $expected_address" absent fail
	fi
	jack_inputs=$(grep '^N: Name=' "$(bhp_evidence_path /proc/bus/input/devices)" 2>/dev/null |
		grep -i 'jack' | sed 's/^N: Name=//' | tr '\n' ';')
	bhp_evidence_observe jack_input_devices "${jack_inputs:-none}"

	if [ -z "$card_id" ]; then
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "no card" unobserved
		return
	fi
	if [ -n "$BHP_EVIDENCE_ROOT" ]; then
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "fixture: no capture" unobserved
		return
	fi
	command -v amixer >/dev/null 2>&1 && bhp_evidence_capture amixer_contents amixer -c "$card_id" contents
	capture_and_measure "hw:CARD=$card_id,DEV=0" 48000 2
}

# The USB array: a card whose usbid has vendor 2886 and whose name says
# XVF3800, and the capture mode its stream0 file offers.
array()
{
	bhp_evidence_observe mic_source xvf3800-usb-array
	cards_file=$(bhp_evidence_path /proc/asound/cards)
	[ -r "$cards_file" ] && bhp_evidence_capture asound_cards cat "$cards_file"
	card_id=
	card_directory=
	usb_id=
	for directory in "$(bhp_evidence_path /proc/asound)"/card[0-9]*; do
		[ -r "$directory/usbid" ] || continue
		candidate_usb_id=$(sed -n '1p' "$directory/usbid")
		case "$candidate_usb_id" in 2886:*) ;; *) continue ;; esac
		index=${directory##*/card}
		grep -q "^ *$index \[.*XVF3800" "$cards_file" 2>/dev/null || continue
		card_id=$(sed -n '1p' "$directory/id")
		card_directory=$directory
		usb_id=$candidate_usb_id
		break
	done
	if [ -n "$card_id" ]; then
		bhp_evidence_check alsa_card "USB vendor 2886, XVF3800 in the card name" "$card_id (usb $usb_id)" pass \
			"/proc/asound/cards and usbid"
		bhp_evidence_observe usb_id "$usb_id"
		bhp_evidence_capture usb_stream cat "$card_directory/stream0"
		[ -r "$card_directory/usbbus" ] && bhp_evidence_observe usb_bus "$(sed -n '1p' "$card_directory/usbbus")"
	else
		bhp_evidence_check alsa_card "USB vendor 2886, XVF3800 in the card name" absent fail \
			"is the array on a USB-A port?"
		bhp_evidence_check capture_mode "S16_LE 6 ch 16000 Hz" "no card" unobserved
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "no card" unobserved
		return
	fi

	# The Capture section of stream0: its Format, Channels and Rates lines.
	capture_section=$(awk '/^Capture:/ { on = 1; next } /^[A-Z][a-z]*:$/ { on = 0 } on' "$card_directory/stream0")
	format=$(printf '%s\n' "$capture_section" | sed -n 's/^ *Format: *//p' | sed -n '1p')
	channels=$(printf '%s\n' "$capture_section" | sed -n 's/^ *Channels: *//p' | sed -n '1p')
	rates=$(printf '%s\n' "$capture_section" | sed -n 's/^ *Rates: *//p' | sed -n '1p')
	observed_mode="$format $channels ch $rates Hz"
	if [ "$format" = S16_LE ] && [ "$channels" = 6 ] && printf '%s\n' "$rates" | grep -q -w 16000; then
		bhp_evidence_check capture_mode "S16_LE 6 ch 16000 Hz" "$observed_mode" pass "stream0 Capture"
	else
		bhp_evidence_check capture_mode "S16_LE 6 ch 16000 Hz" "$observed_mode" fail \
			"another firmware mode; record it before changing anything"
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "mode mismatch" unobserved
		return
	fi
	if [ -n "$BHP_EVIDENCE_ROOT" ]; then
		bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "fixture: no capture" unobserved
		return
	fi
	command -v amixer >/dev/null 2>&1 && bhp_evidence_capture amixer_contents amixer -c "$card_id" contents
	capture_and_measure "hw:CARD=$card_id,DEV=0" 16000 6
}

bhp_evidence_begin FRDM-GATE-AUDIO frdm_mic_capture
bhp_evidence_observe source "$source"
case "$source" in
	headset) headset ;;
	array) array ;;
esac
bhp_evidence_finish
