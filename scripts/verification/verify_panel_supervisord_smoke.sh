#!/bin/sh
# Runs the real panel_supervisord loop with test-linked evdev and redirected sysfs fixtures.
# Inputs: optional BHP_PANEL_SUPERVISORD_SMOKE_DAEMON executable path and CC compiler name.
# Side effects: creates and removes one temporary fixture directory. Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_panel_supervisord_smoke
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "$(uname -s)" != Linux ]; then
	bhp_log warn panel_supervisord_smoke_skipped "$TRACE_ID" reason linux_required
	exit 0
fi

daemon_path=${BHP_PANEL_SUPERVISORD_SMOKE_DAEMON:-$PROJECT_ROOT/.build/verification/panel_supervisord}
test -x "$daemon_path" || {
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.DAEMON_MISSING "$daemon_path" "$TRACE_ID"
	exit 1
}
smoke_cc=${CC:-cc}
command -v "$smoke_cc" >/dev/null 2>&1 || {
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.COMPILER_MISSING "$smoke_cc" "$TRACE_ID"
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.PYTHON_MISSING "python3 is required" "$TRACE_ID"
	exit 1
}

temporary_parent=${TMPDIR:-/tmp}
case "$temporary_parent" in
	/*) ;;
	*)
		bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.TMPDIR_INVALID \
			"TMPDIR must be absolute" "$TRACE_ID"
		exit 1
		;;
esac
test -d "$temporary_parent" || {
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.TMPDIR_MISSING "$temporary_parent" "$TRACE_ID"
	exit 1
}

temporary_root=$(mktemp -d "$temporary_parent/bhp-panel-supervisord-smoke.XXXXXX")
daemon_pid=
cleanup()
{
	if [ -n "$daemon_pid" ] && kill -0 "$daemon_pid" 2>/dev/null; then
		kill -TERM "$daemon_pid" 2>/dev/null || true
		wait "$daemon_pid" 2>/dev/null || true
	fi
	find "$temporary_root" -type s -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -type f -exec rm -- {} \; 2>/dev/null || true
	find "$temporary_root" -depth -type d -exec rmdir -- {} \; 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

sysfs_root=$temporary_root/sysfs
presence_path=$temporary_root/fake-evdev-presence-state
mode_socket_path=$temporary_root/panel-mode.sock
panel_ui_uid=$(id -u)
daemon_log_path=$temporary_root/panel-supervisord.jsonl
preload_library_path=$temporary_root/panel-supervisord-sysfs-fixture-preload.so
mkdir "$sysfs_root"
printf '%s' 73 >"$sysfs_root/brightness"
printf '%s' 100 >"$sysfs_root/max_brightness"
printf '%s' 1 >"$presence_path"

"$smoke_cc" -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -shared \
	"$PROJECT_ROOT/panel_supervisor/tests/fixtures/panel_supervisord_sysfs_fixture_preload.c" \
	-ldl -o "$preload_library_path"

BHP_PANEL_SUPERVISORD_SMOKE_SYSFS_ROOT=$sysfs_root \
LD_PRELOAD=$preload_library_path \
BHP_PANEL_SUPERVISOR_LATITUDE=0 \
BHP_PANEL_SUPERVISOR_LONGITUDE=0 \
BHP_PANEL_SUPERVISOR_PRESENCE_GRACE_SECONDS=1 \
BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH=$presence_path \
BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH=/sys/class/backlight/bhp-smoke/brightness \
BHP_PANEL_SUPERVISOR_BACKLIGHT_MAX_PATH=/sys/class/backlight/bhp-smoke/max_brightness \
BHP_PANEL_SUPERVISOR_MODE_SOCKET=$mode_socket_path \
BHP_PANEL_SUPERVISOR_PANEL_UI_UID=$panel_ui_uid \
BHP_PANEL_SUPERVISOR_PRESENTATION_TIMEOUT_MILLISECONDS=1 \
BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS=80 \
BHP_PANEL_SUPERVISOR_NIGHT_BRIGHTNESS=20 \
BHP_PANEL_SUPERVISOR_GRACE_DAY_BRIGHTNESS=40 \
BHP_PANEL_SUPERVISOR_GRACE_NIGHT_BRIGHTNESS=10 \
BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS=0 \
BHP_PANEL_SUPERVISOR_SUSPEND_MODE=disabled \
BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS=50 \
	"$daemon_path" >"$daemon_log_path" 2>&1 &
daemon_pid=$!

observed_recomputation_count=0
wait_iteration=0
while [ "$wait_iteration" -lt 100 ]; do
	if ! kill -0 "$daemon_pid" 2>/dev/null; then
		break
	fi
	observed_recomputation_count=$(grep -c '"message":"panel_state_recomputed"' \
		"$daemon_log_path" 2>/dev/null || true)
	if [ "$observed_recomputation_count" -ge 4 ]; then
		break
	fi
	wait_iteration=$((wait_iteration + 1))
	sleep 0.05
done

if [ "$observed_recomputation_count" -lt 4 ]; then
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.CYCLES_NOT_OBSERVED \
		"Expected at least four completed daemon cycles; observed $observed_recomputation_count" \
		"$TRACE_ID"
	exit 1
fi

kill -TERM "$daemon_pid"
if wait "$daemon_pid"; then
	daemon_status=0
else
	daemon_status=$?
fi
daemon_pid=
if [ "$daemon_status" -ne 0 ]; then
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.DAEMON_EXIT_FAILED \
		"panel_supervisord exited with status $daemon_status" "$TRACE_ID"
	exit 1
fi

python3 - "$daemon_log_path" <<'PY_PANEL_SUPERVISORD_LOG'
import json
import sys

log_path = sys.argv[1]
with open(log_path, "r", encoding="utf-8") as log_file:
    records = [json.loads(line) for line in log_file if line.strip()]

assert records, "panel_supervisord emitted no structured records"
for record in records:
    for required_field in ("timestamp", "level", "message", "service", "trace_id"):
        assert required_field in record, f"record missing {required_field}: {record}"
    assert record["service"] == "panel_supervisord", record
    assert "latitude" not in record and "longitude" not in record, record

messages = [record["message"] for record in records]
startup_blank_index = messages.index("startup_backlight_forced_off")
daemon_started_index = messages.index("daemon_started")
daemon_stopped_index = messages.index("daemon_stopped")
assert startup_blank_index < daemon_started_index < daemon_stopped_index, messages
startup_blank = records[startup_blank_index]
assert startup_blank["brightness_percent"] == "0", startup_blank
assert startup_blank["display_state"] == "off", startup_blank

recomputed = [record for record in records if record["message"] == "panel_state_recomputed"]
degraded = [record for record in records if record["message"] == "presentation_degraded"]
assert len(recomputed) >= 4, len(recomputed)
assert len(degraded) == len(recomputed), (len(degraded), len(recomputed))
cycle_message_indexes = [
    index
    for index, message in enumerate(messages)
    if message in ("presentation_degraded", "panel_state_recomputed")
]
assert daemon_started_index < cycle_message_indexes[0], cycle_message_indexes
assert cycle_message_indexes[-1] < daemon_stopped_index, cycle_message_indexes
assert [messages[index] for index in cycle_message_indexes] == [
    message
    for _ in recomputed
    for message in ("presentation_degraded", "panel_state_recomputed")
], cycle_message_indexes
assert recomputed[0]["event"] == "start", recomputed[0]
assert sum(record["event"] == "poll" for record in recomputed) >= 3, recomputed
assert [int(record["policy_revision"]) for record in recomputed] == list(
    range(1, len(recomputed) + 1)
), recomputed

for record in recomputed:
    assert record["brightness_percent"] == "0", record
    assert record["presence_detected"] == "true", record
    assert record["presence_state"] == "present", record
    assert record["presentation_status"] == "not_committed", record
    assert record["system_state"] == "active", record
for record in degraded:
    assert record["brightness_percent"] == "0", record
    assert record["degraded"] == "true", record
    assert record["degraded_reason"] == "acknowledgement_unavailable", record
    assert int(record["requested_brightness_percent"]) > 0, record

stopped = records[daemon_stopped_index]
assert stopped["brightness_percent"] == "0", stopped
assert stopped["display_state"] == "off", stopped
assert stopped["reason"] == "signal", stopped
PY_PANEL_SUPERVISORD_LOG

test "$(cat "$sysfs_root/brightness")" = 0 || {
	bhp_fail VERIFY_PANEL_SUPERVISORD_SMOKE.FINAL_BRIGHTNESS_NOT_ZERO \
		"Daemon did not leave the redirected backlight fixture at exact zero" "$TRACE_ID"
	exit 1
}

bhp_log info panel_supervisord_smoke_verified "$TRACE_ID" \
	completed_cycles "$observed_recomputation_count" \
	final_brightness_percent 0 \
	hardware_evidence not_performed
