#include "panel_supervisor/backlight.hpp"
#include "panel_supervisor/clock.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/panel_policy.hpp"
#include "panel_supervisor/panel_supervisor.hpp"
#include "panel_supervisor/presence_source.hpp"
#include "panel_supervisor/solar_calculator.hpp"
#include "panel_supervisor/supervisor_config.hpp"
#include "panel_supervisor/suspend.hpp"
#include "panel_supervisor/ui_mode_notifier.hpp"

#include "bhp/structured_log_level.h"
#include "bhp/structured_logger.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace house_panel::supervisor {
namespace {

inline constexpr long long kSimulationStartEpochMilliseconds = 1'800'000'000'000LL;

struct SimulationOptions {
	long long grace_period_ms{5000};
	BrightnessLevels brightness_levels{80, 25, 45, 15, 0};
};

class SimulationClock final : public Clock {
public:
	Result<std::chrono::system_clock::time_point> now() override
	{
		return Result<std::chrono::system_clock::time_point>::success(now_);
	}

	void advance(const std::chrono::milliseconds duration) { now_ += duration; }

private:
	std::chrono::system_clock::time_point now_{
		std::chrono::milliseconds(kSimulationStartEpochMilliseconds)};
};

class SimulationPresenceSource final : public PresenceSource {
public:
	Result<bool> presence_detected() override { return Result<bool>::success(detected_); }

	void set_detected(const bool detected) noexcept { detected_ = detected; }

private:
	bool detected_{true};
};

class SimulationSolarCalculator final : public SolarCalculator {
public:
	Result<SolarState> calculate(std::chrono::system_clock::time_point) override
	{
		return Result<SolarState>::success(solar_state_);
	}

	void set_solar_state(const SolarState solar_state) noexcept { solar_state_ = solar_state; }

private:
	SolarState solar_state_{SolarState::Day};
};

class SimulationBacklight final : public Backlight {
public:
	Result<void> set_brightness_percent(const int brightness_percent) override
	{
		brightness_percent_ = brightness_percent;
		return Result<void>::success();
	}

	[[nodiscard]] int brightness_percent() const noexcept { return brightness_percent_; }

private:
	int brightness_percent_{0};
};

class SimulationUiModeNotifier final : public UiModeNotifier {
public:
	Result<PendingPresentation> publish(const UiModeNotification &notification) override
	{
		ui_mode_ = notification.ui_mode;
		++publication_sequence_;
		return Result<PendingPresentation>::success(
			PendingPresentation{publication_sequence_});
	}

	PresentationOutcome await_presentation(
		const PendingPresentation &pending_presentation,
		std::chrono::steady_clock::time_point deadline) override
	{
		(void)deadline;
		if (pending_presentation.publication_sequence != publication_sequence_) {
			return PresentationNotCommitted{PresentationNotCommittedReason::Stale};
		}
		return PresentationCommitted{};
	}

	[[nodiscard]] UiMode ui_mode() const noexcept { return ui_mode_; }

private:
	UiMode ui_mode_{UiMode::Suspended};
	std::uint64_t publication_sequence_{0};
};

class SimulationSuspend final : public Suspend {
public:
	Result<void> request_suspend() override
	{
		++request_count_;
		return Result<void>::success();
	}

	[[nodiscard]] std::uint64_t request_count() const noexcept { return request_count_; }

private:
	std::uint64_t request_count_{0};
};

Result<long long> parse_integer(std::string_view option, std::string_view value)
{
	long long parsed_value = 0;
	const auto parse_result =
		std::from_chars(value.data(), value.data() + value.size(), parsed_value);
	if (parse_result.ec != std::errc{} || parse_result.ptr != value.data() + value.size()) {
		return Result<long long>::failure(SupervisorError(
			ErrorCode::SimulationInvalidArgument,
			"Failed to parse panel supervisor simulation integer argument",
			{{"option", std::string(option)}, {"value", std::string(value)}}));
	}
	return Result<long long>::success(parsed_value);
}

Result<SimulationOptions> parse_options(const int argument_count, char **argument_values)
{
	SimulationOptions options;
	for (int index = 1; index < argument_count; index += 2) {
		const std::string_view option(argument_values[index]);
		if (option == "--help") {
			return Result<SimulationOptions>::failure(SupervisorError(
				ErrorCode::SimulationInvalidArgument,
				"Panel supervisor simulation help requested",
				{{"usage",
				  "panel_supervisor_simulation [--grace-ms N] [--present-day N] "
				  "[--present-night N] [--grace-day N] [--grace-night N] "
				  "[--suspended 0]"}}));
		}
		if (index + 1 >= argument_count) {
			return Result<SimulationOptions>::failure(
				SupervisorError(ErrorCode::SimulationInvalidArgument,
						"Failed to parse panel supervisor simulation "
						"arguments because an option has no value",
						{{"option", std::string(option)}}));
		}

		const auto value_result = parse_integer(option, argument_values[index + 1]);
		if (!value_result) {
			return Result<SimulationOptions>::failure(value_result.error());
		}
		const auto value = value_result.value();
		const bool is_brightness_option =
			option == "--present-day" || option == "--present-night" ||
			option == "--grace-day" || option == "--grace-night" ||
			option == "--suspended";
		if (option != "--grace-ms" && !is_brightness_option) {
			return Result<SimulationOptions>::failure(
				SupervisorError(ErrorCode::SimulationInvalidArgument,
						"Failed to parse panel supervisor simulation "
						"arguments because an option is unknown",
						{{"option", std::string(option)}}));
		}
		if (is_brightness_option && (value < std::numeric_limits<int>::min() ||
					     value > std::numeric_limits<int>::max())) {
			return Result<SimulationOptions>::failure(
				SupervisorError(ErrorCode::SimulationInvalidArgument,
						"Failed to parse panel supervisor simulation "
						"argument because it exceeds integer range",
						{{"option", std::string(option)},
						 {"value", std::to_string(value)}}));
		}

		if (option == "--grace-ms") {
			options.grace_period_ms = value;
		} else if (option == "--present-day") {
			options.brightness_levels.present_day_percent = static_cast<int>(value);
		} else if (option == "--present-night") {
			options.brightness_levels.present_night_percent = static_cast<int>(value);
		} else if (option == "--grace-day") {
			options.brightness_levels.grace_day_percent = static_cast<int>(value);
		} else if (option == "--grace-night") {
			options.brightness_levels.grace_night_percent = static_cast<int>(value);
		} else if (option == "--suspended") {
			options.brightness_levels.suspended_percent = static_cast<int>(value);
		}
	}

	const auto maximum_epoch_milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::time_point::max().time_since_epoch())
			.count();
	const auto maximum_safe_grace_period_ms =
		maximum_epoch_milliseconds - kSimulationStartEpochMilliseconds - 2;
	if (options.grace_period_ms > maximum_safe_grace_period_ms) {
		return Result<SimulationOptions>::failure(SupervisorError(
			ErrorCode::SimulationInvalidArgument,
			"Failed to configure simulation because grace period exceeds its clock "
			"range",
			{{"grace_period_ms", std::to_string(options.grace_period_ms)},
			 {"maximum_safe_grace_period_ms",
			  std::to_string(maximum_safe_grace_period_ms)}}));
	}
	return Result<SimulationOptions>::success(options);
}

bool log_cycle_failure(bhp::StructuredLogger &logger, const std::string &trace_id,
		       const Result<PanelState> &result)
{
	if (result) {
		return false;
	}
	logger.log_error("simulation_cycle_failed", trace_id,
			 std::string(to_string(result.error().code())), result.error().message(),
			 "not_available_explicit_error_return", result.error().context());
	return true;
}

} // namespace
} // namespace house_panel::supervisor

int main(const int argument_count, char **argument_values)
{
	using namespace house_panel::supervisor;

	bhp::StructuredLogger logger("panel_supervisor_simulation", std::cout);
	if (argument_count == 2 && std::string_view(argument_values[1]) == "--help") {
		logger.log(
			bhp::StructuredLogLevel::info, "simulation_usage", "simulation-help",
			{{"usage", "panel_supervisor_simulation [--grace-ms N] [--present-day N] "
				   "[--present-night N] [--grace-day N] [--grace-night N] "
				   "[--suspended 0]"}});
		return 0;
	}
	const auto options_result = parse_options(argument_count, argument_values);
	if (!options_result) {
		logger.log_error("simulation_argument_validation_failed", "simulation-arguments",
				 std::string(to_string(options_result.error().code())),
				 options_result.error().message(),
				 "not_available_explicit_error_return",
				 options_result.error().context());
		return 2;
	}
	const auto options = options_result.value();

	const auto config_result = SupervisorConfig::create(
		std::chrono::milliseconds(options.grace_period_ms), options.brightness_levels);
	if (!config_result) {
		logger.log_error(
			"simulation_configuration_validation_failed", "simulation-configuration",
			std::string(to_string(config_result.error().code())),
			config_result.error().message(), "not_available_explicit_error_return",
			config_result.error().context());
		return 2;
	}

	SimulationClock clock;
	SimulationPresenceSource presence_source;
	SimulationSolarCalculator solar_calculator;
	SimulationBacklight backlight;
	SimulationUiModeNotifier ui_mode_notifier;
	SimulationSuspend suspend;
	PanelSupervisor supervisor(config_result.value(), PanelPolicy{}, presence_source, clock,
				   solar_calculator, backlight, ui_mode_notifier, suspend, logger);

	auto cycle_result = supervisor.start("simulation-start");
	if (log_cycle_failure(logger, "simulation-start", cycle_result)) {
		return 1;
	}

	presence_source.set_detected(false);
	const auto before_grace_advance = std::chrono::milliseconds(options.grace_period_ms / 2);
	clock.advance(before_grace_advance);
	cycle_result = supervisor.poll("simulation-grace");
	if (log_cycle_failure(logger, "simulation-grace", cycle_result)) {
		return 1;
	}

	const auto after_grace_advance = std::chrono::milliseconds(
		options.grace_period_ms - before_grace_advance.count() + 1);
	clock.advance(after_grace_advance);
	cycle_result = supervisor.poll("simulation-absence");
	if (log_cycle_failure(logger, "simulation-absence", cycle_result)) {
		return 1;
	}

	presence_source.set_detected(true);
	solar_calculator.set_solar_state(SolarState::Night);
	clock.advance(std::chrono::milliseconds(1));
	cycle_result = supervisor.resume("simulation-resume");
	if (log_cycle_failure(logger, "simulation-resume", cycle_result)) {
		return 1;
	}

	logger.log(bhp::StructuredLogLevel::info, "simulation_completed", "simulation-summary",
		   {{"brightness_percent", std::to_string(backlight.brightness_percent())},
		    {"policy_revision", std::to_string(supervisor.state().policy_revision)},
		    {"presence_state", std::string(to_string(supervisor.state().presence_state))},
		    {"solar_state", std::string(to_string(supervisor.state().solar_state))},
		    {"suspend_request_count", std::to_string(suspend.request_count())},
		    {"system_state", std::string(to_string(supervisor.state().system_state))},
		    {"ui_mode", std::string(to_string(ui_mode_notifier.ui_mode()))}});
	return 0;
}
