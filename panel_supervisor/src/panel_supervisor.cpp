#include "panel_supervisor/panel_supervisor.hpp"

#include "panel_supervisor/error_code.hpp"

#include "bhp/structured_log_level.h"

#include <chrono>
#include <exception>
#include <map>
#include <string>
#include <utility>

namespace house_panel::supervisor {
namespace {

long long elapsed_milliseconds(const std::chrono::steady_clock::time_point started_at) noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		       std::chrono::steady_clock::now() - started_at)
		.count();
}

std::string bool_string(const bool value) { return value ? "true" : "false"; }

std::map<std::string, std::string> port_error_context(const char *event, const char *operation)
{
	return {{"event", event}, {"operation", operation}};
}

template <typename Callable>
auto invoke_adapter(Callable &&callable, const char *adapter_name, const char *operation_name)
	-> decltype(callable())
{
	using AdapterResult = decltype(callable());
	try {
		return std::forward<Callable>(callable)();
	} catch (const std::exception &) {
		return AdapterResult::failure(
			SupervisorError(ErrorCode::AdapterUnavailable,
					"Adapter violated its explicit-result contract by throwing "
					"a standard exception",
					{{"adapter", adapter_name},
					 {"exception_kind", "standard_exception"},
					 {"operation", operation_name}}));
	} catch (...) {
		return AdapterResult::failure(
			SupervisorError(ErrorCode::AdapterUnavailable,
					"Adapter violated its explicit-result contract by throwing "
					"a non-standard exception",
					{{"adapter", adapter_name},
					 {"exception_kind", "non_standard_exception"},
					 {"operation", operation_name}}));
	}
}

} // namespace

PanelSupervisor::PanelSupervisor(SupervisorConfig config, PanelPolicy policy,
				 PresenceSource &presence_source, Clock &clock,
				 SolarCalculator &solar_calculator, Backlight &backlight,
				 UiModeNotifier &ui_mode_notifier, Suspend &suspend,
				 bhp::StructuredLogger &logger)
    : config_(std::move(config)), policy_(std::move(policy)), presence_source_(presence_source),
      clock_(clock), solar_calculator_(solar_calculator), backlight_(backlight),
      ui_mode_notifier_(ui_mode_notifier), suspend_(suspend), logger_(logger)
{
}

Result<PanelState> PanelSupervisor::start(std::string trace_id)
{
	const auto validation = validate_trace_id(trace_id);
	if (!validation) {
		return Result<PanelState>::failure(validation.error());
	}
	return recompute(SupervisorEvent::Start, trace_id);
}

Result<PanelState> PanelSupervisor::poll(std::string trace_id)
{
	const auto validation = validate_trace_id(trace_id);
	if (!validation) {
		return Result<PanelState>::failure(validation.error());
	}
	return recompute(SupervisorEvent::Poll, trace_id);
}

Result<PanelState> PanelSupervisor::resume(std::string trace_id)
{
	const auto validation = validate_trace_id(trace_id);
	if (!validation) {
		return Result<PanelState>::failure(validation.error());
	}

	state_.system_state = SystemState::RecomputingAfterResume;
	try {
		logger_.log(bhp::StructuredLogLevel::info, "resume_recomputation_started", trace_id,
			    {{"policy_revision", std::to_string(state_.policy_revision)},
			     {"system_state", std::string(to_string(state_.system_state))}});
	} catch (const std::exception &) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(
			SupervisorError(ErrorCode::StructuredLogWriteFailed,
					"Failed to log the start of panel resume recomputation",
					{{"logging_exception_kind", "standard_exception"},
					 {"operation", "resume"}}));
	} catch (...) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(
			SupervisorError(ErrorCode::StructuredLogWriteFailed,
					"Failed to log the start of panel resume recomputation",
					{{"logging_exception_kind", "non_standard_exception"},
					 {"operation", "resume"}}));
	}

	return recompute(SupervisorEvent::Resume, trace_id);
}

const PanelState &PanelSupervisor::state() const noexcept { return state_; }

Result<PanelState> PanelSupervisor::recompute(const SupervisorEvent event,
					      const std::string &trace_id)
{
	const auto event_name = std::string(to_string(event));

	const auto clock_started_at = std::chrono::steady_clock::now();
	auto observed_at_result = invoke_adapter([this]() { return clock_.now(); }, "clock", "now");
	if (!observed_at_result) {
		const auto error = SupervisorError::wrap(
			ErrorCode::ClockReadFailed,
			"Failed to read the supervisor clock during panel state recomputation",
			observed_at_result.error(),
			port_error_context(event_name.c_str(), "clock.now"),
			observed_at_result.error().retryable());
		const auto log_result =
			log_external_call(trace_id, "clock", "now",
					  elapsed_milliseconds(clock_started_at), false, &error);
		if (!log_result) {
			state_.system_state = SystemState::Faulted;
			return Result<PanelState>::failure(log_result.error());
		}
		return fail_cycle(trace_id, event, error);
	}
	auto log_result = log_external_call(trace_id, "clock", "now",
					    elapsed_milliseconds(clock_started_at), true);
	if (!log_result) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(log_result.error());
	}
	const auto observed_at = observed_at_result.value();

	const auto presence_started_at = std::chrono::steady_clock::now();
	auto presence_result =
		invoke_adapter([this]() { return presence_source_.presence_detected(); },
			       "presence_source", "presence_detected");
	if (!presence_result) {
		const auto error = SupervisorError::wrap(
			ErrorCode::PresenceReadFailed,
			"Failed to read the presence source during panel state recomputation",
			presence_result.error(),
			port_error_context(event_name.c_str(), "presence_source.presence_detected"),
			presence_result.error().retryable());
		log_result =
			log_external_call(trace_id, "presence_source", "presence_detected",
					  elapsed_milliseconds(presence_started_at), false, &error);
		if (!log_result) {
			state_.system_state = SystemState::Faulted;
			return Result<PanelState>::failure(log_result.error());
		}
		return fail_cycle(trace_id, event, error);
	}
	log_result = log_external_call(trace_id, "presence_source", "presence_detected",
				       elapsed_milliseconds(presence_started_at), true);
	if (!log_result) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(log_result.error());
	}

	const auto solar_started_at = std::chrono::steady_clock::now();
	auto solar_result = invoke_adapter(
		[this, observed_at]() { return solar_calculator_.calculate(observed_at); },
		"solar_calculator", "calculate");
	if (!solar_result) {
		const auto error = SupervisorError::wrap(
			ErrorCode::SolarCalculationFailed,
			"Failed to calculate solar state during panel state recomputation",
			solar_result.error(),
			port_error_context(event_name.c_str(), "solar_calculator.calculate"),
			solar_result.error().retryable());
		log_result =
			log_external_call(trace_id, "solar_calculator", "calculate",
					  elapsed_milliseconds(solar_started_at), false, &error);
		if (!log_result) {
			state_.system_state = SystemState::Faulted;
			return Result<PanelState>::failure(log_result.error());
		}
		return fail_cycle(trace_id, event, error);
	}
	log_result = log_external_call(trace_id, "solar_calculator", "calculate",
				       elapsed_milliseconds(solar_started_at), true);
	if (!log_result) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(log_result.error());
	}

	const PolicyObservation observation{presence_result.value(), solar_result.value(),
					    observed_at, event};
	auto decision_result = policy_.evaluate(state_, observation, config_);
	if (!decision_result) {
		return fail_cycle(trace_id, event, decision_result.error());
	}
	const auto decision = decision_result.value();

	state_.presence_state = decision.presence_state;
	state_.solar_state = decision.solar_state;
	state_.last_presence_detected_at = decision.last_presence_detected_at;
	state_.policy_revision = decision.policy_revision;

	const UiModeNotification ui_mode_notification{
		decision.ui_mode,
		decision.presence_state,
		decision.solar_state,
		decision.target_system_state,
		decision.brightness_percent,
		observed_at,
	};

	auto apply_backlight = [this, &decision, &event_name, &trace_id]() -> Result<void> {
		const auto started_at = std::chrono::steady_clock::now();
		auto result = invoke_adapter(
			[this, &decision]() {
				return backlight_.set_brightness_percent(
					decision.brightness_percent);
			},
			"backlight", "set_brightness_percent");
		if (!result) {
			auto context = port_error_context(event_name.c_str(),
							  "backlight.set_brightness_percent");
			context.emplace("brightness_percent",
					std::to_string(decision.brightness_percent));
			const auto error = SupervisorError::wrap(
				ErrorCode::BacklightWriteFailed,
				"Failed to apply policy brightness during panel state "
				"recomputation",
				result.error(), std::move(context), result.error().retryable());
			const auto external_log_result =
				log_external_call(trace_id, "backlight", "set_brightness_percent",
						  elapsed_milliseconds(started_at), false, &error);
			if (!external_log_result) {
				return Result<void>::failure(external_log_result.error());
			}
			return Result<void>::failure(error);
		}
		state_.applied_brightness_percent = decision.brightness_percent;
		const auto external_log_result =
			log_external_call(trace_id, "backlight", "set_brightness_percent",
					  elapsed_milliseconds(started_at), true);
		if (!external_log_result) {
			return Result<void>::failure(external_log_result.error());
		}
		return Result<void>::success();
	};

	auto notify_ui_mode = [this, &decision, &event_name, &trace_id,
			       &ui_mode_notification]() -> Result<void> {
		const auto started_at = std::chrono::steady_clock::now();
		auto result = invoke_adapter(
			[this, &ui_mode_notification]() {
				return ui_mode_notifier_.notify_ui_mode(ui_mode_notification);
			},
			"ui_mode_notifier", "notify_ui_mode");
		if (!result) {
			auto context = port_error_context(event_name.c_str(),
							  "ui_mode_notifier.notify_ui_mode");
			context.emplace("ui_mode", std::string(to_string(decision.ui_mode)));
			const auto error = SupervisorError::wrap(
				ErrorCode::UiModeNotificationFailed,
				"Failed to notify UI mode during panel state recomputation",
				result.error(), std::move(context), result.error().retryable());
			const auto external_log_result =
				log_external_call(trace_id, "ui_mode_notifier", "notify_ui_mode",
						  elapsed_milliseconds(started_at), false, &error);
			if (!external_log_result) {
				return Result<void>::failure(external_log_result.error());
			}
			return Result<void>::failure(error);
		}
		state_.notified_ui_mode = decision.ui_mode;
		const auto external_log_result =
			log_external_call(trace_id, "ui_mode_notifier", "notify_ui_mode",
					  elapsed_milliseconds(started_at), true);
		if (!external_log_result) {
			return Result<void>::failure(external_log_result.error());
		}
		return Result<void>::success();
	};

	auto apply_output = [this, &trace_id, event](auto &&operation) -> Result<PanelState> {
		const auto result = operation();
		if (result) {
			return Result<PanelState>::success(state_);
		}
		if (result.error().code() == ErrorCode::StructuredLogWriteFailed) {
			state_.system_state = SystemState::Faulted;
			return Result<PanelState>::failure(result.error());
		}
		return fail_cycle(trace_id, event, result.error());
	};

	const bool blank_before_mode = decision.target_system_state == SystemState::Suspended ||
				       decision.brightness_percent == 0;
	if (blank_before_mode) {
		const auto backlight_output = apply_output(apply_backlight);
		if (!backlight_output) {
			return backlight_output;
		}
		const auto ui_output = apply_output(notify_ui_mode);
		if (!ui_output) {
			return ui_output;
		}
	} else {
		const auto ui_output = apply_output(notify_ui_mode);
		if (!ui_output) {
			return ui_output;
		}
		const auto backlight_output = apply_output(apply_backlight);
		if (!backlight_output) {
			return backlight_output;
		}
	}

	if (decision.request_suspend) {
		const auto suspend_started_at = std::chrono::steady_clock::now();
		auto suspend_result =
			invoke_adapter([this]() { return suspend_.request_suspend(); }, "suspend",
				       "request_suspend");
		if (!suspend_result) {
			const auto error = SupervisorError::wrap(
				ErrorCode::SuspendRequestFailed,
				"Failed to request system suspension during panel state "
				"recomputation",
				suspend_result.error(),
				port_error_context(event_name.c_str(), "suspend.request_suspend"),
				suspend_result.error().retryable());
			log_result = log_external_call(trace_id, "suspend", "request_suspend",
						       elapsed_milliseconds(suspend_started_at),
						       false, &error);
			if (!log_result) {
				state_.system_state = SystemState::Faulted;
				return Result<PanelState>::failure(log_result.error());
			}
			return fail_cycle(trace_id, event, error);
		}
		log_result = log_external_call(trace_id, "suspend", "request_suspend",
					       elapsed_milliseconds(suspend_started_at), true);
		if (!log_result) {
			state_.system_state = SystemState::Faulted;
			return Result<PanelState>::failure(log_result.error());
		}
	}

	state_.system_state = decision.target_system_state;
	try {
		logger_.log(bhp::StructuredLogLevel::info, "panel_state_recomputed", trace_id,
			    {{"brightness_percent", std::to_string(decision.brightness_percent)},
			     {"event", event_name},
			     {"policy_revision", std::to_string(decision.policy_revision)},
			     {"presence_detected", bool_string(observation.presence_detected)},
			     {"presence_state", std::string(to_string(decision.presence_state))},
			     {"solar_state", std::string(to_string(decision.solar_state))},
			     {"system_state", std::string(to_string(decision.target_system_state))},
			     {"ui_mode", std::string(to_string(decision.ui_mode))}});
	} catch (const std::exception &) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(SupervisorError(
			ErrorCode::StructuredLogWriteFailed,
			"Failed to log the completed panel state recomputation",
			{{"event", event_name}, {"logging_exception_kind", "standard_exception"}}));
	} catch (...) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(
			SupervisorError(ErrorCode::StructuredLogWriteFailed,
					"Failed to log the completed panel state recomputation",
					{{"event", event_name},
					 {"logging_exception_kind", "non_standard_exception"}}));
	}

	return Result<PanelState>::success(state_);
}

Result<void> PanelSupervisor::log_external_call(const std::string &trace_id, std::string target,
						std::string operation, const long long duration_ms,
						const bool succeeded, const SupervisorError *error)
{
	std::map<std::string, std::string> context{
		{"duration_ms", std::to_string(duration_ms)},
		{"operation", std::move(operation)},
		{"status", succeeded ? "success" : "error"},
		{"target", std::move(target)},
	};

	try {
		if (succeeded) {
			logger_.log(bhp::StructuredLogLevel::debug, "external_call_completed",
				    trace_id, context);
		} else {
			context.emplace("retryable",
					bool_string(error != nullptr && error->retryable()));
			logger_.log_error(
				"external_call_failed", trace_id,
				error == nullptr ? "PANEL_SUPERVISOR.UNSPECIFIED_EXTERNAL_FAILURE"
						 : std::string(to_string(error->code())),
				error == nullptr ? "External call failed without a typed error"
						 : error->message(),
				"not_available_explicit_error_return", context);
		}
	} catch (const std::exception &) {
		return Result<void>::failure(
			SupervisorError(ErrorCode::StructuredLogWriteFailed,
					"Failed to write the structured external-call log record",
					{{"logging_exception_kind", "standard_exception"},
					 {"operation", context.at("operation")},
					 {"target", context.at("target")}}));
	} catch (...) {
		return Result<void>::failure(
			SupervisorError(ErrorCode::StructuredLogWriteFailed,
					"Failed to write the structured external-call log record",
					{{"logging_exception_kind", "non_standard_exception"},
					 {"operation", context.at("operation")},
					 {"target", context.at("target")}}));
	}

	return Result<void>::success();
}

Result<PanelState> PanelSupervisor::fail_cycle(const std::string &trace_id,
					       const SupervisorEvent event, SupervisorError error,
					       std::map<std::string, std::string> context)
{
	state_.system_state = SystemState::Faulted;
	context.emplace("event", std::string(to_string(event)));
	context.emplace("input_presence_state", std::string(to_string(state_.presence_state)));
	context.emplace("input_solar_state", std::string(to_string(state_.solar_state)));
	context.emplace("policy_revision", std::to_string(state_.policy_revision));
	context.emplace("retryable", bool_string(error.retryable()));
	context.emplace("system_state", std::string(to_string(state_.system_state)));
	for (const auto &[key, value] : error.context()) {
		context.emplace("error_context_" + key, value);
	}

	try {
		logger_.log_error("panel_state_recomputation_failed", trace_id,
				  std::string(to_string(error.code())), error.message(),
				  "not_available_explicit_error_return", context);
	} catch (const std::exception &) {
		return Result<PanelState>::failure(SupervisorError::wrap(
			ErrorCode::ErrorReportingFailed,
			"Failed to report a typed panel state recomputation error", error,
			{{"logging_exception_kind", "standard_exception"},
			 {"original_error_code", std::string(to_string(error.code()))}}));
	} catch (...) {
		return Result<PanelState>::failure(SupervisorError::wrap(
			ErrorCode::ErrorReportingFailed,
			"Failed to report a typed panel state recomputation error", error,
			{{"logging_exception_kind", "non_standard_exception"},
			 {"original_error_code", std::string(to_string(error.code()))}}));
	}

	return Result<PanelState>::failure(std::move(error));
}

Result<void> PanelSupervisor::validate_trace_id(const std::string &trace_id) const
{
	if (trace_id.empty()) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::InvalidTraceId,
			"Failed to run panel supervisor cycle because trace_id is empty",
			{{"operation", "panel_state_recomputation"}}));
	}
	return Result<void>::success();
}

} // namespace house_panel::supervisor
