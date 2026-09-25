#!/bin/sh
# FRDM-GATE-AUDIO evidence: WM8962 ALSA card identity and a bounded capture
# from the CTIA headset microphone in the 3.5 mm jack, with signal statistics.
# Inputs: --duration-seconds N (1..30); common options (see --help). Speak or
#   tap near the headset microphone during the window.
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
	printf '%s\n' "Usage: $0 --duration-seconds N [common options]"
	bhp_evidence_usage_common
}

bhp_evidence_parse_common_arguments "$@" || { usage >&2; exit 2; }
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
		--duration-seconds) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; duration=$2; shift 2 ;;
		--help|-h) usage; exit 0 ;;
		*) usage >&2; exit 2 ;;
	esac
done
case "$duration" in ''|*[!0-9]*) usage >&2; exit 2 ;; esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 30 ]; then usage >&2; exit 2; fi

bhp_evidence_begin FRDM-GATE-AUDIO frdm_mic_capture
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
	bhp_evidence_finish
	exit 0
fi
if [ -n "$BHP_EVIDENCE_ROOT" ]; then
	bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "fixture: no capture" unobserved
	bhp_evidence_finish
	exit 0
fi
command -v amixer >/dev/null 2>&1 && bhp_evidence_capture amixer_contents amixer -c "$card_id" contents
capture_device=hw:CARD=$card_id,DEV=0
wav_file=$BHP_EVIDENCE_ARTIFACTS/capture.wav
printf '%s\n' "Recording ${duration}s from $capture_device: speak into the headset microphone now." >&2
if arecord -D "$capture_device" -f S16_LE -r 48000 -c 2 -d "$duration" "$wav_file" \
	>"$BHP_EVIDENCE_ARTIFACTS/arecord.txt" 2>&1; then
	bhp_evidence_check capture_opened "$capture_device S16_LE 48 kHz stereo" "captured" pass
else
	bhp_evidence_check capture_opened "$capture_device S16_LE 48 kHz stereo" \
		"$(sed -n '1p' "$BHP_EVIDENCE_ARTIFACTS/arecord.txt")" fail
fi
bhp_evidence_register_artifact arecord.txt 0
if [ -s "$wav_file" ]; then
	bhp_evidence_register_artifact capture.wav 0
	# Canonical 44-byte WAV header from arecord; statistics over all channels.
	statistics=$(od -An -v -td2 -j44 "$wav_file" | awk '
		{ for (i = 1; i <= NF; i++) { value = $i; if (value < 0) value = -value
			if (value > peak) peak = value; sum += $i * $i; count++ } }
		END {
			if (count == 0) { print "0 -inf -inf"; exit }
			rms = sqrt(sum / count)
			peak_db = (peak > 0) ? 20 * log(peak / 32768) / log(10) : -999
			rms_db = (rms > 0) ? 20 * log(rms / 32768) / log(10) : -999
			printf "%d %.1f %.1f\n", count, peak_db, rms_db
		}')
	sample_count=${statistics%% *}
	peak_db=$(printf '%s' "$statistics" | awk '{ print $2 }')
	rms_db=$(printf '%s' "$statistics" | awk '{ print $3 }')
	bhp_evidence_observe sample_count "$sample_count"
	bhp_evidence_observe peak_dbfs "$peak_db"
	bhp_evidence_observe rms_dbfs "$rms_db"
	if awk -v peak="$peak_db" -v rms="$rms_db" 'BEGIN { exit !(peak > -50 && rms > -70) }'; then
		status=pass
		bhp_evidence_candidate BHP_BOARD_AUDIO_CAPTURE_DEVICE "$capture_device" \
			"non-silent capture: peak $peak_db dBFS, RMS $rms_db dBFS"
	else
		status=fail
	fi
	bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" \
		"peak $peak_db dBFS, RMS $rms_db dBFS" "$status" \
		"thresholds are a silence detector, not an audio-quality claim"
else
	bhp_evidence_check capture_non_silent "peak > -50 dBFS and RMS > -70 dBFS" "no samples" fail
fi
bhp_evidence_finish
