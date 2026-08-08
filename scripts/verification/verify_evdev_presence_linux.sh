#!/bin/sh
# Strict-builds and runs the production evdev adapter against an exact Linux ioctl fixture.
# Inputs: optional CXX and CC compiler names. Side effects: temporary files only. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_evdev_presence_linux
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "$(uname -s)" != Linux ]; then
	bhp_log warn evdev_presence_linux_skipped "$TRACE_ID" reason linux_required
	exit 0
fi

linux_cxx=${CXX:-c++}
linux_cc=${CC:-cc}
for compiler in "$linux_cxx" "$linux_cc"; do
	command -v "$compiler" >/dev/null 2>&1 || {
		bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.COMPILER_MISSING "$compiler" "$TRACE_ID"
		exit 1
	}
done

temporary_parent=${TMPDIR:-/tmp}
case "$temporary_parent" in
	/*) ;;
	*)
		bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.TMPDIR_INVALID \
			"TMPDIR must be absolute" "$TRACE_ID"
		exit 1
		;;
esac
test -d "$temporary_parent" || {
	bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.TMPDIR_MISSING "$temporary_parent" "$TRACE_ID"
	exit 1
}

temporary_root=$(mktemp -d "$temporary_parent/bhp-evdev-presence-linux.XXXXXX")
cleanup()
{
	find "$temporary_root" -type f -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -depth -type d -exec rmdir -- {} \; 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

device_path=$temporary_root/fake-event-device
state_path=$temporary_root/front-proximity-state
request_log_path=$temporary_root/ioctl-requests
compiler_output_path=$temporary_root/compiler-output
test_output_path=$temporary_root/test-output
test_executable_path=$temporary_root/evdev_presence_source_linux_test
preload_library_path=$temporary_root/evdev_presence_linux_fixture_preload.so
printf '%s' fake-evdev >"$device_path"
printf '%s' 0 >"$state_path"
: >"$request_log_path"

if ! "$linux_cc" -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -shared \
	"$PROJECT_ROOT/panel_supervisor/tests/fixtures/evdev_presence_linux_fixture_preload.c" \
	-ldl -o "$preload_library_path" >"$compiler_output_path" 2>&1; then
	compiler_failure=$(head -c 512 "$compiler_output_path" | tr '\r\n' '  ')
	bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.PRELOAD_BUILD_FAILED \
		"${compiler_failure:-Linux fixture preload compilation failed}" "$TRACE_ID"
	exit 1
fi
if ! "$linux_cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
	-I"$PROJECT_ROOT/panel_supervisor/include" \
	"$PROJECT_ROOT/panel_supervisor/src/error_code.cpp" \
	"$PROJECT_ROOT/panel_supervisor/src/supervisor_error.cpp" \
	"$PROJECT_ROOT/panel_supervisor/src/evdev_presence_source.cpp" \
	"$PROJECT_ROOT/panel_supervisor/tests/evdev_presence_source_linux_test.cpp" \
	-o "$test_executable_path" >"$compiler_output_path" 2>&1; then
	compiler_failure=$(head -c 512 "$compiler_output_path" | tr '\r\n' '  ')
	bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.TEST_BUILD_FAILED \
		"${compiler_failure:-Linux evdev test compilation failed}" "$TRACE_ID"
	exit 1
fi

if ! BHP_EVDEV_PRESENCE_LINUX_DEVICE_PATH=$device_path \
	BHP_EVDEV_PRESENCE_LINUX_STATE_PATH=$state_path \
	BHP_EVDEV_PRESENCE_LINUX_REQUEST_LOG=$request_log_path \
	LD_PRELOAD=$preload_library_path \
	"$test_executable_path" "$device_path" "$state_path" "$request_log_path" \
		>"$test_output_path" 2>&1; then
	test_failure=$(head -c 512 "$test_output_path" | tr '\r\n' '  ')
	bhp_fail VERIFY_EVDEV_PRESENCE_LINUX.TEST_FAILED \
		"${test_failure:-Linux evdev test failed without output}" "$TRACE_ID"
	exit 1
fi

bhp_log info evdev_presence_linux_verified "$TRACE_ID" \
	capability_request_observed true \
	state_request_observed true \
	asserted_state_observed true \
	deasserted_state_observed true \
	hardware_evidence not_performed
