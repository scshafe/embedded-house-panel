#!/bin/sh
# Compiles and runs host C++ slices and the shared LVGL/HTTP integration.
# Inputs: optional all|panel_supervisor|panel_ui|vocabulary_panel.
# Side effects: writes compiler products below .build/verification.
# Idempotent: yes.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_host_cpp
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

scope=${1:-all}
case "$scope" in
  all|panel_supervisor|panel_ui|vocabulary_panel) ;;
  *)
    bhp_fail VERIFY_HOST.SCOPE_INVALID \
      "Expected all, panel_supervisor, panel_ui, or vocabulary_panel" "$TRACE_ID"
    exit 2
    ;;
esac

cxx=${CXX:-c++}
command -v "$cxx" >/dev/null 2>&1 || {
  bhp_fail VERIFY_HOST.COMPILER_MISSING "$cxx" "$TRACE_ID"
  exit 1
}

output_directory=$PROJECT_ROOT/.build/verification
mkdir -p "$output_directory"

command -v pkg-config >/dev/null 2>&1 || {
  bhp_fail VERIFY_HOST.PKG_CONFIG_MISSING "pkg-config" "$TRACE_ID"
  exit 1
}
openssl_cflags=$(pkg-config --cflags openssl) || {
  bhp_fail VERIFY_HOST.OPENSSL_DEVELOPMENT_MISSING "openssl cflags" "$TRACE_ID"
  exit 1
}
openssl_libs=$(pkg-config --libs openssl) || {
  bhp_fail VERIFY_HOST.OPENSSL_DEVELOPMENT_MISSING "openssl libs" "$TRACE_ID"
  exit 1
}
application_release_id=$(
  "$PROJECT_ROOT/scripts/build/application_release_id.sh" "$PROJECT_ROOT"
) || {
  bhp_fail VERIFY_HOST.APPLICATION_RELEASE_ID_FAILED \
    "Canonical application release identity could not be derived" "$TRACE_ID"
  exit 1
}
application_release_definition=-DBHP_APPLIANCE_RELEASE_ID=\"$application_release_id\"

compile_supervisor()
{
	compile_supervisor_core_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
			"$application_release_definition" \
			-I"$PROJECT_ROOT/shared/structured_logging/include" \
			-I"$PROJECT_ROOT/shared/application_identity/include" \
			-I"$PROJECT_ROOT/shared/panel_mode_protocol/include" \
			-I"$PROJECT_ROOT/shared/touch_observation_protocol/include" \
			-I"$PROJECT_ROOT/panel_supervisor/include" \
			-I"$PROJECT_ROOT/panel_supervisor/tests" \
			"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
			"$PROJECT_ROOT/shared/touch_observation_protocol/src/touch_observation_validation.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/error_code.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_policy.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_supervisor.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/presence_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/solar_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_config.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_error.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_event.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/system_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/touch_observation_authority.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/ui_mode.cpp" \
			"$entrypoint" -o "$output_path"
	}

	compile_supervisor_runtime_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		evdev_source=${3:-$PROJECT_ROOT/panel_supervisor/src/evdev_presence_source.cpp}
		# shellcheck disable=SC2086
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
			$openssl_cflags "$application_release_definition" \
			-I"$PROJECT_ROOT/shared/structured_logging/include" \
			-I"$PROJECT_ROOT/shared/application_identity/include" \
			-I"$PROJECT_ROOT/shared/panel_mode_protocol/include" \
			-I"$PROJECT_ROOT/panel_supervisor/include" \
			-I"$PROJECT_ROOT/shared/touch_observation_protocol/include" \
			-I"$PROJECT_ROOT/shared/media_window_protocol/include" \
			-I"$PROJECT_ROOT/panel_supervisor/src" \
			-I"$PROJECT_ROOT/panel_supervisor/tests" \
			"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
			"$PROJECT_ROOT/shared/touch_observation_protocol/src/touch_observation_validation.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/error_code.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_policy.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_supervisor.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/presence_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/solar_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_config.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_error.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_event.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/system_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/touch_observation_authority.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/ui_mode.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/capture_closure.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/daemon_stop_controller.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/evdev_touch_source.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/touch_runtime_configuration.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/touch_observation_runtime.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/media_window_endpoint.cpp" \
			"$PROJECT_ROOT/shared/media_window_protocol/src/media_window_protocol.cpp" \
			"$PROJECT_ROOT/shared/touch_observation_protocol/src/touch_observation_codec.cpp" \
			"$PROJECT_ROOT/shared/touch_observation_protocol/src/unix_touch_observation_socket.cpp" \
			"$evdev_source" \
			"$PROJECT_ROOT/panel_supervisor/src/exact_file_writer.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/offline_solar_calculator.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_supervisord_config.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/rate_limited_stop_aware_suspend.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/suspend_mode.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/sysfs_backlight.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/sysfs_suspend.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/system_clock.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/unix_socket_ui_mode_notifier.cpp" \
			"$PROJECT_ROOT/shared/panel_mode_protocol/src/codec.cpp" \
			"$PROJECT_ROOT/shared/panel_mode_protocol/src/framing.cpp" \
			"$entrypoint" $openssl_libs -o "$output_path"
	}

	compile_supervisor_core_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/tests/panel_policy_test.cpp" \
		"$output_directory/panel_policy_test"
	compile_supervisor_core_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/tests/panel_supervisor_test.cpp" \
		"$output_directory/panel_supervisor_test"
	compile_supervisor_core_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/tests/supervisor_config_test.cpp" \
		"$output_directory/supervisor_config_test"
	compile_supervisor_core_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/tests/touch_observation_authority_test.cpp" \
		"$output_directory/touch_observation_authority_test"
	# shellcheck disable=SC2086
	"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
		$openssl_cflags -I"$PROJECT_ROOT/shared/panel_mode_protocol/include" \
		"$PROJECT_ROOT/shared/panel_mode_protocol/src/codec.cpp" \
		"$PROJECT_ROOT/shared/panel_mode_protocol/src/framing.cpp" \
		"$PROJECT_ROOT/shared/panel_mode_protocol/tests/codec_test.cpp" \
		$openssl_libs -o "$output_directory/panel_mode_protocol_test"
	"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
		-DBHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS=1 \
		-I"$PROJECT_ROOT/shared/panel_mode_protocol/include" \
		-I"$PROJECT_ROOT/shared/touch_observation_protocol/include" \
		"$PROJECT_ROOT/shared/touch_observation_protocol/src/touch_observation_validation.cpp" \
		"$PROJECT_ROOT/shared/touch_observation_protocol/src/bounded_normalized_touch_observation_stream.cpp" \
		"$PROJECT_ROOT/shared/touch_observation_protocol/tests/bounded_normalized_touch_observation_stream_test.cpp" \
		-o "$output_directory/touch_observation_protocol_test"
	compile_supervisor_core_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/simulation/panel_supervisor_simulation.cpp" \
		"$output_directory/panel_supervisor_simulation"
	for test_name in \
		offline_solar_calculator \
		panel_supervisord_config \
		daemon_stop_controller \
		rate_limited_stop_aware_suspend \
		capture_closure \
		sysfs_backlight \
		sysfs_suspend \
		unix_socket_ui_mode_notifier \
		media_window_endpoint \
		evdev_presence_source \
		system_clock; do
		compile_supervisor_runtime_entrypoint \
			"$PROJECT_ROOT/panel_supervisor/tests/${test_name}_test.cpp" \
			"$output_directory/${test_name}_test"
	done
	compile_supervisor_runtime_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/daemon/panel_supervisord.cpp" \
		"$output_directory/panel_supervisord"
	rejected_argument_marker=fixture_secret_must_not_enter_logs
	set +e
	rejected_argument_output=$(
		"$output_directory/panel_supervisord" "$rejected_argument_marker" 2>&1
	)
	rejected_argument_status=$?
	set -e
	if [ "$rejected_argument_status" -ne 2 ]; then
		bhp_fail VERIFY_HOST.DAEMON_ARGUMENT_STATUS_INVALID \
			"Rejected daemon argument did not return status 2" "$TRACE_ID"
		exit 1
	fi
	case "$rejected_argument_output" in
	  *"$rejected_argument_marker"*)
		bhp_fail VERIFY_HOST.DAEMON_ARGUMENT_CONTENT_LEAKED \
			"Rejected daemon argument content entered logs" "$TRACE_ID"
		exit 1
		;;
	esac
	case "$rejected_argument_output" in
	  *'"argument_content":"redacted"'*) ;;
	  *)
		bhp_fail VERIFY_HOST.DAEMON_ARGUMENT_REDACTION_MISSING \
			"Rejected daemon argument log lacks the stable redaction marker" "$TRACE_ID"
		exit 1
		;;
	esac
	daemon_smoke_executed=false
	if [ "$(uname -s)" = Linux ]; then
		CXX="$cxx" "$PROJECT_ROOT/scripts/verification/verify_evdev_presence_linux.sh"
		smoke_output_directory=$output_directory/panel-supervisord-smoke
		mkdir -p "$smoke_output_directory"
		compile_supervisor_runtime_entrypoint \
			"$PROJECT_ROOT/panel_supervisor/daemon/panel_supervisord.cpp" \
			"$smoke_output_directory/panel_supervisord" \
			"$PROJECT_ROOT/panel_supervisor/tests/fake_evdev_presence_source.cpp"
		BHP_PANEL_SUPERVISORD_SMOKE_DAEMON="$smoke_output_directory/panel_supervisord" \
			"$PROJECT_ROOT/scripts/verification/verify_panel_supervisord_smoke.sh"
		daemon_smoke_executed=true
	fi
	"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
		-I"$PROJECT_ROOT/shared/structured_logging/include" \
		"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
		"$PROJECT_ROOT/shared/structured_logging/tests/structured_logger_test.cpp" \
		-o "$output_directory/structured_logger_test"

	"$output_directory/panel_policy_test"
	"$output_directory/panel_supervisor_test"
	"$output_directory/supervisor_config_test"
	"$output_directory/touch_observation_authority_test"
	"$output_directory/panel_mode_protocol_test"
	"$output_directory/touch_observation_protocol_test"
	"$PROJECT_ROOT/scripts/verification/verify_panel_mode_protocol_fixture.py"
	"$output_directory/panel_supervisor_simulation" >/dev/null
	for test_name in \
		offline_solar_calculator \
		panel_supervisord_config \
		daemon_stop_controller \
		rate_limited_stop_aware_suspend \
		capture_closure \
		sysfs_backlight \
		sysfs_suspend \
		unix_socket_ui_mode_notifier \
		media_window_endpoint \
		evdev_presence_source \
		system_clock; do
		"$output_directory/${test_name}_test"
	done
	"$output_directory/structured_logger_test"
	bhp_log info host_slice_verified "$TRACE_ID" slice panel_supervisor \
		test_executables 17 daemon_compiled true \
		daemon_smoke_executed "$daemon_smoke_executed"
}

compile_panel_ui()
{
	compile_panel_ui_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		# shellcheck disable=SC2086
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
			$openssl_cflags "$application_release_definition" \
			-I"$PROJECT_ROOT/shared/application_identity/include" \
			-I"$PROJECT_ROOT/shared/panel_mode_protocol/include" \
			-I"$PROJECT_ROOT/shared/touch_observation_protocol/include" \
			-I"$PROJECT_ROOT/panel_ui/include" \
			"$PROJECT_ROOT/shared/panel_mode_protocol/src/codec.cpp" \
			"$PROJECT_ROOT/shared/panel_mode_protocol/src/framing.cpp" \
			"$PROJECT_ROOT/panel_ui/src/application_catalog.cpp" \
			"$PROJECT_ROOT/panel_ui/src/bounded_unicode_text_policy.cpp" \
			"$PROJECT_ROOT/panel_ui/src/fail_dark_lifecycle_display_commit_backend.cpp" \
			"$PROJECT_ROOT/panel_ui/src/headless_renderer.cpp" \
			"$PROJECT_ROOT/panel_ui/src/interaction_intent.cpp" \
			"$PROJECT_ROOT/panel_ui/src/panel_app_descriptors.cpp" \
			"$PROJECT_ROOT/panel_ui/src/panel_intent_registry.cpp" \
			"$PROJECT_ROOT/panel_ui/src/panel_ui_error.cpp" \
			"$PROJECT_ROOT/panel_ui/src/panel_ui_service.cpp" \
			"$PROJECT_ROOT/panel_ui/src/shell_controller.cpp" \
			"$PROJECT_ROOT/panel_ui/src/shell_state.cpp" \
			"$PROJECT_ROOT/panel_ui/src/unix_socket_lifecycle_client.cpp" \
			"$entrypoint" $openssl_libs -o "$output_path"
	}

	panel_ui_test_count=0
	for test_name in \
		panel_app_descriptor \
		panel_intent_registry \
		shell_controller \
		bounded_unicode_text_policy \
		panel_ui_service \
		headless_renderer \
		unix_socket_lifecycle_client; do
		compile_panel_ui_entrypoint \
			"$PROJECT_ROOT/panel_ui/tests/${test_name}_test.cpp" \
			"$output_directory/panel_ui_${test_name}_test"
		"$output_directory/panel_ui_${test_name}_test"
		panel_ui_test_count=$((panel_ui_test_count + 1))
	done
	compile_panel_ui_entrypoint \
		"$PROJECT_ROOT/panel_ui/src/panel_ui_core_smoke.cpp" \
		"$output_directory/panel_ui_core_smoke"
	"$output_directory/panel_ui_core_smoke" >/dev/null
	compile_panel_ui_entrypoint \
		"$PROJECT_ROOT/panel_ui/src/panel_ui_main.cpp" \
		"$output_directory/bhp-panel-ui"
	panel_ui_configuration_error=$output_directory/panel-ui-configuration-error.json
	if "$output_directory/bhp-panel-ui" 2>"$panel_ui_configuration_error"; then
		bhp_fail VERIFY_HOST.PANEL_UI_ACCEPTED_IMPLICIT_CONFIGURATION \
			"bhp-panel-ui started without its three explicit deployment inputs" \
			"$TRACE_ID"
		exit 1
	else
		panel_ui_configuration_status=$?
	fi
	if [ "$panel_ui_configuration_status" -ne 2 ] || \
		! grep -F 'PANEL_UI.LIFECYCLE_SOCKET_CONFIGURATION_INVALID' \
			"$panel_ui_configuration_error" >/dev/null; then
		bhp_fail VERIFY_HOST.PANEL_UI_CONFIGURATION_FAILURE_INVALID \
			"bhp-panel-ui did not return its stable explicit-configuration failure" \
			"$TRACE_ID"
		exit 1
	fi
	bhp_log info host_slice_verified "$TRACE_ID" slice panel_ui \
		test_executables "$panel_ui_test_count" \
		production_entrypoint_verified true \
		hardware_evidence not_performed
}

compile_vocabulary_panel()
{
  (cd "$PROJECT_ROOT" && cmake --fresh --preset host-dev)
  cmake --build "$PROJECT_ROOT/.build/host-dev" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-6}"
  ctest --test-dir "$PROJECT_ROOT/.build/host-dev" --output-on-failure
  printf 'reveal\nagain\nquit\n' | \
    "$PROJECT_ROOT/.build/host-dev/vocabulary_panel/vocabulary_panel_terminal" >/dev/null
  bhp_log info host_slice_verified "$TRACE_ID" slice vocabulary_panel \
    optional_adapters_verified true hardware_evidence not_performed
}

case "$scope" in
  all)
    compile_supervisor
    compile_panel_ui
    compile_vocabulary_panel
    ;;
  panel_supervisor) compile_supervisor ;;
  panel_ui) compile_panel_ui ;;
  vocabulary_panel) compile_vocabulary_panel ;;
esac

bhp_log info host_cpp_verification_complete "$TRACE_ID" scope "$scope" compiler "$cxx"
