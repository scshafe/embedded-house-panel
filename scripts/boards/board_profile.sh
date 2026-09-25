#!/bin/sh
# @canonical board_profile_loader -- the only shell reader of config/boards/<id>.env.
# Sourced by build, verification, and on-target evidence scripts. POSIX sh and
# awk only, so it also runs on BusyBox and on the NXP vendor image.
#
# Functions return 0 on success. On failure they return 1 and set
# BHP_BOARD_PROFILE_ERROR_CODE (stable machine code) and
# BHP_BOARD_PROFILE_ERROR_DETAIL (single-line detail); they never print.
# shellcheck disable=SC2034
# Public variables are consumed by scripts that source this library.

BHP_BOARD_PROFILE_ERROR_CODE=
BHP_BOARD_PROFILE_ERROR_DETAIL=
BHP_BOARD_PROFILE_LOADED_ID=
BHP_BOARD_PROFILE_LOADED_PATH=
BHP_BOARD_PROFILE_KEYS=

bhp_board_profile_set_error()
{
	BHP_BOARD_PROFILE_ERROR_CODE=$1
	BHP_BOARD_PROFILE_ERROR_DETAIL=$2
	return 1
}

bhp_board_id_is_valid()
{
	case "$1" in
		''|-*|*-|*--*|*[!a-z0-9-]*) return 1 ;;
	esac
	return 0
}

# Prints config/boards under the given project root.
bhp_board_profile_directory()
{
	printf '%s/config/boards\n' "$1"
}

# Validates the line grammar of one profile file without assigning anything.
bhp_board_profile_validate_file()
{
	BHP_BOARD_PROFILE_VALIDATE_PATH=$1
	[ -f "$BHP_BOARD_PROFILE_VALIDATE_PATH" ] || {
		bhp_board_profile_set_error BOARD_PROFILE.FILE_MISSING \
			"$BHP_BOARD_PROFILE_VALIDATE_PATH"
		return 1
	}
	[ ! -L "$BHP_BOARD_PROFILE_VALIDATE_PATH" ] || {
		bhp_board_profile_set_error BOARD_PROFILE.FILE_IS_SYMLINK \
			"$BHP_BOARD_PROFILE_VALIDATE_PATH"
		return 1
	}
	BHP_BOARD_PROFILE_GRAMMAR_ERROR=$(LC_ALL=C awk '
		/^#/ || /^$/ { next }
		!/^BHP_BOARD_[A-Z0-9_]+=[A-Za-z0-9_.\/:,@+=*%-]*$/ {
			printf "line %d: invalid assignment", NR
			exit 1
		}
		{
			key = $0
			sub(/=.*/, "", key)
			# Deployment variables share the prefix; never shadow them.
			if (key == "BHP_BOARD_SSH_TARGET" || key == "BHP_BOARD_UPLOAD_DIR") {
				printf "line %d: reserved %s", NR, key
				exit 1
			}
			if (seen[key]++) {
				printf "line %d: duplicate %s", NR, key
				exit 1
			}
		}
	' "$BHP_BOARD_PROFILE_VALIDATE_PATH") || {
		bhp_board_profile_set_error BOARD_PROFILE.GRAMMAR_INVALID \
			"$BHP_BOARD_PROFILE_VALIDATE_PATH: $BHP_BOARD_PROFILE_GRAMMAR_ERROR"
		return 1
	}
	return 0
}

# Loads BHP_BOARD_* variables from an explicit profile file path.
bhp_board_profile_load_file()
{
	BHP_BOARD_PROFILE_LOAD_PATH=$1
	bhp_board_profile_validate_file "$BHP_BOARD_PROFILE_LOAD_PATH" || return 1
	for BHP_BOARD_PROFILE_PREVIOUS_KEY in $BHP_BOARD_PROFILE_KEYS; do
		unset "$BHP_BOARD_PROFILE_PREVIOUS_KEY"
	done
	BHP_BOARD_PROFILE_KEYS=
	while IFS= read -r BHP_BOARD_PROFILE_LINE || [ -n "$BHP_BOARD_PROFILE_LINE" ]; do
		case "$BHP_BOARD_PROFILE_LINE" in
			''|'#'*) continue ;;
		esac
		BHP_BOARD_PROFILE_KEY=${BHP_BOARD_PROFILE_LINE%%=*}
		BHP_BOARD_PROFILE_VALUE=${BHP_BOARD_PROFILE_LINE#*=}
		# Key and value already matched the strict grammar above, so this
		# assignment cannot expand, execute, or split anything.
		eval "$BHP_BOARD_PROFILE_KEY=\$BHP_BOARD_PROFILE_VALUE"
		BHP_BOARD_PROFILE_KEYS="$BHP_BOARD_PROFILE_KEYS $BHP_BOARD_PROFILE_KEY"
	done <"$BHP_BOARD_PROFILE_LOAD_PATH"
	[ "${BHP_BOARD_PROFILE_SCHEMA:-}" = 1 ] || {
		bhp_board_profile_set_error BOARD_PROFILE.SCHEMA_UNSUPPORTED \
			"$BHP_BOARD_PROFILE_LOAD_PATH: BHP_BOARD_PROFILE_SCHEMA must be 1"
		return 1
	}
	bhp_board_id_is_valid "${BHP_BOARD_ID:-}" || {
		bhp_board_profile_set_error BOARD_PROFILE.ID_INVALID \
			"$BHP_BOARD_PROFILE_LOAD_PATH: BHP_BOARD_ID"
		return 1
	}
	BHP_BOARD_PROFILE_LOADED_ID=$BHP_BOARD_ID
	BHP_BOARD_PROFILE_LOADED_PATH=$BHP_BOARD_PROFILE_LOAD_PATH
	return 0
}

# Loads config/boards/<board-id>.env below the project root and checks that the
# file declares the same id it was selected by.
bhp_board_profile_load()
{
	BHP_BOARD_PROFILE_PROJECT_ROOT=$1
	BHP_BOARD_PROFILE_REQUESTED_ID=$2
	bhp_board_id_is_valid "$BHP_BOARD_PROFILE_REQUESTED_ID" || {
		bhp_board_profile_set_error BOARD_PROFILE.ID_INVALID \
			"$BHP_BOARD_PROFILE_REQUESTED_ID"
		return 1
	}
	bhp_board_profile_load_file "$(bhp_board_profile_directory \
		"$BHP_BOARD_PROFILE_PROJECT_ROOT")/$BHP_BOARD_PROFILE_REQUESTED_ID.env" ||
		return 1
	[ "$BHP_BOARD_ID" = "$BHP_BOARD_PROFILE_REQUESTED_ID" ] || {
		bhp_board_profile_set_error BOARD_PROFILE.ID_MISMATCH \
			"$BHP_BOARD_PROFILE_REQUESTED_ID.env declares $BHP_BOARD_ID"
		return 1
	}
	return 0
}

# Prints the id of the single profile whose BHP_BOARD_STATUS is active.
bhp_board_profile_active_id()
{
	BHP_BOARD_PROFILE_ACTIVE_ROOT=$1
	BHP_BOARD_PROFILE_ACTIVE_FOUND=
	for BHP_BOARD_PROFILE_CANDIDATE in \
		"$(bhp_board_profile_directory "$BHP_BOARD_PROFILE_ACTIVE_ROOT")"/*.env; do
		[ -f "$BHP_BOARD_PROFILE_CANDIDATE" ] || continue
		if grep -x 'BHP_BOARD_STATUS=active' "$BHP_BOARD_PROFILE_CANDIDATE" \
			>/dev/null 2>&1; then
			[ -z "$BHP_BOARD_PROFILE_ACTIVE_FOUND" ] || {
				bhp_board_profile_set_error BOARD_PROFILE.ACTIVE_AMBIGUOUS \
					"more than one profile declares BHP_BOARD_STATUS=active"
				return 1
			}
			BHP_BOARD_PROFILE_ACTIVE_FOUND=${BHP_BOARD_PROFILE_CANDIDATE##*/}
			BHP_BOARD_PROFILE_ACTIVE_FOUND=${BHP_BOARD_PROFILE_ACTIVE_FOUND%.env}
		fi
	done
	[ -n "$BHP_BOARD_PROFILE_ACTIVE_FOUND" ] || {
		bhp_board_profile_set_error BOARD_PROFILE.ACTIVE_MISSING \
			"no profile declares BHP_BOARD_STATUS=active"
		return 1
	}
	printf '%s\n' "$BHP_BOARD_PROFILE_ACTIVE_FOUND"
}

# Loads the profile named by BHP_BOARD, or the single active profile when
# BHP_BOARD is unset. Sets BHP_BOARD_PROFILE_SELECTION to explicit|active.
bhp_board_profile_load_selected()
{
	BHP_BOARD_PROFILE_SELECT_ROOT=$1
	if [ -n "${BHP_BOARD:-}" ]; then
		BHP_BOARD_PROFILE_SELECTION=explicit
		bhp_board_profile_load "$BHP_BOARD_PROFILE_SELECT_ROOT" "$BHP_BOARD"
		return
	fi
	BHP_BOARD_PROFILE_SELECTION=active
	BHP_BOARD_PROFILE_SELECTED_ID=$(bhp_board_profile_active_id \
		"$BHP_BOARD_PROFILE_SELECT_ROOT") || return 1
	bhp_board_profile_load "$BHP_BOARD_PROFILE_SELECT_ROOT" \
		"$BHP_BOARD_PROFILE_SELECTED_ID"
}

# Classifies a loaded value: prints measured, unmeasured, human_decision, or
# absent.
bhp_board_profile_value_state()
{
	BHP_BOARD_PROFILE_STATE_KEY=$1
	case " $BHP_BOARD_PROFILE_KEYS " in
		*" $BHP_BOARD_PROFILE_STATE_KEY "*) ;;
		*) printf '%s\n' absent; return 0 ;;
	esac
	eval "BHP_BOARD_PROFILE_STATE_VALUE=\${$BHP_BOARD_PROFILE_STATE_KEY}"
	case "$BHP_BOARD_PROFILE_STATE_VALUE" in
		UNMEASURED:*) printf '%s\n' unmeasured ;;
		HUMAN_DECISION_REQUIRED:*) printf '%s\n' human_decision ;;
		*) printf '%s\n' measured ;;
	esac
}

# Prints a loaded value that must be concrete. Unmeasured and undecided values
# fail loudly with the gate or decision id in the detail.
bhp_board_profile_require()
{
	BHP_BOARD_PROFILE_REQUIRE_KEY=$1
	case "$BHP_BOARD_PROFILE_REQUIRE_KEY" in
		BHP_BOARD_*) ;;
		*)
			bhp_board_profile_set_error BOARD_PROFILE.KEY_INVALID \
				"$BHP_BOARD_PROFILE_REQUIRE_KEY"
			return 1
			;;
	esac
	case "$BHP_BOARD_PROFILE_REQUIRE_KEY" in
		*[!A-Z0-9_]*)
			bhp_board_profile_set_error BOARD_PROFILE.KEY_INVALID \
				"$BHP_BOARD_PROFILE_REQUIRE_KEY"
			return 1
			;;
	esac
	case " $BHP_BOARD_PROFILE_KEYS " in
		*" $BHP_BOARD_PROFILE_REQUIRE_KEY "*) ;;
		*)
			bhp_board_profile_set_error BOARD_PROFILE.KEY_MISSING \
				"${BHP_BOARD_PROFILE_LOADED_ID:-unloaded}: $BHP_BOARD_PROFILE_REQUIRE_KEY"
			return 1
			;;
	esac
	eval "BHP_BOARD_PROFILE_REQUIRE_VALUE=\${$BHP_BOARD_PROFILE_REQUIRE_KEY}"
	case "$BHP_BOARD_PROFILE_REQUIRE_VALUE" in
		UNMEASURED:*)
			bhp_board_profile_set_error BOARD_PROFILE.VALUE_UNMEASURED \
				"$BHP_BOARD_PROFILE_LOADED_ID: $BHP_BOARD_PROFILE_REQUIRE_KEY requires evidence for ${BHP_BOARD_PROFILE_REQUIRE_VALUE#UNMEASURED:}"
			return 1
			;;
		HUMAN_DECISION_REQUIRED:*)
			bhp_board_profile_set_error BOARD_PROFILE.HUMAN_DECISION_REQUIRED \
				"$BHP_BOARD_PROFILE_LOADED_ID: $BHP_BOARD_PROFILE_REQUIRE_KEY requires human decision ${BHP_BOARD_PROFILE_REQUIRE_VALUE#HUMAN_DECISION_REQUIRED:}"
			return 1
			;;
		'')
			bhp_board_profile_set_error BOARD_PROFILE.VALUE_EMPTY \
				"$BHP_BOARD_PROFILE_LOADED_ID: $BHP_BOARD_PROFILE_REQUIRE_KEY"
			return 1
			;;
	esac
	printf '%s\n' "$BHP_BOARD_PROFILE_REQUIRE_VALUE"
}

# Assigns a concrete loaded value to the named shell variable in the current
# shell, so failures keep BHP_BOARD_PROFILE_ERROR_* (unlike "$(...)").
bhp_board_profile_require_into()
{
	BHP_BOARD_PROFILE_TARGET_VARIABLE=$1
	case "$BHP_BOARD_PROFILE_TARGET_VARIABLE" in
		''|[!A-Za-z_]*|*[!A-Za-z0-9_]*)
			bhp_board_profile_set_error BOARD_PROFILE.TARGET_VARIABLE_INVALID \
				"$BHP_BOARD_PROFILE_TARGET_VARIABLE"
			return 1
			;;
	esac
	bhp_board_profile_require "$2" >/dev/null || return 1
	eval "$BHP_BOARD_PROFILE_TARGET_VARIABLE=\$BHP_BOARD_PROFILE_REQUIRE_VALUE"
}

# Prints a loaded value verbatim, placeholders included. Use only for
# reporting; configuration must go through bhp_board_profile_require.
bhp_board_profile_value()
{
	BHP_BOARD_PROFILE_VALUE_KEY=$1
	case "$BHP_BOARD_PROFILE_VALUE_KEY" in
		BHP_BOARD_*) ;;
		*) bhp_board_profile_set_error BOARD_PROFILE.KEY_INVALID "$1"; return 1 ;;
	esac
	case "$BHP_BOARD_PROFILE_VALUE_KEY" in
		*[!A-Z0-9_]*) bhp_board_profile_set_error BOARD_PROFILE.KEY_INVALID "$1"; return 1 ;;
	esac
	case " $BHP_BOARD_PROFILE_KEYS " in
		*" $BHP_BOARD_PROFILE_VALUE_KEY "*) ;;
		*)
			bhp_board_profile_set_error BOARD_PROFILE.KEY_MISSING \
				"${BHP_BOARD_PROFILE_LOADED_ID:-unloaded}: $BHP_BOARD_PROFILE_VALUE_KEY"
			return 1
			;;
	esac
	eval "printf '%s\\n' \"\${$BHP_BOARD_PROFILE_VALUE_KEY}\""
}
