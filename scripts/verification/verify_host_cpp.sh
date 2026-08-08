#!/bin/sh
# Compiles and runs the host-testable C++ slices without requiring CMake.
# Inputs: optional all|panel_supervisor|vocabulary_panel.
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
  all|panel_supervisor|vocabulary_panel) ;;
  *)
    bhp_fail VERIFY_HOST.SCOPE_INVALID "Expected all, panel_supervisor, or vocabulary_panel" "$TRACE_ID"
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

compile_supervisor()
{
	compile_supervisor_core_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
			-I"$PROJECT_ROOT/shared/structured_logging/include" \
			-I"$PROJECT_ROOT/panel_supervisor/include" \
			-I"$PROJECT_ROOT/panel_supervisor/tests" \
			"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/error_code.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_policy.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_supervisor.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/presence_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/solar_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_config.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_error.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_event.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/system_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/ui_mode.cpp" \
			"$entrypoint" -o "$output_path"
	}

	compile_supervisor_runtime_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		evdev_source=${3:-$PROJECT_ROOT/panel_supervisor/src/evdev_presence_source.cpp}
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
			-I"$PROJECT_ROOT/shared/structured_logging/include" \
			-I"$PROJECT_ROOT/panel_supervisor/include" \
			-I"$PROJECT_ROOT/panel_supervisor/src" \
			-I"$PROJECT_ROOT/panel_supervisor/tests" \
			"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/error_code.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_policy.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/panel_supervisor.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/presence_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/solar_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_config.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_error.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/supervisor_event.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/system_state.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/ui_mode.cpp" \
			"$PROJECT_ROOT/panel_supervisor/src/daemon_stop_controller.cpp" \
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
			"$entrypoint" -o "$output_path"
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
		"$PROJECT_ROOT/panel_supervisor/simulation/panel_supervisor_simulation.cpp" \
		"$output_directory/panel_supervisor_simulation"
	for test_name in \
		offline_solar_calculator \
		panel_supervisord_config \
		daemon_stop_controller \
		rate_limited_stop_aware_suspend \
		sysfs_backlight \
		sysfs_suspend \
		unix_socket_ui_mode_notifier \
		evdev_presence_source \
		system_clock; do
		compile_supervisor_runtime_entrypoint \
			"$PROJECT_ROOT/panel_supervisor/tests/${test_name}_test.cpp" \
			"$output_directory/${test_name}_test"
	done
	compile_supervisor_runtime_entrypoint \
		"$PROJECT_ROOT/panel_supervisor/daemon/panel_supervisord.cpp" \
		"$output_directory/panel_supervisord"
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
	"$output_directory/panel_supervisor_simulation" >/dev/null
	for test_name in \
		offline_solar_calculator \
		panel_supervisord_config \
		daemon_stop_controller \
		rate_limited_stop_aware_suspend \
		sysfs_backlight \
		sysfs_suspend \
		unix_socket_ui_mode_notifier \
		evdev_presence_source \
		system_clock; do
		"$output_directory/${test_name}_test"
	done
	"$output_directory/structured_logger_test"
	bhp_log info host_slice_verified "$TRACE_ID" slice panel_supervisor \
		test_executables 13 daemon_compiled true \
		daemon_smoke_executed "$daemon_smoke_executed"
}

compile_vocabulary_panel()
{
	compile_vocabulary_entrypoint()
	{
		entrypoint=$1
		output_path=$2
		"$cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
			-I"$PROJECT_ROOT/shared/structured_logging/include" \
			-I"$PROJECT_ROOT/vocabulary_panel/include" \
			-I"$PROJECT_ROOT/vocabulary_panel/tests" \
			"$PROJECT_ROOT/shared/structured_logging/src/structured_logger.cpp" \
			"$PROJECT_ROOT/vocabulary_panel/src/fixture_lexinet_client.cpp" \
			"$PROJECT_ROOT/vocabulary_panel/src/flash_card_session.cpp" \
			"$entrypoint" -o "$output_path"
	}

	compile_vocabulary_entrypoint \
		"$PROJECT_ROOT/vocabulary_panel/tests/flash_card_session_test.cpp" \
		"$output_directory/vocabulary_panel_flash_card_session_test"
	compile_vocabulary_entrypoint \
		"$PROJECT_ROOT/vocabulary_panel/src/vocabulary_panel_terminal_main.cpp" \
		"$output_directory/vocabulary_panel_terminal"

  "$output_directory/vocabulary_panel_flash_card_session_test"
  printf 'reveal\nagain\nquit\n' | "$output_directory/vocabulary_panel_terminal" >/dev/null
  optional_adapters_verified=false
  if [ "$(uname -s)" = Linux ]; then
    cmake --preset host-dev
    cmake --build --preset host-dev --parallel 1
    ctest --preset host-dev --output-on-failure
    cmake_version=$(cmake --version | sed -n '1p')
    libcurl_version=$(pkg-config --modversion libcurl)
    libdrm_version=$(pkg-config --modversion libdrm)
    lvgl_version=$(pkg-config --modversion lvgl)
    bhp_log info host_dev_verification_complete "$TRACE_ID" \
      host_os "$(uname -s)" \
      host_architecture "$(uname -m)" \
      cmake_version "$cmake_version" \
      libcurl_version "$libcurl_version" \
      libdrm_version "$libdrm_version" \
      lvgl_version "$lvgl_version" \
      hardware_evidence not_performed
    optional_adapters_verified=true
  fi
  bhp_log info host_slice_verified "$TRACE_ID" slice vocabulary_panel \
    optional_adapters_verified "$optional_adapters_verified"
}

case "$scope" in
  all)
    compile_supervisor
    compile_vocabulary_panel
    ;;
  panel_supervisor) compile_supervisor ;;
  vocabulary_panel) compile_vocabulary_panel ;;
esac

bhp_log info host_cpp_verification_complete "$TRACE_ID" scope "$scope" compiler "$cxx"
