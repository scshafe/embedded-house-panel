#include "panel_supervisor/daemon_startup_safety.hpp"
#include "panel_supervisor/daemon_stop_controller.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/evdev_presence_source.hpp"
#include "panel_supervisor/offline_solar_calculator.hpp"
#include "panel_supervisor/panel_policy.hpp"
#include "panel_supervisor/panel_supervisor.hpp"
#include "panel_supervisor/panel_supervisord_config.hpp"
#include "panel_supervisor/rate_limited_stop_aware_suspend.hpp"
#include "panel_supervisor/suspend.hpp"
#include "panel_supervisor/sysfs_backlight.hpp"
#include "panel_supervisor/sysfs_suspend.hpp"
#include "panel_supervisor/system_clock.hpp"
#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"

#include "bhp/structured_log_level.h"
#include "bhp/structured_logger.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

class DisabledSuspend final : public Suspend {
public:
	Result<void> request_suspend() override { return Result<void>::success(); }
};

std::string error_chain(const SupervisorError &error)
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

void log_failure(bhp::StructuredLogger &logger, const std::string &message,
		 const std::string &trace_id, const SupervisorError &error)
{
	logger.log_error(message, trace_id, std::string(to_string(error.code())), error.message(),
			 error_chain(error), error.context());
}

void log_failure_best_effort(bhp::StructuredLogger &logger, const std::string &message,
			     const std::string &trace_id,
			     const SupervisorError &error) noexcept
{
	try {
		log_failure(logger, message, trace_id, error);
	} catch (...) {
		// Process termination remains a hard failure when its logging sink is unavailable.
	}
}

int fail_after_backlight_construction(bhp::StructuredLogger &logger, Backlight &backlight,
				     const std::string &message,
				     const std::string &trace_id,
				     const SupervisorError &primary_error,
				     const int exit_status = 1) noexcept
{
	try {
		const auto blank_result = force_backlight_off_for_safety(backlight);
		log_failure_best_effort(logger, message, trace_id, primary_error);

		if (blank_result) {
			try {
				logger.log(
					bhp::StructuredLogLevel::warn,
					"fatal_exit_backlight_forced_off", trace_id,
					{{"brightness_percent", "0"},
					 {"display_state", "off"},
					 {"primary_error_code",
					  std::string(to_string(primary_error.code()))},
					 {"primary_error_message", primary_error.message()},
					 {"primary_stack_trace", error_chain(primary_error)}});
			} catch (...) {
				// The nonzero exit remains the observable logging-failure result.
			}
			return exit_status;
		}

		try {
			const auto safety_error = make_fatal_exit_backlight_blank_error(
				primary_error, blank_result.error(), error_chain(primary_error));
			log_failure_best_effort(logger, "fatal_exit_backlight_blank_failed",
						trace_id, safety_error);
		} catch (...) {
			// Both failures still produce the requested nonzero process result.
		}
	} catch (...) {
		// Allocation failure cannot turn the already-attempted safety action into success.
		log_failure_best_effort(logger, message, trace_id, primary_error);
	}
	return exit_status;
}

Result<std::string> next_trace_id(std::uint64_t &sequence)
{
	if (sequence == std::numeric_limits<std::uint64_t>::max()) {
		return Result<std::string>::failure(
			SupervisorError(ErrorCode::RevisionOverflow,
					"Failed to run another daemon cycle because its trace "
					"sequence is exhausted",
					{{"operation", "panel_supervisord.next_trace_id"},
					 {"trace_sequence", std::to_string(sequence)}}));
	}
	++sequence;
	return Result<std::string>::success("panel-supervisord-" +
					    std::to_string(static_cast<long long>(::getpid())) +
					    "-" + std::to_string(sequence));
}

Result<PanelState> resume_after_suspend_returns(PanelSupervisor &supervisor,
						SuspendMode suspend_mode,
						Result<PanelState> cycle_result,
						std::uint64_t &trace_sequence,
						bhp::StructuredLogger &logger,
						DaemonStopController &stop_controller)
{
	if (suspend_mode == SuspendMode::Disabled || !cycle_result) {
		return cycle_result;
	}
	while (cycle_result.value().system_state == SystemState::Suspended) {
		const auto trace_id_result = next_trace_id(trace_sequence);
		if (!trace_id_result) {
			return Result<PanelState>::failure(trace_id_result.error());
		}
		const std::string trace_id = trace_id_result.value();
		logger.log(bhp::StructuredLogLevel::info,
			   "suspend_write_returned_recomputing_resume", trace_id,
			   {{"suspend_mode", std::string(to_string(suspend_mode))}});
		cycle_result = supervisor.resume(trace_id);
		if (!cycle_result) {
			return cycle_result;
		}
		if (stop_controller.stop_requested()) {
			logger.log(bhp::StructuredLogLevel::info,
				   "shutdown_prevented_resuspend_after_resume", trace_id,
				   {{"suspend_mode", std::string(to_string(suspend_mode))}});
			return cycle_result;
		}
	}
	return cycle_result;
}

int run_daemon(bhp::StructuredLogger &logger)
{
	const auto config_result = PanelSupervisordConfig::from_environment();
	if (!config_result) {
		log_failure(logger, "daemon_configuration_rejected", "panel-supervisord-config",
			    config_result.error());
		return 2;
	}
	PanelSupervisordConfig config = config_result.value();

	auto stop_controller_result = DaemonStopController::create();
	if (!stop_controller_result) {
		log_failure(logger, "daemon_signal_setup_failed", "panel-supervisord-signals",
			    stop_controller_result.error());
		return 1;
	}
	std::unique_ptr<DaemonStopController> stop_controller =
		std::move(stop_controller_result).value();

	auto backlight_result =
		SysfsBacklight::create(config.backlight_path(), config.backlight_maximum_path());
	if (!backlight_result) {
		log_failure(logger, "backlight_adapter_creation_failed",
			    "panel-supervisord-startup", backlight_result.error());
		return 1;
	}
	SysfsBacklight backlight = std::move(backlight_result).value();
	try {
	const auto startup_blank_result = force_backlight_off_for_safety(backlight);
	if (!startup_blank_result) {
		return fail_after_backlight_construction(
			logger, backlight, "startup_backlight_blank_failed",
			"panel-supervisord-startup", startup_blank_result.error());
	}
	logger.log(bhp::StructuredLogLevel::info, "startup_backlight_forced_off",
		   "panel-supervisord-startup",
		   {{"brightness_percent", "0"}, {"display_state", "off"}});

	auto presence_result = config.presence_input_name().empty()
				       ? EvdevPresenceSource::create(config.presence_input_path())
				       : EvdevPresenceSource::create_from_exact_device_name(
						 config.presence_input_name());
	if (!presence_result) {
		return fail_after_backlight_construction(
			logger, backlight, "presence_adapter_creation_failed",
			"panel-supervisord-startup", presence_result.error());
	}
	auto solar_result = OfflineSolarCalculator::create(config.latitude_degrees(),
							   config.longitude_degrees());
	if (!solar_result) {
		return fail_after_backlight_construction(
			logger, backlight, "solar_adapter_creation_failed",
			"panel-supervisord-startup", solar_result.error());
	}
	auto notifier_result = UnixSocketUiModeNotifier::create(config.mode_socket_path());
	if (!notifier_result) {
		return fail_after_backlight_construction(
			logger, backlight, "ui_socket_creation_failed",
			"panel-supervisord-startup", notifier_result.error());
	}

	std::unique_ptr<Suspend> suspend;
	if (config.suspend_mode() == SuspendMode::Disabled) {
		suspend = std::make_unique<DisabledSuspend>();
	} else {
		auto suspend_result = SysfsSuspend::create(
			config.power_state_path(), std::string(to_string(config.suspend_mode())));
		if (!suspend_result) {
			return fail_after_backlight_construction(
				logger, backlight, "suspend_adapter_creation_failed",
				"panel-supervisord-startup", suspend_result.error());
		}
		suspend = std::make_unique<SysfsSuspend>(std::move(suspend_result).value());
	}

	EvdevPresenceSource presence_source = std::move(presence_result).value();
	OfflineSolarCalculator solar_calculator = std::move(solar_result).value();
	std::unique_ptr<UnixSocketUiModeNotifier> ui_mode_notifier =
		std::move(notifier_result).value();
	SystemClock clock;
	RateLimitedStopAwareSuspend stop_aware_suspend(
		*suspend, *stop_controller, config.suspend_retry_backoff());
	PanelSupervisor supervisor(config.supervisor_config(), PanelPolicy{}, presence_source,
				   clock, solar_calculator, backlight, *ui_mode_notifier,
				   stop_aware_suspend, logger);

	logger.log(bhp::StructuredLogLevel::info, "daemon_started", "panel-supervisord-startup",
		   {{"poll_interval_milliseconds", std::to_string(config.poll_interval().count())},
		    {"suspend_retry_backoff_milliseconds",
		     std::to_string(config.suspend_retry_backoff().count())},
		    {"suspend_mode", std::string(to_string(config.suspend_mode()))}});

	std::uint64_t trace_sequence = 0;
	bool startup_cycle_completed = false;
	while (!stop_controller->stop_requested() && !startup_cycle_completed) {
		auto trace_id_result = next_trace_id(trace_sequence);
		if (!trace_id_result) {
			return fail_after_backlight_construction(
				logger, backlight, "daemon_trace_creation_failed",
				"panel-supervisord-startup", trace_id_result.error());
		}
		const std::string trace_id = trace_id_result.value();
		auto cycle_result = supervisor.start(trace_id);
		if (!cycle_result &&
		    is_retryable_startup_clock_readiness_failure(cycle_result.error())) {
			logger.log(
				bhp::StructuredLogLevel::warn,
				"daemon_startup_clock_not_ready_retrying", trace_id,
				{{"display_state", "off"},
				 {"error_code", std::string(to_string(cycle_result.error().code()))},
				 {"error_message", cycle_result.error().message()},
				 {"retry_after_milliseconds",
				  std::to_string(config.poll_interval().count())},
				 {"retryable", "true"},
				 {"stack_trace", error_chain(cycle_result.error())}});
			const auto wait_result = stop_controller->wait_for(config.poll_interval());
			if (!wait_result) {
				return fail_after_backlight_construction(
					logger, backlight, "daemon_startup_retry_wait_failed",
					trace_id, wait_result.error());
			}
			continue;
		}
		cycle_result = resume_after_suspend_returns(
			supervisor, config.suspend_mode(), std::move(cycle_result), trace_sequence,
			logger, *stop_controller);
		if (!cycle_result) {
			return fail_after_backlight_construction(
				logger, backlight, "daemon_cycle_failed", trace_id,
				cycle_result.error());
		}
		startup_cycle_completed = true;
	}

	while (!stop_controller->stop_requested()) {
		const auto wait_result = stop_controller->wait_for(config.poll_interval());
		if (!wait_result) {
			return fail_after_backlight_construction(
				logger, backlight, "daemon_poll_wait_failed",
				"panel-supervisord-run", wait_result.error());
		}
		if (wait_result.value()) {
			break;
		}
		auto trace_id_result = next_trace_id(trace_sequence);
		if (!trace_id_result) {
			return fail_after_backlight_construction(
				logger, backlight, "daemon_trace_creation_failed",
				"panel-supervisord-run", trace_id_result.error());
		}
		auto cycle_result = supervisor.poll(trace_id_result.value());
		cycle_result = resume_after_suspend_returns(supervisor, config.suspend_mode(),
						    std::move(cycle_result), trace_sequence,
						    logger, *stop_controller);
		if (!cycle_result) {
			return fail_after_backlight_construction(
				logger, backlight, "daemon_cycle_failed", trace_id_result.value(),
				cycle_result.error());
		}
	}

	const auto shutdown_blank_result = force_backlight_off_for_safety(backlight);
	if (!shutdown_blank_result) {
		return fail_after_backlight_construction(
			logger, backlight, "daemon_shutdown_backlight_blank_failed",
			"panel-supervisord-stop", shutdown_blank_result.error());
	}
	logger.log(bhp::StructuredLogLevel::info, "daemon_stopped", "panel-supervisord-stop",
		   {{"brightness_percent", "0"}, {"display_state", "off"}, {"reason", "signal"}});
	return 0;
	} catch (const std::exception &) {
		const SupervisorError primary_error(
			ErrorCode::AdapterUnavailable,
			"Failed to continue panel_supervisord after backlight construction because an unexpected standard exception escaped",
			{{"exception_kind", "standard_exception"},
			 {"operation", "panel_supervisord.run_after_backlight_construction"}});
		return fail_after_backlight_construction(
			logger, backlight, "daemon_unhandled_exception_after_backlight_construction",
			"panel-supervisord-unhandled", primary_error);
	} catch (...) {
		const SupervisorError primary_error(
			ErrorCode::AdapterUnavailable,
			"Failed to continue panel_supervisord after backlight construction because an unexpected non-standard exception escaped",
			{{"exception_kind", "non_standard_exception"},
			 {"operation", "panel_supervisord.run_after_backlight_construction"}});
		return fail_after_backlight_construction(
			logger, backlight, "daemon_unhandled_exception_after_backlight_construction",
			"panel-supervisord-unhandled", primary_error);
	}
}

} // namespace
} // namespace house_panel::supervisor

int main(const int argument_count, char **argument_values)
{
	using namespace house_panel::supervisor;

	bhp::StructuredLogger logger("panel_supervisord", std::cout);
	if (argument_count == 2 && std::string_view(argument_values[1]) == "--help") {
		logger.log(bhp::StructuredLogLevel::info, "daemon_usage", "panel-supervisord-help",
			   {{"usage", "panel_supervisord (configured only by documented "
				      "BHP_PANEL_SUPERVISOR_* environment variables)"}});
		return 0;
	}
	if (argument_count != 1) {
		const std::string argument = argument_count > 1 ? argument_values[1] : "missing";
		logger.log_error("daemon_argument_validation_failed", "panel-supervisord-arguments",
				 std::string(to_string(ErrorCode::DaemonConfigurationInvalid)),
				 "panel_supervisord accepts no arguments except --help",
				 "not_available_explicit_validation_failure",
				 {{"argument", argument}});
		return 2;
	}

	try {
		return run_daemon(logger);
	} catch (const std::exception &) {
		logger.log_error(
			"daemon_unhandled_exception", "panel-supervisord-unhandled",
			std::string(to_string(ErrorCode::AdapterUnavailable)),
			"An unexpected standard exception escaped the daemon result boundary",
			"standard_exception_redacted");
	} catch (...) {
		logger.log_error(
			"daemon_unhandled_exception", "panel-supervisord-unhandled",
			std::string(to_string(ErrorCode::AdapterUnavailable)),
			"An unexpected non-standard exception escaped the daemon result boundary",
			"non_standard_exception_redacted");
	}
	return 1;
}
