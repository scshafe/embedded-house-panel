#!/bin/sh
# @canonical frdm_evidence_library -- shared CLI, profile, and JSON evidence writer
# for hardware_tests/frdm/*.sh. POSIX sh + BusyBox only: the scripts run on the
# NXP vendor image and on the project bring-up image, neither of which has Python.
#
# A script sources this file after setting BHP_EVIDENCE_SCRIPT_DIRECTORY, then:
#   bhp_evidence_parse_common_arguments "$@"   (sets BHP_EVIDENCE_REMAINING_ARGUMENTS)
#   bhp_evidence_begin GATE SCRIPT_NAME
#   bhp_evidence_check ID EXPECTED OBSERVED STATUS [BASIS]
#   bhp_evidence_observe KEY VALUE
#   bhp_evidence_candidate PROFILE_KEY VALUE BASIS
#   bhp_evidence_capture NAME COMMAND...     (raw text artifact, hashed)
#   bhp_evidence_finish                     (writes JSON, prints its path)
#
# Output contract: hardware_tests/schemas/frdm_gate_evidence.schema.json.
# Evidence never closes a gate by itself: `gate_closed` is always false. A
# reviewed commit copies candidates into config/boards/<board>.env and links
# the evidence file.
# shellcheck disable=SC2034
# Variables are consumed by the scripts that source this library.

BHP_EVIDENCE_PROJECT_ROOT=$(CDPATH='' cd -- "$BHP_EVIDENCE_SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../../../scripts/structured_logging/structured_log.sh
. "$BHP_EVIDENCE_PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
# shellcheck source=../../../scripts/boards/board_profile.sh
. "$BHP_EVIDENCE_PROJECT_ROOT/scripts/boards/board_profile.sh"
# shellcheck source=../../../scripts/file_integrity/sha256_file.sh
. "$BHP_EVIDENCE_PROJECT_ROOT/scripts/file_integrity/sha256_file.sh"

BHP_EVIDENCE_INSTALLED_PROFILE=/usr/share/beaglebone-house-panel/board-profile.env
BHP_EVIDENCE_SCHEMA=bhp.frdm_gate_evidence.v1
BHP_EVIDENCE_OUTPUT_ROOT=$BHP_EVIDENCE_PROJECT_ROOT/hardware_tests/results/frdm
BHP_EVIDENCE_PROFILE_PATH=
# Test-only: a fixture tree standing in for / (see hardware_tests/tests/).
BHP_EVIDENCE_ROOT=
BHP_EVIDENCE_REMAINING_ARGUMENTS=
BHP_EVIDENCE_FAIL_COUNT=0
BHP_EVIDENCE_UNOBSERVED_COUNT=0
BHP_EVIDENCE_PASS_COUNT=0

bhp_evidence_usage_common()
{
	printf '%s\n' \
		'Common options:' \
		'  --output-directory DIR  evidence root (default: hardware_tests/results/frdm)' \
		'  --profile FILE          board profile (default: the image-installed profile,' \
		'                          else config/boards/<active>.env)' \
		'  --fixture-root DIR      test only: read DIR as if it were /'
}

# Consumes the common options and leaves the rest, newline-separated and in
# order, in BHP_EVIDENCE_REMAINING_ARGUMENTS. Values must not contain newlines.
bhp_evidence_parse_common_arguments()
{
	BHP_EVIDENCE_REMAINING_ARGUMENTS=
	while [ "$#" -gt 0 ]; do
		case "$1" in
			--output-directory|--profile|--fixture-root)
				[ "$#" -ge 2 ] || return 2
				case "$1" in
					--output-directory) BHP_EVIDENCE_OUTPUT_ROOT=$2 ;;
					--profile) BHP_EVIDENCE_PROFILE_PATH=$2 ;;
					--fixture-root) BHP_EVIDENCE_ROOT=$2 ;;
				esac
				shift 2
				;;
			*)
				BHP_EVIDENCE_REMAINING_ARGUMENTS="$BHP_EVIDENCE_REMAINING_ARGUMENTS$1
"
				shift
				;;
		esac
	done
	if [ -n "$BHP_EVIDENCE_ROOT" ]; then
		case "$BHP_EVIDENCE_ROOT" in
			/*) ;;
			*) return 2 ;;
		esac
		[ -d "$BHP_EVIDENCE_ROOT" ] || return 2
	fi
	return 0
}

# Prints the host path for a target path, honouring --fixture-root.
bhp_evidence_path()
{
	printf '%s%s\n' "$BHP_EVIDENCE_ROOT" "$1"
}

# Prints the first line of a target file, or nothing when it is unreadable.
bhp_evidence_read_line()
{
	bhp_evidence_read_path=$(bhp_evidence_path "$1")
	[ -r "$bhp_evidence_read_path" ] || return 1
	sed -n '1p' "$bhp_evidence_read_path" | tr -d '\000'
}

# Prints a device-tree string property (NUL-separated list joined by commas).
bhp_evidence_read_dt_string()
{
	bhp_evidence_dt_path=$(bhp_evidence_path "$1")
	[ -r "$bhp_evidence_dt_path" ] || return 1
	tr '\000' ',' <"$bhp_evidence_dt_path" | sed 's/,$//'
}

# Prints the canonical target path a symlink resolves to (fixture prefix removed).
bhp_evidence_resolve()
{
	bhp_evidence_resolve_path=$(bhp_evidence_path "$1")
	bhp_evidence_resolved=$(readlink -f "$bhp_evidence_resolve_path" 2>/dev/null) || return 1
	[ -n "$bhp_evidence_resolved" ] || return 1
	printf '%s\n' "${bhp_evidence_resolved#"$BHP_EVIDENCE_ROOT"}"
}

bhp_evidence_load_profile()
{
	if [ -n "$BHP_EVIDENCE_PROFILE_PATH" ]; then
		bhp_board_profile_load_file "$BHP_EVIDENCE_PROFILE_PATH" || return 1
	elif [ -z "$BHP_EVIDENCE_ROOT" ] && [ -r "$BHP_EVIDENCE_INSTALLED_PROFILE" ]; then
		BHP_EVIDENCE_PROFILE_PATH=$BHP_EVIDENCE_INSTALLED_PROFILE
		bhp_board_profile_load_file "$BHP_EVIDENCE_PROFILE_PATH" || return 1
	else
		bhp_board_profile_load_selected "$BHP_EVIDENCE_PROJECT_ROOT" || return 1
		BHP_EVIDENCE_PROFILE_PATH=$BHP_BOARD_PROFILE_LOADED_PATH
	fi
}

# Prints a profile value or an empty string (for EXPECTED_* comparisons).
bhp_evidence_profile_value()
{
	bhp_board_profile_value "$1" 2>/dev/null || true
}

bhp_evidence_begin()
{
	BHP_EVIDENCE_GATE=$1
	BHP_EVIDENCE_SCRIPT=$2
	BHP_EVIDENCE_TRACE_ID=$(bhp_new_trace_id)
	BHP_SCRIPT_SERVICE_NAME=hardware_tests.frdm.$BHP_EVIDENCE_SCRIPT
	export BHP_SCRIPT_SERVICE_NAME
	if ! bhp_evidence_load_profile; then
		bhp_fail "$BHP_BOARD_PROFILE_ERROR_CODE" "$BHP_BOARD_PROFILE_ERROR_DETAIL" \
			"$BHP_EVIDENCE_TRACE_ID" >&2
		exit 1
	fi
	BHP_EVIDENCE_STARTED_AT=$(date -u +%Y-%m-%dT%H:%M:%SZ)
	bhp_evidence_stamp=$(date -u +%Y%m%dT%H%M%SZ)
	bhp_evidence_gate_directory=$BHP_EVIDENCE_OUTPUT_ROOT/$(printf '%s' "$BHP_EVIDENCE_GATE" |
		tr '[:upper:]' '[:lower:]')
	# Same-second runs get -2, -3, ...; an existing record is never replaced.
	bhp_evidence_suffix=
	bhp_evidence_attempt=1
	while :; do
		BHP_EVIDENCE_JSON=$bhp_evidence_gate_directory/$bhp_evidence_stamp-$BHP_EVIDENCE_SCRIPT$bhp_evidence_suffix.json
		BHP_EVIDENCE_ARTIFACTS=$bhp_evidence_gate_directory/$bhp_evidence_stamp-$BHP_EVIDENCE_SCRIPT$bhp_evidence_suffix
		[ -e "$BHP_EVIDENCE_JSON" ] || [ -e "$BHP_EVIDENCE_ARTIFACTS" ] || break
		bhp_evidence_attempt=$((bhp_evidence_attempt + 1))
		if [ "$bhp_evidence_attempt" -gt 99 ]; then
			bhp_fail FRDM_EVIDENCE.OUTPUT_EXISTS "$BHP_EVIDENCE_JSON" "$BHP_EVIDENCE_TRACE_ID" >&2
			exit 1
		fi
		bhp_evidence_suffix=-$bhp_evidence_attempt
	done
	BHP_EVIDENCE_PASS_COUNT=0
	BHP_EVIDENCE_FAIL_COUNT=0
	BHP_EVIDENCE_UNOBSERVED_COUNT=0
	mkdir -p "$BHP_EVIDENCE_ARTIFACTS" || {
		bhp_fail FRDM_EVIDENCE.OUTPUT_UNWRITABLE "$BHP_EVIDENCE_ARTIFACTS" \
			"$BHP_EVIDENCE_TRACE_ID" >&2
		exit 1
	}
	BHP_EVIDENCE_WORK=$BHP_EVIDENCE_ARTIFACTS/.records
	mkdir -p "$BHP_EVIDENCE_WORK"
	: >"$BHP_EVIDENCE_WORK/checks"
	: >"$BHP_EVIDENCE_WORK/observations"
	: >"$BHP_EVIDENCE_WORK/candidates"
	: >"$BHP_EVIDENCE_WORK/artifacts"
	: >"$BHP_EVIDENCE_WORK/actions"
	bhp_log info evidence_started "$BHP_EVIDENCE_TRACE_ID" gate "$BHP_EVIDENCE_GATE" \
		board_id "$BHP_BOARD_ID" profile "$BHP_EVIDENCE_PROFILE_PATH" >&2
}

# STATUS: pass | fail | unobserved | not_applicable.
bhp_evidence_check()
{
	case "$4" in
		pass) BHP_EVIDENCE_PASS_COUNT=$((BHP_EVIDENCE_PASS_COUNT + 1)) ;;
		fail) BHP_EVIDENCE_FAIL_COUNT=$((BHP_EVIDENCE_FAIL_COUNT + 1)) ;;
		unobserved) BHP_EVIDENCE_UNOBSERVED_COUNT=$((BHP_EVIDENCE_UNOBSERVED_COUNT + 1)) ;;
		not_applicable) ;;
		*) printf '%s\n' "FRDM_EVIDENCE.CHECK_STATUS_INVALID: $4" >&2; exit 1 ;;
	esac
	printf '{"id":"%s","expected":"%s","observed":"%s","status":"%s","basis":"%s"}\n' \
		"$(bhp_json_escape "$1")" "$(bhp_json_escape "$2")" "$(bhp_json_escape "$3")" \
		"$4" "$(bhp_json_escape "${5:-}")" >>"$BHP_EVIDENCE_WORK/checks"
}

bhp_evidence_observe()
{
	printf '{"key":"%s","value":"%s"}\n' "$(bhp_json_escape "$1")" \
		"$(bhp_json_escape "$2")" >>"$BHP_EVIDENCE_WORK/observations"
}

# A candidate is a value a reviewer may copy into the profile; never automatic.
bhp_evidence_candidate()
{
	printf '{"profile_key":"%s","candidate_value":"%s","basis":"%s","current_profile_value":"%s"}\n' \
		"$(bhp_json_escape "$1")" "$(bhp_json_escape "$2")" "$(bhp_json_escape "$3")" \
		"$(bhp_json_escape "$(bhp_evidence_profile_value "$1")")" \
		>>"$BHP_EVIDENCE_WORK/candidates"
}

# Records a system-changing action taken only because an --execute flag was given.
bhp_evidence_action()
{
	printf '{"action":"%s","detail":"%s","at_utc":"%s"}\n' "$(bhp_json_escape "$1")" \
		"$(bhp_json_escape "$2")" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
		>>"$BHP_EVIDENCE_WORK/actions"
}

# Runs COMMAND and stores its stdout+stderr as a hashed raw artifact.
bhp_evidence_capture()
{
	bhp_evidence_capture_name=$1
	shift
	bhp_evidence_capture_file=$BHP_EVIDENCE_ARTIFACTS/$bhp_evidence_capture_name.txt
	"$@" >"$bhp_evidence_capture_file" 2>&1
	bhp_evidence_capture_status=$?
	bhp_evidence_register_artifact "$bhp_evidence_capture_name.txt" "$bhp_evidence_capture_status"
	return "$bhp_evidence_capture_status"
}

bhp_evidence_register_artifact()
{
	bhp_evidence_artifact_path=$BHP_EVIDENCE_ARTIFACTS/$1
	printf '{"path":"%s","sha256":"%s","command_exit_status":%s}\n' \
		"$(bhp_json_escape "${BHP_EVIDENCE_ARTIFACTS##*/}/$1")" \
		"$(bhp_sha256_file "$bhp_evidence_artifact_path")" "${2:-0}" \
		>>"$BHP_EVIDENCE_WORK/artifacts"
}

bhp_evidence_json_array()
{
	printf '['
	awk 'NR > 1 { printf "," } { printf "%s", $0 }' "$1"
	printf ']'
}

bhp_evidence_finish()
{
	if [ "$BHP_EVIDENCE_FAIL_COUNT" -gt 0 ]; then
		bhp_evidence_result=mismatch
	elif [ "$BHP_EVIDENCE_UNOBSERVED_COUNT" -gt 0 ]; then
		bhp_evidence_result=incomplete
	else
		bhp_evidence_result=complete
	fi
	bhp_evidence_image_commit=$(sed -n 's/^project_commit=//p' \
		"$(bhp_evidence_path /etc/beaglebone-house-panel/image-version)" 2>/dev/null)
	bhp_evidence_kernel=$(bhp_evidence_read_line /proc/sys/kernel/osrelease || true)
	bhp_evidence_temporary=$BHP_EVIDENCE_JSON.tmp.$$
	{
		printf '{"schema":"%s","gate":"%s","script":"%s","trace_id":"%s",' \
			"$BHP_EVIDENCE_SCHEMA" "$BHP_EVIDENCE_GATE" "$BHP_EVIDENCE_SCRIPT" \
			"$(bhp_json_escape "$BHP_EVIDENCE_TRACE_ID")"
		printf '"board_profile_id":"%s","board_profile_path":"%s",' \
			"$(bhp_json_escape "$BHP_BOARD_ID")" "$(bhp_json_escape "$BHP_EVIDENCE_PROFILE_PATH")"
		printf '"started_at_utc":"%s","finished_at_utc":"%s",' \
			"$BHP_EVIDENCE_STARTED_AT" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf '"kernel_release":"%s","image_project_commit":"%s","fixture_root":%s,' \
			"$(bhp_json_escape "${bhp_evidence_kernel:-unobserved}")" \
			"$(bhp_json_escape "${bhp_evidence_image_commit:-not_a_project_image}")" \
			"$([ -n "$BHP_EVIDENCE_ROOT" ] && printf true || printf false)"
		printf '"result":"%s","counts":{"pass":%s,"fail":%s,"unobserved":%s},' \
			"$bhp_evidence_result" "$BHP_EVIDENCE_PASS_COUNT" "$BHP_EVIDENCE_FAIL_COUNT" \
			"$BHP_EVIDENCE_UNOBSERVED_COUNT"
		printf '"gate_closed":false,"hardware_evidence":"%s",' \
			"$([ -n "$BHP_EVIDENCE_ROOT" ] && printf fixture_not_evidence || printf captured_unreviewed)"
		printf '"checks":'; bhp_evidence_json_array "$BHP_EVIDENCE_WORK/checks"
		printf ',"observations":'; bhp_evidence_json_array "$BHP_EVIDENCE_WORK/observations"
		printf ',"candidates":'; bhp_evidence_json_array "$BHP_EVIDENCE_WORK/candidates"
		printf ',"actions":'; bhp_evidence_json_array "$BHP_EVIDENCE_WORK/actions"
		printf ',"artifacts":'; bhp_evidence_json_array "$BHP_EVIDENCE_WORK/artifacts"
		printf '}\n'
	} >"$bhp_evidence_temporary"
	mv "$bhp_evidence_temporary" "$BHP_EVIDENCE_JSON"
	for bhp_evidence_record in checks observations candidates artifacts actions; do
		rm -f "$BHP_EVIDENCE_WORK/$bhp_evidence_record"
	done
	rmdir "$BHP_EVIDENCE_WORK" 2>/dev/null || true
	bhp_log info evidence_written "$BHP_EVIDENCE_TRACE_ID" gate "$BHP_EVIDENCE_GATE" \
		result "$bhp_evidence_result" evidence "$BHP_EVIDENCE_JSON" >&2
	printf '%s\n' "$BHP_EVIDENCE_JSON"
}

# Percent-encodes characters outside the board-profile value grammar (RFC 3986 %XX).
bhp_evidence_profile_encode()
{
	printf '%s' "$1" | LC_ALL=C od -An -v -tx1 | LC_ALL=C awk '
		{
			for (field = 1; field <= NF; field++) {
				byte = $field
				decimal = 0
				for (position = 1; position <= 2; position++) {
					digit = index("0123456789abcdef", substr(byte, position, 1)) - 1
					decimal = decimal * 16 + digit
				}
				character = sprintf("%c", decimal)
				if (character ~ /[A-Za-z0-9_.\/:,@+=*-]/) printf "%s", character
				else printf "%%%s", toupper(byte)
			}
		}'
}

# Expands a board-profile %XX value back to text (inverse of the encoder).
bhp_evidence_profile_decode()
{
	printf '%s' "$1" | LC_ALL=C awk '
		{
			line = $0
			output = ""
			while ((position = index(line, "%")) > 0) {
				output = output substr(line, 1, position - 1)
				hex = tolower(substr(line, position + 1, 2))
				decimal = (index("0123456789abcdef", substr(hex, 1, 1)) - 1) * 16 + \
					(index("0123456789abcdef", substr(hex, 2, 1)) - 1)
				output = output sprintf("%c", decimal)
				line = substr(line, position + 3)
			}
			printf "%s", output line
		}'
}
