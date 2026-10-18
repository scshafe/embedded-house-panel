#include "panel_supervisor/panel_supervisor.hpp"

#include "panel_supervisor/error_code.hpp"

#include "bhp/application_identity.h"
#include "bhp/panel_mode_protocol/types.h"
#include "bhp/structured_log_level.h"

#include <chrono>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

std::string error_code_chain(const SupervisorError &error)
{
	std::string chain(to_string(error.code()));
	auto cause = error.cause();
	while (cause != nullptr) {
		chain += " <- ";
		chain += to_string(cause->code());
		cause = cause->cause();
	}
	return chain;
}

std::string_view presentation_status(const PresentationOutcome &outcome) noexcept
{
	if (std::holds_alternative<PresentationCommitted>(outcome)) {
		return "committed";
	}
	if (std::holds_alternative<PresentationNotCommitted>(outcome)) {
		return "not_committed";
	}
	return "failed";
}

std::string_view protocol_display_mode(const UiMode ui_mode) noexcept
{
	switch (ui_mode) {
	case UiMode::Day:
		return "day_active";
	case UiMode::Night:
		return "night_red";
	case UiMode::Suspended:
		return "suspended";
	}
	return "invalid";
}

bool is_lowercase_hex(const std::string_view value, const std::size_t expected_size,
		      const bool require_nonzero) noexcept
{
	if (value.size() != expected_size) {
		return false;
	}
	bool contains_nonzero = false;
	for (const char character : value) {
		if (!((character >= '0' && character <= '9') ||
		      (character >= 'a' && character <= 'f'))) {
			return false;
		}
		contains_nonzero = contains_nonzero || character != '0';
	}
	return !require_nonzero || contains_nonzero;
}

std::optional<std::string_view> invalid_committed_presentation_field(
	const PresentationCommitted &committed, const PendingPresentation &pending,
	const UiModeNotification &notification,
	const PresentationCommitted *last_committed) noexcept
{
	using bhp::panel_mode_protocol::kMaximumSafeJsonInteger;
	if (committed.appliance_release_id != bhp::kApplianceReleaseId) {
		return "appliance_release_id";
	}
	if (pending.publication_sequence == 0U ||
	    pending.publication_sequence > kMaximumSafeJsonInteger ||
	    committed.publication_sequence != pending.publication_sequence) {
		return "publication_sequence";
	}
	if (committed.render_generation == 0U ||
	    committed.render_generation > kMaximumSafeJsonInteger) {
		return "render_generation";
	}
	if (!is_lowercase_hex(committed.supervisor_session_id, 32U, true)) {
		return "supervisor_session_id";
	}
	if (!is_lowercase_hex(committed.connection_challenge, 32U, true)) {
		return "connection_challenge";
	}
	constexpr std::string_view digest_prefix = "sha256:";
	if (committed.snapshot_id.size() != digest_prefix.size() + 64U ||
	    committed.snapshot_id.compare(0U, digest_prefix.size(), digest_prefix) != 0 ||
	    !is_lowercase_hex(
		std::string_view(committed.snapshot_id).substr(digest_prefix.size()), 64U,
		false)) {
		return "snapshot_id";
	}
	if (committed.display_mode != protocol_display_mode(notification.ui_mode)) {
		return "display_mode";
	}
	if (!pending.expected_commitment.has_value()) {
		return "expected_commitment";
	}
	const auto &expected = *pending.expected_commitment;
	if (committed.appliance_release_id != expected.appliance_release_id) {
		return "appliance_release_id";
	}
	if (committed.supervisor_session_id != expected.supervisor_session_id) {
		return "supervisor_session_id";
	}
	if (committed.connection_challenge != expected.connection_challenge) {
		return "connection_challenge";
	}
	if (committed.snapshot_id != expected.snapshot_id) {
		return "snapshot_id";
	}
	if (committed.display_mode != expected.display_mode) {
		return "display_mode";
	}
	if (expected.minimum_render_generation_exclusive >= kMaximumSafeJsonInteger ||
	    committed.render_generation <= expected.minimum_render_generation_exclusive) {
		return "render_generation";
	}
	if (last_committed != nullptr) {
		if (committed.supervisor_session_id !=
		    last_committed->supervisor_session_id) {
			return "supervisor_session_id";
		}
		if (committed.publication_sequence <=
		    last_committed->publication_sequence) {
			return "publication_sequence";
		}
		if (committed.connection_challenge ==
			    last_committed->connection_challenge &&
		    committed.render_generation <= last_committed->render_generation) {
			return "render_generation";
		}
	}
	return std::nullopt;
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

	auto apply_backlight = [this, &event_name, &trace_id](
				 const int brightness_percent) -> Result<void> {
		const auto started_at = std::chrono::steady_clock::now();
		auto result = invoke_adapter(
			[this, brightness_percent]() {
				return backlight_.set_brightness_percent(brightness_percent);
			},
			"backlight", "set_brightness_percent");
		if (!result) {
			auto context = port_error_context(event_name.c_str(),
							  "backlight.set_brightness_percent");
			context.emplace("brightness_percent", std::to_string(brightness_percent));
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
		state_.applied_brightness_percent = brightness_percent;
		const auto external_log_result =
			log_external_call(trace_id, "backlight", "set_brightness_percent",
					  elapsed_milliseconds(started_at), true);
		if (!external_log_result) {
			return Result<void>::failure(external_log_result.error());
		}
		return Result<void>::success();
	};

	auto publish_and_await_presentation =
		[this, &decision, event, &event_name, &trace_id,
		 &ui_mode_notification]() -> Result<PresentationOutcome> {
		const auto publish_started_at = std::chrono::steady_clock::now();
		auto publish_result = invoke_adapter(
			[this, &ui_mode_notification]() {
				return ui_mode_notifier_.publish(ui_mode_notification);
			},
			"ui_mode_notifier", "publish");
		const auto publish_duration_ms = elapsed_milliseconds(publish_started_at);
		if (!publish_result) {
			auto context = port_error_context(event_name.c_str(),
							  "ui_mode_notifier.publish");
			context.emplace("ui_mode", std::string(to_string(decision.ui_mode)));
			const auto error = SupervisorError::wrap(
				ErrorCode::UiModeNotificationFailed,
				"Failed to publish the UI presentation snapshot during panel state "
				"recomputation",
				publish_result.error(), std::move(context),
				publish_result.error().retryable());
			PresentationOutcome outcome = PresentationFailed{
				PresentationFailureReason::PublicationFailed, error};
			const auto warning_result = log_degraded_presentation(
				trace_id, event, decision, outcome, "publish",
				publish_duration_ms);
			if (!warning_result) {
				return Result<PresentationOutcome>::failure(warning_result.error());
			}
			return Result<PresentationOutcome>::success(std::move(outcome));
		}
		state_.notified_ui_mode = decision.ui_mode;
		auto external_log_result =
			log_external_call(trace_id, "ui_mode_notifier", "publish",
					  publish_duration_ms, true);
		if (!external_log_result) {
			return Result<PresentationOutcome>::failure(external_log_result.error());
		}

		const auto pending_presentation = publish_result.value();
		const auto presentation_deadline =
			std::chrono::steady_clock::now() + config_.presentation_timeout();
		const auto await_started_at = std::chrono::steady_clock::now();
		PresentationOutcome outcome = PresentationNotCommitted{
			PresentationNotCommittedReason::AcknowledgementUnavailable};
		bool adapter_returned_outcome = false;
		try {
			outcome = ui_mode_notifier_.await_presentation(
				pending_presentation, presentation_deadline);
			adapter_returned_outcome = true;
		} catch (const std::exception &) {
			outcome = PresentationFailed{
				PresentationFailureReason::AdapterContractViolation,
				SupervisorError(
					ErrorCode::AdapterUnavailable,
					"UI presentation adapter violated its explicit-outcome "
					"contract by throwing a standard exception",
					{{"adapter", "ui_mode_notifier"},
					 {"exception_kind", "standard_exception"},
					 {"operation", "await_presentation"}})};
		} catch (...) {
			outcome = PresentationFailed{
				PresentationFailureReason::AdapterContractViolation,
				SupervisorError(
					ErrorCode::AdapterUnavailable,
					"UI presentation adapter violated its explicit-outcome "
					"contract by throwing a non-standard exception",
					{{"adapter", "ui_mode_notifier"},
					 {"exception_kind", "non_standard_exception"},
					 {"operation", "await_presentation"}})};
		}
		const auto await_duration_ms = elapsed_milliseconds(await_started_at);
		if (adapter_returned_outcome) {
			external_log_result =
				log_external_call(trace_id, "ui_mode_notifier",
						  "await_presentation", await_duration_ms, true);
			if (!external_log_result) {
				return Result<PresentationOutcome>::failure(
					external_log_result.error());
			}
		}
		if (const auto *committed = std::get_if<PresentationCommitted>(&outcome)) {
			const auto invalid_field = invalid_committed_presentation_field(
				*committed, pending_presentation, ui_mode_notification,
				last_committed_presentation_.has_value()
					? &*last_committed_presentation_
					: nullptr);
			if (invalid_field.has_value()) {
				outcome = PresentationFailed{
					PresentationFailureReason::AdapterContractViolation,
					SupervisorError(
						ErrorCode::UiPresentationBindingInvalid,
						"UI presentation adapter returned a commitment that was not "
						"bound to the exact pending lifecycle publication",
						{{"field", std::string(*invalid_field)},
						 {"operation",
						  "ui_mode_notifier.await_presentation"}})};
			} else {
				last_committed_presentation_ = *committed;
			}
		}
		if (!std::holds_alternative<PresentationCommitted>(outcome)) {
			const auto warning_result = log_degraded_presentation(
				trace_id, event, decision, outcome, "await_presentation",
				await_duration_ms);
			if (!warning_result) {
				return Result<PresentationOutcome>::failure(warning_result.error());
			}
		}
		return Result<PresentationOutcome>::success(std::move(outcome));
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

	// ADR 0011: a lit panel whose committed display mode does not change stays lit
	// while the UI commits the same mode; every other cycle blanks first.
	const bool keep_lit = state_.applied_brightness_percent.value_or(0) != 0 &&
			      decision.ui_mode != UiMode::Suspended &&
			      last_committed_presentation_.has_value() &&
			      last_committed_presentation_->display_mode ==
				      protocol_display_mode(decision.ui_mode);
	if (!keep_lit) {
		const auto blank_output = apply_output([&apply_backlight]() {
			return apply_backlight(0);
		});
		if (!blank_output) {
			return blank_output;
		}
	}

	const auto presentation_result = publish_and_await_presentation();
	if (!presentation_result) {
		state_.system_state = SystemState::Faulted;
		return Result<PanelState>::failure(presentation_result.error());
	}
	const PresentationOutcome presentation_outcome = presentation_result.value();
	const bool presentation_committed =
		std::holds_alternative<PresentationCommitted>(presentation_outcome);
	if (presentation_committed && decision.brightness_percent != 0) {
		const auto brightness_output = apply_output([&apply_backlight, &decision]() {
			return apply_backlight(decision.brightness_percent);
		});
		if (!brightness_output) {
			return brightness_output;
		}
	} else if (state_.applied_brightness_percent.value_or(0) != 0) {
		// Only a committed presentation may leave the panel lit.
		const auto blank_output = apply_output([&apply_backlight]() {
			return apply_backlight(0);
		});
		if (!blank_output) {
			return blank_output;
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
			    {{"brightness_percent",
			      std::to_string(state_.applied_brightness_percent.value_or(0))},
			     {"event", event_name},
			     {"policy_revision", std::to_string(decision.policy_revision)},
			     {"presentation_status",
			      std::string(presentation_status(presentation_outcome))},
			     {"presence_detected", bool_string(observation.presence_detected)},
			     {"presence_state", std::string(to_string(decision.presence_state))},
			     {"requested_brightness_percent",
			      std::to_string(decision.brightness_percent)},
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

Result<void> PanelSupervisor::log_degraded_presentation(
	const std::string &trace_id, const SupervisorEvent event,
	const PolicyDecision &decision, const PresentationOutcome &outcome,
	std::string operation, const long long duration_ms)
{
	std::map<std::string, std::string> context{
		{"brightness_percent", "0"},
		{"degraded", "true"},
		{"duration_ms", std::to_string(duration_ms)},
		{"event", std::string(to_string(event))},
		{"operation", std::move(operation)},
		{"policy_revision", std::to_string(decision.policy_revision)},
		{"presentation_status", std::string(presentation_status(outcome))},
		{"requested_brightness_percent", std::to_string(decision.brightness_percent)},
		{"system_state", std::string(to_string(decision.target_system_state))},
		{"target", "ui_mode_notifier"},
		{"ui_mode", std::string(to_string(decision.ui_mode))},
	};

	if (const auto *not_committed = std::get_if<PresentationNotCommitted>(&outcome)) {
		context.emplace("degraded_reason", std::string(to_string(not_committed->reason)));
	} else if (const auto *failed = std::get_if<PresentationFailed>(&outcome)) {
		context.emplace("degraded_reason", std::string(to_string(failed->reason)));
		context.emplace("error_code", std::string(to_string(failed->error.code())));
		context.emplace("error_message", failed->error.message());
		context.emplace("retryable", bool_string(failed->error.retryable()));
		context.emplace("stack_trace", error_code_chain(failed->error));
		for (const auto &[key, value] : failed->error.context()) {
			context.emplace("error_context_" + key, value);
		}
	}

	try {
		logger_.log(bhp::StructuredLogLevel::warn, "presentation_degraded", trace_id,
			    context);
	} catch (const std::exception &) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::StructuredLogWriteFailed,
			"Failed to write the degraded presentation warning",
			{{"logging_exception_kind", "standard_exception"},
			 {"operation", context.at("operation")},
			 {"presentation_status", context.at("presentation_status")}}));
	} catch (...) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::StructuredLogWriteFailed,
			"Failed to write the degraded presentation warning",
			{{"logging_exception_kind", "non_standard_exception"},
			 {"operation", context.at("operation")},
			 {"presentation_status", context.at("presentation_status")}}));
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
