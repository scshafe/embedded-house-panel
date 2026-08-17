#include "panel_ui/panel_ui_service.hpp"

#include "bhp/panel_mode_protocol/types.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace house_panel::panel_ui {
namespace {

constexpr std::chrono::milliseconds kMaximumServiceInterval{60'000};
constexpr std::size_t kMaximumConsecutiveFailureCeiling = 1'000U;
constexpr std::size_t kMaximumSurfaceIdBytes = 64U;

PanelUiError service_error(const PanelUiErrorCode code, std::string message,
			   std::string operation,
			   std::map<std::string, std::string> context = {})
{
	context.emplace("operation", std::move(operation));
	return PanelUiError(code, std::move(message), std::move(context));
}

bool is_safe_surface_id(const std::string_view value) noexcept
{
	if (value.empty() || value.size() > kMaximumSurfaceIdBytes ||
	    value.find("REPLACE_ME") != std::string_view::npos) {
		return false;
	}
	return std::all_of(value.begin(), value.end(), [](const char character) {
		return (character >= 'a' && character <= 'z') ||
		       (character >= '0' && character <= '9') || character == '_' ||
		       character == '-' || character == '.';
	});
}

bool presentation_result_is_valid(
	const bhp::panel_mode_protocol::PresentationResult result) noexcept
{
	return result == bhp::panel_mode_protocol::PresentationResult::Committed ||
	       result == bhp::panel_mode_protocol::PresentationResult::Failed;
}

ShellTransitionState transition_for_snapshot(
	const bhp::panel_mode_protocol::Snapshot &snapshot) noexcept
{
	if (snapshot.system_state ==
	    bhp::panel_mode_protocol::SystemState::RecomputingAfterResume) {
		return ShellTransitionState::Resuming;
	}
	if (snapshot.display_mode == bhp::panel_mode_protocol::DisplayMode::Suspended) {
		return ShellTransitionState::Suspending;
	}
	return ShellTransitionState::Committing;
}

} // namespace

PanelUiResult<std::unique_ptr<PanelUiService>> PanelUiService::create(
	PanelUiServiceConfiguration configuration,
	std::unique_ptr<ShellController> shell_controller,
	std::unique_ptr<HeadlessRenderer> headless_renderer,
	LifecycleDisplayCommitBackend &display_commit_backend,
	LifecyclePresentationConnectionFactory connection_factory,
	PanelUiServiceControl &service_control, MonotonicClock &monotonic_clock,
	PanelUiServiceErrorSink error_sink)
{
	if (configuration.lifecycle_receive_timeout.count() <= 0 ||
	    configuration.lifecycle_receive_timeout > kMaximumServiceInterval) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceConfigurationInvalid,
			"Panel UI service requires an explicit bounded lifecycle receive timeout",
			"panel_ui_service.create",
			{{"field", "lifecycle_receive_timeout"},
			 {"bound", "1..60000_milliseconds"}}));
	}
	if (configuration.reconnect_delay.count() <= 0 ||
	    configuration.reconnect_delay > kMaximumServiceInterval) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceConfigurationInvalid,
			"Panel UI service requires an explicit bounded reconnect delay",
			"panel_ui_service.create",
			{{"field", "reconnect_delay"},
			 {"bound", "1..60000_milliseconds"}}));
	}
	if (configuration.maximum_consecutive_failures == 0U ||
	    configuration.maximum_consecutive_failures >
		    kMaximumConsecutiveFailureCeiling) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceConfigurationInvalid,
			"Panel UI service requires an explicit consecutive-failure ceiling",
			"panel_ui_service.create",
			{{"field", "maximum_consecutive_failures"},
			 {"bound", "1..1000"}}));
	}
	if (!is_safe_surface_id(configuration.active_surface_id)) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceConfigurationInvalid,
			"Panel UI service requires one protocol-safe active surface identifier",
			"panel_ui_service.create", {{"field", "active_surface_id"}}));
	}
	if (!shell_controller || !headless_renderer || !connection_factory || !error_sink) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceConfigurationInvalid,
			"Panel UI service requires complete shell, renderer, connection, control, and diagnostic ports",
			"panel_ui_service.create", {{"field", "service_ports"}}));
	}
	return PanelUiResult<std::unique_ptr<PanelUiService>>::success(
		std::unique_ptr<PanelUiService>(new PanelUiService(
			std::move(configuration), std::move(shell_controller),
			std::move(headless_renderer), display_commit_backend,
			std::move(connection_factory), service_control, monotonic_clock,
			std::move(error_sink))));
}

PanelUiService::PanelUiService(
	PanelUiServiceConfiguration configuration,
	std::unique_ptr<ShellController> shell_controller,
	std::unique_ptr<HeadlessRenderer> headless_renderer,
	LifecycleDisplayCommitBackend &display_commit_backend,
	LifecyclePresentationConnectionFactory connection_factory,
	PanelUiServiceControl &service_control, MonotonicClock &monotonic_clock,
	PanelUiServiceErrorSink error_sink)
	: configuration_(std::move(configuration)),
	  shell_controller_(std::move(shell_controller)),
	  headless_renderer_(std::move(headless_renderer)),
	  display_commit_backend_(display_commit_backend),
	  connection_factory_(std::move(connection_factory)), service_control_(service_control),
	  monotonic_clock_(monotonic_clock), error_sink_(std::move(error_sink))
{
}

PanelUiResult<PanelUiServiceSummary> PanelUiService::run()
{
	while (!service_control_.stop_requested()) {
		PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>> connection_result =
			[&]() {
				try {
					return connection_factory_();
				} catch (...) {
					return PanelUiResult<
						std::unique_ptr<LifecyclePresentationConnection>>::failure(
						service_error(
							PanelUiErrorCode::LifecycleServiceConnectorFailed,
							"Lifecycle connection factory did not return a structured result",
							"panel_ui_service.connect"));
				}
			}();
		if (!connection_result || !connection_result.value()) {
			const PanelUiError connection_error =
				connection_result
					? service_error(
						  PanelUiErrorCode::LifecycleServiceConnectorFailed,
						  "Lifecycle connection factory returned no connection",
						  "panel_ui_service.connect")
					: connection_result.error();
			auto reported = report_error(connection_error);
			if (!reported) {
				return PanelUiResult<PanelUiServiceSummary>::failure(reported.error());
			}
			++consecutive_failure_count_;
			++summary_.reconnect_failure_count;
			if (consecutive_failure_count_ >=
			    configuration_.maximum_consecutive_failures) {
				return failure_limit_error(connection_error);
			}
			auto waited = wait_before_reconnect();
			if (!waited) {
				return PanelUiResult<PanelUiServiceSummary>::failure(waited.error());
			}
			if (!waited.value()) {
				return PanelUiResult<PanelUiServiceSummary>::success(summary_);
			}
			continue;
		}

		auto connection = std::move(connection_result).value();
		++summary_.accepted_connection_count;
		while (!service_control_.stop_requested() && connection->connected()) {
			const auto now = monotonic_clock_.now();
			if (now > std::chrono::steady_clock::time_point::max() -
				  configuration_.lifecycle_receive_timeout) {
				connection->close();
				return PanelUiResult<PanelUiServiceSummary>::failure(service_error(
					PanelUiErrorCode::LifecycleServiceDeadlineOverflow,
					"Lifecycle receive deadline is not representable",
					"panel_ui_service.receive"));
			}
			const auto exchange_result =
				connection->receive_render_and_acknowledge_until(
					now + configuration_.lifecycle_receive_timeout,
					[this](const bhp::panel_mode_protocol::Snapshot &snapshot) {
						return render_and_commit(snapshot);
					});
			if (!exchange_result) {
				connection->close();
				auto reported = report_error(exchange_result.error());
				if (!reported) {
					return PanelUiResult<PanelUiServiceSummary>::failure(
						reported.error());
				}
				++consecutive_failure_count_;
				++summary_.reconnect_failure_count;
				if (consecutive_failure_count_ >=
				    configuration_.maximum_consecutive_failures) {
					return failure_limit_error(exchange_result.error());
				}
				auto waited = wait_before_reconnect();
				if (!waited) {
					return PanelUiResult<PanelUiServiceSummary>::failure(
						waited.error());
				}
				if (!waited.value()) {
					return PanelUiResult<PanelUiServiceSummary>::success(summary_);
				}
				break;
			}

			++summary_.presentation_attempt_count;
			if (exchange_result.value().presentation_result ==
			    bhp::panel_mode_protocol::PresentationResult::Committed) {
				++summary_.committed_presentation_count;
			} else {
				++summary_.failed_presentation_count;
			}
			consecutive_failure_count_ = 0U;
		}
		connection->close();
	}
	return PanelUiResult<PanelUiServiceSummary>::success(summary_);
}

PanelUiResult<LifecycleRenderCommit> PanelUiService::render_and_commit(
	const bhp::panel_mode_protocol::Snapshot &snapshot)
{
	const ShellRenderModel render_model{
		snapshot.display_mode, transition_for_snapshot(snapshot),
		configuration_.active_surface_id, {}};
	auto frame_result = headless_renderer_->render(render_model);
	if (!frame_result) {
		return PanelUiResult<LifecycleRenderCommit>::failure(frame_result.error());
	}
	PanelUiResult<LifecycleRenderCommit> commit_result = [&]() {
		try {
			return display_commit_backend_.commit(snapshot, frame_result.value());
		} catch (...) {
			return PanelUiResult<LifecycleRenderCommit>::failure(service_error(
				PanelUiErrorCode::LifecycleRenderFailed,
				"Display commit backend did not return a structured result",
				"panel_ui_service.render_and_commit"));
		}
	}();
	if (!commit_result) {
		return commit_result;
	}
	if (!presentation_result_is_valid(commit_result.value().presentation_result) ||
	    commit_result.value().render_generation !=
		    frame_result.value().render_generation ||
	    commit_result.value().render_generation == 0U ||
	    commit_result.value().render_generation >
		    bhp::panel_mode_protocol::kMaximumSafeJsonInteger) {
		return PanelUiResult<LifecycleRenderCommit>::failure(service_error(
			PanelUiErrorCode::LifecycleDisplayCommitEvidenceInvalid,
			"Display commit evidence does not match the exact validated frame generation",
			"panel_ui_service.render_and_commit",
			{{"field", "lifecycle_render_commit"}}));
	}
	return commit_result;
}

PanelUiResult<bool> PanelUiService::report_error(const PanelUiError &error)
{
	try {
		error_sink_(error);
		return PanelUiResult<bool>::success(true);
	} catch (...) {
		return PanelUiResult<bool>::failure(service_error(
			PanelUiErrorCode::LifecycleServiceDiagnosticFailed,
			"Panel UI diagnostic sink did not accept a structured error",
			"panel_ui_service.report_error"));
	}
}

PanelUiResult<bool> PanelUiService::wait_before_reconnect()
{
	if (service_control_.stop_requested()) {
		return PanelUiResult<bool>::success(false);
	}
	auto wait_result = service_control_.wait_for(configuration_.reconnect_delay);
	if (!wait_result) {
		return wait_result;
	}
	return PanelUiResult<bool>::success(wait_result.value() &&
					    !service_control_.stop_requested());
}

PanelUiResult<PanelUiServiceSummary> PanelUiService::failure_limit_error(
	const PanelUiError &last_error) const
{
	return PanelUiResult<PanelUiServiceSummary>::failure(service_error(
		PanelUiErrorCode::LifecycleServiceFailureLimitReached,
		"Panel UI stopped after reaching the explicit consecutive lifecycle failure ceiling",
		"panel_ui_service.run",
		{{"failure_count", std::to_string(consecutive_failure_count_)},
		 {"last_error_code", std::string(last_error.code_string())}}));
}

} // namespace house_panel::panel_ui
