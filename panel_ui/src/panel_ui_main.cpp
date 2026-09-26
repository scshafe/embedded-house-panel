#ifdef BHP_PANEL_SIMULATION_DRM
#include "panel_ui/simulation_drm_main.hpp"
#endif
#include "panel_ui/fail_dark_lifecycle_display_commit_backend.hpp"
#include "panel_ui/headless_renderer.hpp"
#include "panel_ui/monotonic_clock.hpp"
#include "panel_ui/panel_intent_registry.hpp"
#include "panel_ui/panel_ui_error.hpp"
#include "panel_ui/panel_ui_service.hpp"
#include "panel_ui/panel_ui_service_control.hpp"
#include "panel_ui/result.hpp"
#include "panel_ui/shell_controller.hpp"
#include "panel_ui/sibling_surface_contract.hpp"
#include "panel_ui/unix_socket_lifecycle_client.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <poll.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace house_panel::panel_ui {
namespace {

constexpr std::uint64_t kMaximumConfiguredCollectionBound = 1'048'576U;
volatile std::sig_atomic_t g_stop_requested = 0;

struct PanelUiRuntimeConfiguration {
	std::string lifecycle_socket_path;
	std::uint32_t expected_supervisor_uid;
	std::chrono::milliseconds lifecycle_receive_timeout;
	std::chrono::milliseconds reconnect_delay;
	std::size_t maximum_consecutive_failures;
	std::chrono::milliseconds measured_conflict_window;
	std::string conflict_window_evidence_id;
	std::size_t maximum_source_instances;
	UnicodeTextPolicyConfiguration unicode_text_policy;
	std::string active_surface_id;
};

class SystemMonotonicClock final : public MonotonicClock {
public:
	[[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override
	{
		return std::chrono::steady_clock::now();
	}
};

class ProcessPanelUiServiceControl final : public PanelUiServiceControl {
public:
	[[nodiscard]] bool stop_requested() const noexcept override
	{
		return g_stop_requested != 0;
	}

	[[nodiscard]] PanelUiResult<bool>
	wait_for(const std::chrono::milliseconds delay) override
	{
		const auto deadline = std::chrono::steady_clock::now() + delay;
		while (!stop_requested()) {
			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline) {
				return PanelUiResult<bool>::success(true);
			}
			const auto remaining =
				std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
			const int timeout = static_cast<int>(std::min<long long>(
				remaining,
				static_cast<long long>(std::numeric_limits<int>::max())));
			const int wait_result = ::poll(nullptr, 0, timeout);
			if (wait_result == 0) {
				return PanelUiResult<bool>::success(true);
			}
			if (wait_result < 0 && errno == EINTR) {
				continue;
			}
			if (wait_result < 0) {
				return PanelUiResult<bool>::failure(PanelUiError(
					PanelUiErrorCode::LifecycleServiceWaitFailed,
					"Panel UI reconnect wait failed",
					{{"operation", "panel_ui_main.wait_for"},
					 {"errno", std::to_string(errno)}}));
			}
		}
		return PanelUiResult<bool>::success(false);
	}
};

void handle_stop_signal(const int signal_number) noexcept
{
	(void)signal_number;
	g_stop_requested = 1;
}

PanelUiError configuration_error(std::string message, std::string field)
{
	return PanelUiError(PanelUiErrorCode::LifecycleSocketConfigurationInvalid,
			    std::move(message),
			    {{"field", std::move(field)},
			     {"operation", "panel_ui_main.parse_configuration"}});
}

template <typename Integer>
bool parse_unsigned_integer(const std::string_view text, Integer &value) noexcept
{
	if (text.empty() || text.front() == '-') {
		return false;
	}
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool is_explicit_evidence_id(const std::string_view value) noexcept
{
	if (value.empty() || value.size() > 128U ||
	    value.find("REPLACE_ME") != std::string_view::npos) {
		return false;
	}
	for (const char character : value) {
		const bool alphanumeric = (character >= 'A' && character <= 'Z') ||
					  (character >= 'a' && character <= 'z') ||
					  (character >= '0' && character <= '9');
		if (!alphanumeric && character != '-' && character != '_' && character != '.' &&
		    character != ':') {
			return false;
		}
	}
	return true;
}

PanelUiResult<PanelUiRuntimeConfiguration> parse_configuration(const int argument_count,
						       char **arguments)
{
	std::string socket_path;
	std::string conflict_window_evidence_id;
	std::string active_surface_id;
	std::uint64_t supervisor_uid = 0U;
	std::uint64_t receive_timeout_milliseconds = 0U;
	std::uint64_t reconnect_delay_milliseconds = 0U;
	std::uint64_t maximum_consecutive_failures = 0U;
	std::uint64_t conflict_window_milliseconds = 0U;
	std::uint64_t maximum_source_instances = 0U;
	std::uint64_t maximum_utf8_bytes = 0U;
	std::uint64_t maximum_code_points = 0U;
	std::uint64_t allow_line_breaks = 0U;
	bool has_socket_path = false;
	bool has_supervisor_uid = false;
	bool has_receive_timeout = false;
	bool has_reconnect_delay = false;
	bool has_failure_ceiling = false;
	bool has_conflict_window = false;
	bool has_conflict_evidence = false;
	bool has_source_bound = false;
	bool has_utf8_bound = false;
	bool has_code_point_bound = false;
	bool has_line_break_policy = false;
	bool has_active_surface = false;

	for (int index = 1; index < argument_count; ++index) {
		const std::string_view option(arguments[index]);
		if (index + 1 >= argument_count) {
			return PanelUiResult<PanelUiRuntimeConfiguration>::failure(
				configuration_error("Panel UI option requires one explicit value",
						    "command_line"));
		}
		const std::string_view value(arguments[++index]);
		if (option == "--socket-path" && !has_socket_path && !value.empty()) {
			has_socket_path = true;
			socket_path = value;
			continue;
		}
		if (option == "--supervisor-uid" && !has_supervisor_uid &&
		    parse_unsigned_integer(value, supervisor_uid) &&
		    supervisor_uid <= std::numeric_limits<std::uint32_t>::max()) {
			has_supervisor_uid = true;
			continue;
		}
		if (option == "--receive-timeout-ms" && !has_receive_timeout &&
		    parse_unsigned_integer(value, receive_timeout_milliseconds) &&
		    receive_timeout_milliseconds > 0U &&
		    receive_timeout_milliseconds <= 60'000U) {
			has_receive_timeout = true;
			continue;
		}
		if (option == "--reconnect-delay-ms" && !has_reconnect_delay &&
		    parse_unsigned_integer(value, reconnect_delay_milliseconds) &&
		    reconnect_delay_milliseconds > 0U &&
		    reconnect_delay_milliseconds <= 60'000U) {
			has_reconnect_delay = true;
			continue;
		}
		if (option == "--maximum-consecutive-failures" && !has_failure_ceiling &&
		    parse_unsigned_integer(value, maximum_consecutive_failures) &&
		    maximum_consecutive_failures > 0U &&
		    maximum_consecutive_failures <= 1'000U) {
			has_failure_ceiling = true;
			continue;
		}
		if (option == "--conflict-window-ms" && !has_conflict_window &&
		    parse_unsigned_integer(value, conflict_window_milliseconds) &&
		    conflict_window_milliseconds > 0U &&
		    conflict_window_milliseconds <= 60'000U) {
			has_conflict_window = true;
			continue;
		}
		if (option == "--conflict-window-evidence-id" && !has_conflict_evidence &&
		    is_explicit_evidence_id(value)) {
			has_conflict_evidence = true;
			conflict_window_evidence_id = value;
			continue;
		}
		if (option == "--maximum-source-instances" && !has_source_bound &&
		    parse_unsigned_integer(value, maximum_source_instances) &&
		    maximum_source_instances > 0U &&
		    maximum_source_instances <= kMaximumConfiguredCollectionBound) {
			has_source_bound = true;
			continue;
		}
		if (option == "--maximum-utf8-bytes" && !has_utf8_bound &&
		    parse_unsigned_integer(value, maximum_utf8_bytes) &&
		    maximum_utf8_bytes > 0U &&
		    maximum_utf8_bytes <= kMaximumConfiguredCollectionBound) {
			has_utf8_bound = true;
			continue;
		}
		if (option == "--maximum-code-points" && !has_code_point_bound &&
		    parse_unsigned_integer(value, maximum_code_points) &&
		    maximum_code_points > 0U &&
		    maximum_code_points <= kMaximumConfiguredCollectionBound) {
			has_code_point_bound = true;
			continue;
		}
		if (option == "--allow-line-breaks" && !has_line_break_policy &&
		    parse_unsigned_integer(value, allow_line_breaks) && allow_line_breaks <= 1U) {
			has_line_break_policy = true;
			continue;
		}
		if (option == "--active-surface-id" && !has_active_surface && !value.empty()) {
			has_active_surface = true;
			active_surface_id = value;
			continue;
		}
		return PanelUiResult<PanelUiRuntimeConfiguration>::failure(configuration_error(
			"Panel UI command line contains a duplicate, unknown, or invalid option",
			"command_line"));
	}

	if (!has_socket_path || !has_supervisor_uid || !has_receive_timeout ||
	    !has_reconnect_delay || !has_failure_ceiling || !has_conflict_window ||
	    !has_conflict_evidence || !has_source_bound || !has_utf8_bound ||
	    !has_code_point_bound || !has_line_break_policy || !has_active_surface ||
	    maximum_code_points > maximum_utf8_bytes) {
		return PanelUiResult<PanelUiRuntimeConfiguration>::failure(configuration_error(
			"Panel UI requires every lifecycle, reconnect, shell, and Unicode bound",
			"command_line"));
	}

	return PanelUiResult<PanelUiRuntimeConfiguration>::success(
		PanelUiRuntimeConfiguration{
			std::move(socket_path), static_cast<std::uint32_t>(supervisor_uid),
			std::chrono::milliseconds(receive_timeout_milliseconds),
			std::chrono::milliseconds(reconnect_delay_milliseconds),
			static_cast<std::size_t>(maximum_consecutive_failures),
			std::chrono::milliseconds(conflict_window_milliseconds),
			std::move(conflict_window_evidence_id),
			static_cast<std::size_t>(maximum_source_instances),
			UnicodeTextPolicyConfiguration{
				static_cast<std::size_t>(maximum_utf8_bytes),
				static_cast<std::size_t>(maximum_code_points),
				allow_line_breaks == 1U},
			std::move(active_surface_id)});
}

std::string json_escape(const std::string_view value)
{
	std::string escaped;
	escaped.reserve(value.size());
	for (const char character : value) {
		switch (character) {
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(character);
			break;
		}
	}
	return escaped;
}

void emit_error(const PanelUiError &error)
{
	std::cerr << "{\"data\":null,\"error\":{\"code\":\""
		  << error.code_string() << "\",\"message\":\""
		  << json_escape(error.message())
		  << "\"},\"meta\":{\"service\":\"panel_ui\","
		     "\"hardware_evidence\":\"not_performed\"}}\n";
}

PanelUiResult<bool> install_stop_signal_handlers()
{
	struct sigaction action {};
	action.sa_handler = handle_stop_signal;
	if (sigemptyset(&action.sa_mask) != 0 ||
	    ::sigaction(SIGINT, &action, nullptr) != 0 ||
	    ::sigaction(SIGTERM, &action, nullptr) != 0) {
		return PanelUiResult<bool>::failure(PanelUiError(
			PanelUiErrorCode::LifecycleServiceSignalSetupFailed,
			"Failed to install panel UI stop signal handlers",
			{{"operation", "panel_ui_main.install_stop_signal_handlers"},
			 {"errno", std::to_string(errno)}}));
	}
	return PanelUiResult<bool>::success(true);
}

std::vector<std::string> enabled_intent_routes()
{
	const PanelIntentRegistry registry;
	std::vector<std::string> routes;
	for (const auto &definition : registry.definitions()) {
		if (definition.enabled) {
			routes.emplace_back(definition.intent_id);
		}
	}
	return routes;
}

void print_usage()
{
	std::cout
		<< "usage: bhp-panel-ui --socket-path ABSOLUTE_PATH --supervisor-uid UID "
		   "--receive-timeout-ms MILLISECONDS --reconnect-delay-ms MILLISECONDS "
		   "--maximum-consecutive-failures COUNT --conflict-window-ms MILLISECONDS "
		   "--conflict-window-evidence-id ID --maximum-source-instances COUNT "
		   "--maximum-utf8-bytes BYTES --maximum-code-points COUNT "
		   "--allow-line-breaks 0_OR_1 --active-surface-id ID\n";
}

} // namespace
} // namespace house_panel::panel_ui

int main(const int argument_count, char **arguments)
{
	using namespace house_panel::panel_ui;
#ifdef BHP_PANEL_SIMULATION_DRM
	for(int index=1;index<argument_count;++index)
		if(std::string_view(arguments[index])=="--simulation-drm")return run_simulation_drm(argument_count,arguments);
#endif
	if (argument_count == 2 && std::string_view(arguments[1]) == "--help") {
		print_usage();
		return 0;
	}

	const auto configuration_result = parse_configuration(argument_count, arguments);
	if (!configuration_result) {
		emit_error(configuration_result.error());
		return 2;
	}
	const auto &configuration = configuration_result.value();
	const auto signal_result = install_stop_signal_handlers();
	if (!signal_result) {
		emit_error(signal_result.error());
		return 1;
	}

	SystemMonotonicClock monotonic_clock;
	auto shell_controller_result = ShellController::create(
		ShellControllerConfiguration{
			configuration.measured_conflict_window,
			configuration.conflict_window_evidence_id,
			configuration.maximum_source_instances, false},
		std::nullopt,
		{SiblingSurfaceContract{configuration.active_surface_id,
					enabled_intent_routes()}},
		monotonic_clock);
	if (!shell_controller_result) {
		emit_error(shell_controller_result.error());
		return 2;
	}
	auto headless_renderer_result =
		HeadlessRenderer::create(configuration.unicode_text_policy);
	if (!headless_renderer_result) {
		emit_error(headless_renderer_result.error());
		return 2;
	}

	FailDarkLifecycleDisplayCommitBackend display_commit_backend;
	ProcessPanelUiServiceControl service_control;
	LifecyclePresentationConnectionFactory connection_factory = [&configuration]() {
		auto concrete_result = UnixSocketLifecycleClient::connect(
			configuration.lifecycle_socket_path,
			configuration.expected_supervisor_uid);
		if (!concrete_result) {
			return PanelUiResult<
				std::unique_ptr<LifecyclePresentationConnection>>::failure(
				concrete_result.error());
		}
		std::unique_ptr<LifecyclePresentationConnection> connection =
			std::move(concrete_result).value();
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::success(
			std::move(connection));
	};
	auto service_result = PanelUiService::create(
		PanelUiServiceConfiguration{
			configuration.lifecycle_receive_timeout, configuration.reconnect_delay,
			configuration.maximum_consecutive_failures,
			configuration.active_surface_id},
		std::move(shell_controller_result).value(),
		std::move(headless_renderer_result).value(), display_commit_backend,
		std::move(connection_factory), service_control, monotonic_clock,
		[](const PanelUiError &error) { emit_error(error); });
	if (!service_result) {
		emit_error(service_result.error());
		return 2;
	}

	auto run_result = service_result.value()->run();
	if (!run_result) {
		emit_error(run_result.error());
		return 1;
	}
	std::cout << "{\"data\":{\"service\":\"panel_ui\",\"status\":\"stopped\","
		     "\"presentation_attempt_count\":"
		  << run_result.value().presentation_attempt_count
		  << ",\"committed_presentation_count\":"
		  << run_result.value().committed_presentation_count
		  << ",\"failed_presentation_count\":"
		  << run_result.value().failed_presentation_count
		  << "},\"error\":null,\"meta\":{\"hardware_evidence\":"
		     "\"not_performed\"}}\n";
	return 0;
}
