#!/bin/sh
# Runs the image's x86_64 ShellCheck and, only after status 137 on an ARM host,
# reruns the same analysis with the pinned native aarch64 ShellCheck binary.
# Inputs: ShellCheck arguments and BHP_SHELLCHECK_AARCH64_FALLBACK_ALLOWED.
# Side effects: emits structured fallback logs. Idempotent: yes.

set -u

primary_shellcheck=/usr/bin/shellcheck
fallback_shellcheck=/opt/bhp-shellcheck-aarch64/shellcheck

"$primary_shellcheck" "$@"
primary_status=$?
if [ "$primary_status" -ne 137 ]; then
	exit "$primary_status"
fi
if [ "${BHP_SHELLCHECK_AARCH64_FALLBACK_ALLOWED:-false}" != true ]; then
	exit 137
fi

timestamp=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
trace_id=shellcheck-fallback-$(date -u '+%Y%m%dT%H%M%SZ')-$$
printf '%s\n' \
	"{\"timestamp\":\"$timestamp\",\"level\":\"warn\",\"message\":\"shellcheck_aarch64_fallback_started\",\"service\":\"beaglebone_house_panel.shellcheck_with_aarch64_fallback\",\"trace_id\":\"$trace_id\",\"primary_status\":\"137\"}"

if [ ! -x "$fallback_shellcheck" ]; then
	printf '%s\n' \
		"{\"timestamp\":\"$(date -u '+%Y-%m-%dT%H:%M:%SZ')\",\"level\":\"error\",\"message\":\"shellcheck_aarch64_fallback_missing\",\"service\":\"beaglebone_house_panel.shellcheck_with_aarch64_fallback\",\"trace_id\":\"$trace_id\"}"
	exit 126
fi

"$fallback_shellcheck" "$@"
fallback_status=$?
if [ "$fallback_status" -eq 0 ]; then
	fallback_level=info
	fallback_message=shellcheck_aarch64_fallback_complete
else
	fallback_level=error
	fallback_message=shellcheck_aarch64_fallback_failed
fi
printf '%s\n' \
	"{\"timestamp\":\"$(date -u '+%Y-%m-%dT%H:%M:%SZ')\",\"level\":\"$fallback_level\",\"message\":\"$fallback_message\",\"service\":\"beaglebone_house_panel.shellcheck_with_aarch64_fallback\",\"trace_id\":\"$trace_id\",\"fallback_status\":\"$fallback_status\"}"
exit "$fallback_status"
