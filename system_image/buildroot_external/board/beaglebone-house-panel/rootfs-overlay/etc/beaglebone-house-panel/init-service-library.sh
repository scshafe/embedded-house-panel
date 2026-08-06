#!/bin/sh

# canonical: bhp_target_init_service_support -- shared SysV init logging,
# environment loading, and PID inspection for house-panel target services.

BHP_INIT_TRACE_ID=sysv-init

bhp_init_log_field_is_safe()
{
	case "$1" in
		*\"*|*\\*|*'
'*) return 1 ;;
		*) return 0 ;;
	esac
}

bhp_init_log()
{
	BHP_INIT_LOG_LEVEL=$1
	BHP_INIT_LOG_SERVICE=$2
	BHP_INIT_LOG_MESSAGE=$3

	if ! bhp_init_log_field_is_safe "$BHP_INIT_LOG_LEVEL" ||
		 ! bhp_init_log_field_is_safe "$BHP_INIT_LOG_SERVICE" ||
		 ! bhp_init_log_field_is_safe "$BHP_INIT_LOG_MESSAGE"; then
		printf '%s\n' 'BHP_INIT.LOG_FIELD_INVALID: refused to emit malformed JSON log field' >&2
		return 1
	fi

	BHP_INIT_LOG_TIMESTAMP=$(date -u '+%Y-%m-%dT%H:%M:%SZ') || return 1
	printf '{"timestamp":"%s","level":"%s","message":"%s","service":"%s","trace_id":"%s"}\n' \
		"$BHP_INIT_LOG_TIMESTAMP" \
		"$BHP_INIT_LOG_LEVEL" \
		"$BHP_INIT_LOG_MESSAGE" \
		"$BHP_INIT_LOG_SERVICE" \
		"$BHP_INIT_TRACE_ID"
}

bhp_init_log_error()
{
	BHP_INIT_ERROR_SERVICE=$1
	BHP_INIT_ERROR_CODE=$2
	BHP_INIT_ERROR_MESSAGE=$3

	if ! bhp_init_log_field_is_safe "$BHP_INIT_ERROR_SERVICE" ||
		 ! bhp_init_log_field_is_safe "$BHP_INIT_ERROR_CODE" ||
		 ! bhp_init_log_field_is_safe "$BHP_INIT_ERROR_MESSAGE"; then
		printf '%s\n' 'BHP_INIT.LOG_FIELD_INVALID: refused to emit malformed JSON error field' >&2
		return 1
	fi

	BHP_INIT_ERROR_TIMESTAMP=$(date -u '+%Y-%m-%dT%H:%M:%SZ') || return 1
	printf '{"timestamp":"%s","level":"error","message":"service_operation_failed","service":"%s","trace_id":"%s","error_code":"%s","error_message":"%s","stack_trace":"unavailable_for_sysv_shell"}\n' \
		"$BHP_INIT_ERROR_TIMESTAMP" \
		"$BHP_INIT_ERROR_SERVICE" \
		"$BHP_INIT_TRACE_ID" \
		"$BHP_INIT_ERROR_CODE" \
		"$BHP_INIT_ERROR_MESSAGE" >&2
}

bhp_init_environment_file_is_secure()
{
	BHP_INIT_ENVIRONMENT_FILE=$1
	if [ -L "$BHP_INIT_ENVIRONMENT_FILE" ] ||
	   [ ! -f "$BHP_INIT_ENVIRONMENT_FILE" ] ||
	   [ ! -r "$BHP_INIT_ENVIRONMENT_FILE" ]; then
		return 1
	fi
	# The image's BusyBox profile provides numeric long-format ls but not stat.
	BHP_INIT_ENVIRONMENT_LISTING=$(LC_ALL=C ls -ldn "$BHP_INIT_ENVIRONMENT_FILE") || return 1
	# Field splitting is intentional; callers pass fixed paths without globs.
	# shellcheck disable=SC2086
	set -- $BHP_INIT_ENVIRONMENT_LISTING
	[ "$#" -ge 3 ] || return 1
	BHP_INIT_ENVIRONMENT_PERMISSIONS=$1
	BHP_INIT_ENVIRONMENT_OWNER_UID=$3
	[ "$BHP_INIT_ENVIRONMENT_OWNER_UID" = 0 ] || return 1
	case "$BHP_INIT_ENVIRONMENT_PERMISSIONS" in
		-r--------|-r--r-----|-rw-------|-rw-r-----) ;;
		*) return 1 ;;
	esac

	if [ ! -s "$BHP_INIT_ENVIRONMENT_FILE" ]; then
		return 1
	fi
	return 0
}

bhp_init_load_environment()
{
	BHP_INIT_ENVIRONMENT_FILE=$1
	[ -e "$BHP_INIT_ENVIRONMENT_FILE" ] || return 0
	bhp_init_environment_file_is_secure "$BHP_INIT_ENVIRONMENT_FILE" || return 1

	# This file is executable shell syntax by design. Root ownership, a regular
	# non-symlink file, and mode 0400, 0440, 0600, or 0640 are enforced above.
	set -a
	# shellcheck disable=SC1090
	. "$BHP_INIT_ENVIRONMENT_FILE"
	BHP_INIT_ENVIRONMENT_STATUS=$?
	set +a
	return "$BHP_INIT_ENVIRONMENT_STATUS"
}

bhp_init_boolean_is_valid()
{
	case "$1" in
		0|1) return 0 ;;
		*) return 1 ;;
	esac
}

# canonical: bhp_panel_runtime_directory -- prepares the supervisor-owned IPC
# directory so vocabulary-panel can traverse/connect but cannot create, unlink,
# or replace the supervisor socket. Setgid gives root-created sockets the
# vocabulary-panel group.
bhp_init_prepare_panel_runtime_directory()
{
	[ -r /etc/group ] || return 1
	grep -q '^vocabulary-panel:' /etc/group || return 1
	mkdir -p /run/beaglebone-house-panel || return 1
	[ -d /run/beaglebone-house-panel ] || return 1
	[ ! -L /run/beaglebone-house-panel ] || return 1
	chown root:vocabulary-panel /run/beaglebone-house-panel || return 1
	chmod 2750 /run/beaglebone-house-panel || return 1
}

bhp_init_pidfile_process_is_running()
{
	BHP_INIT_PIDFILE=$1
	[ -r "$BHP_INIT_PIDFILE" ] || return 1
	BHP_INIT_PID=$(sed -n '1p' "$BHP_INIT_PIDFILE") || return 1
	case "$BHP_INIT_PID" in
		''|*[!0-9]*) return 1 ;;
	esac
	kill -0 "$BHP_INIT_PID" 2>/dev/null
}

bhp_init_wait_for_pidfile_exit()
{
	BHP_INIT_WAIT_PIDFILE=$1
	BHP_INIT_WAIT_TIMEOUT_SECONDS=$2
	case "$BHP_INIT_WAIT_TIMEOUT_SECONDS" in
		''|*[!0-9]*) return 2 ;;
	esac

	BHP_INIT_WAIT_ELAPSED_SECONDS=0
	while bhp_init_pidfile_process_is_running "$BHP_INIT_WAIT_PIDFILE"; do
		if [ "$BHP_INIT_WAIT_ELAPSED_SECONDS" -ge "$BHP_INIT_WAIT_TIMEOUT_SECONDS" ]; then
			return 1
		fi
		sleep 1
		BHP_INIT_WAIT_ELAPSED_SECONDS=$((BHP_INIT_WAIT_ELAPSED_SECONDS + 1))
	done
	return 0
}
