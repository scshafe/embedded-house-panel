#include "fixtures/supervisor_test_fixture.hpp"

#include "panel_supervisor/backlight.hpp"
#include "panel_supervisor/clock.hpp"
#include "panel_supervisor/daemon_startup_safety.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/panel_policy.hpp"
#include "panel_supervisor/panel_supervisor.hpp"
#include "panel_supervisor/presence_source.hpp"
#include "panel_supervisor/solar_calculator.hpp"
#include "panel_supervisor/suspend.hpp"
#include "panel_supervisor/ui_mode_notifier.hpp"

#include "bhp/structured_logger.h"

#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

SupervisorError unavailable_error(const std::string &adapter_name)
{
	return SupervisorError(ErrorCode::AdapterUnavailable,
			       "Test adapter is explicitly unavailable",
			       {{"adapter", adapter_name}}, true);
}

class FakeClock final : public Clock {
public:
	Result<std::chrono::system_clock::time_point> now() override
	{
		++read_count;
		if (failure.has_value()) {
			return Result<std::chrono::system_clock::time_point>::failure(
				failure.value());
		}
		return Result<std::chrono::system_clock::time_point>::success(current_time);
	}

	std::chrono::system_clock::time_point current_time{test_fixture::kInitialTime};
	std::optional<SupervisorError> failure;
	int read_count{0};
};

class FakePresenceSource final : public PresenceSource {
public:
	Result<bool> presence_detected() override
	{
		++read_count;
		if (throw_exception) {
			throw std::runtime_error(
				"test adapter exception must not cross the supervisor boundary");
		}
		if (failure.has_value()) {
			return Result<bool>::failure(failure.value());
		}
		return Result<bool>::success(detected);
	}

	bool detected{false};
	bool throw_exception{false};
	std::optional<SupervisorError> failure;
	int read_count{0};
};

class FakeSolarCalculator final : public SolarCalculator {
public:
	Result<SolarState>
	calculate(const std::chrono::system_clock::time_point observed_at) override
	{
		++calculation_count;
		observed_times.push_back(observed_at);
		if (failure.has_value()) {
			return Result<SolarState>::failure(failure.value());
		}
		return Result<SolarState>::success(solar_state);
	}

	SolarState solar_state{SolarState::Day};
	std::optional<SupervisorError> failure;
	int calculation_count{0};
	std::vector<std::chrono::system_clock::time_point> observed_times;
};

class FakeBacklight final : public Backlight {
public:
	explicit FakeBacklight(std::vector<std::string> &calls) : calls_(calls) {}

	Result<void> set_brightness_percent(const int brightness_percent) override
	{
		++write_count;
		calls_.emplace_back("backlight");
		if (failure.has_value()) {
			return Result<void>::failure(failure.value());
		}
		applied_brightness = brightness_percent;
		return Result<void>::success();
	}

	std::optional<int> applied_brightness;
	std::optional<SupervisorError> failure;
	int write_count{0};

private:
	std::vector<std::string> &calls_;
};

class FakeUiModeNotifier final : public UiModeNotifier {
public:
	explicit FakeUiModeNotifier(std::vector<std::string> &calls) : calls_(calls) {}

	Result<void> notify_ui_mode(const UiModeNotification &notification) override
	{
		++notification_count;
		calls_.emplace_back("ui_mode");
		if (failure.has_value()) {
			return Result<void>::failure(failure.value());
		}
		notified_mode = notification.ui_mode;
		last_notification = notification;
		return Result<void>::success();
	}

	std::optional<UiMode> notified_mode;
	std::optional<UiModeNotification> last_notification;
	std::optional<SupervisorError> failure;
	int notification_count{0};

private:
	std::vector<std::string> &calls_;
};

class FakeSuspend final : public Suspend {
public:
	explicit FakeSuspend(std::vector<std::string> &calls) : calls_(calls) {}

	Result<void> request_suspend() override
	{
		++request_count;
		calls_.emplace_back("suspend");
		if (failure.has_value()) {
			return Result<void>::failure(failure.value());
		}
		return Result<void>::success();
	}

	std::optional<SupervisorError> failure;
	int request_count{0};

private:
	std::vector<std::string> &calls_;
};

struct SupervisorFixture {
	SupervisorFixture()
	    : logger("panel_supervisor_test", log_output), backlight(calls),
	      ui_mode_notifier(calls), suspend(calls),
	      supervisor(test_fixture::valid_config().value(), PanelPolicy{}, presence_source,
			 clock, solar_calculator, backlight, ui_mode_notifier, suspend, logger)
	{
	}

	std::ostringstream log_output;
	bhp::StructuredLogger logger;
	std::vector<std::string> calls;
	FakeClock clock;
	FakePresenceSource presence_source;
	FakeSolarCalculator solar_calculator;
	FakeBacklight backlight;
	FakeUiModeNotifier ui_mode_notifier;
	FakeSuspend suspend;
	PanelSupervisor supervisor;
};

bool resume_reacquires_all_inputs_and_applies_fresh_night_state()
{
	SupervisorFixture fixture;
	const auto start_result = fixture.supervisor.start("resume-fresh-start");
	TEST_REQUIRE(start_result.has_value());
	TEST_REQUIRE(start_result.value().system_state == SystemState::Suspended);
	TEST_REQUIRE(fixture.suspend.request_count == 1);

	fixture.presence_source.detected = true;
	fixture.solar_calculator.solar_state = SolarState::Night;
	fixture.clock.current_time += std::chrono::hours(8);
	fixture.calls.clear();
	const auto resume_result = fixture.supervisor.resume("resume-fresh-event");

	TEST_REQUIRE(resume_result.has_value());
	TEST_REQUIRE(resume_result.value().presence_state == PresenceState::Present);
	TEST_REQUIRE(resume_result.value().solar_state == SolarState::Night);
	TEST_REQUIRE(resume_result.value().system_state == SystemState::Active);
	TEST_REQUIRE(resume_result.value().applied_brightness_percent ==
		     test_fixture::kPresentNightBrightnessPercent);
	TEST_REQUIRE(resume_result.value().notified_ui_mode == UiMode::Night);
	TEST_REQUIRE(fixture.clock.read_count == 2);
	TEST_REQUIRE(fixture.presence_source.read_count == 2);
	TEST_REQUIRE(fixture.solar_calculator.calculation_count == 2);
	TEST_REQUIRE(fixture.solar_calculator.observed_times.back() == fixture.clock.current_time);
	const std::vector<std::string> expected_resume_calls{"ui_mode", "backlight"};
	TEST_REQUIRE(fixture.calls == expected_resume_calls);
	TEST_REQUIRE(fixture.log_output.str().find("resume_recomputation_started") !=
		     std::string::npos);
	return true;
}

bool resume_while_still_absent_requests_suspend_again()
{
	SupervisorFixture fixture;
	TEST_REQUIRE(fixture.supervisor.start("resume-absent-start").has_value());
	TEST_REQUIRE(fixture.suspend.request_count == 1);

	fixture.clock.current_time += std::chrono::hours(1);
	const auto resume_result = fixture.supervisor.resume("resume-absent-event");

	TEST_REQUIRE(resume_result.has_value());
	TEST_REQUIRE(resume_result.value().system_state == SystemState::Suspended);
	TEST_REQUIRE(fixture.suspend.request_count == 2);
	return true;
}

bool initial_absence_applies_outputs_before_requesting_suspend()
{
	SupervisorFixture fixture;
	const auto result = fixture.supervisor.start("ordered-suspend");

	TEST_REQUIRE(result.has_value());
	const std::vector<std::string> expected_calls{"backlight", "ui_mode", "suspend"};
	TEST_REQUIRE(fixture.calls == expected_calls);
	TEST_REQUIRE(fixture.backlight.applied_brightness ==
		     test_fixture::kSuspendedBrightnessPercent);
	TEST_REQUIRE(fixture.ui_mode_notifier.notified_mode == UiMode::Suspended);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification.has_value());
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->presence_state ==
		     PresenceState::Absent);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->solar_state == SolarState::Day);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->system_state ==
		     SystemState::Suspended);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->effective_brightness_percent ==
		     test_fixture::kSuspendedBrightnessPercent);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->observed_at ==
		     fixture.clock.current_time);
	return true;
}

bool initial_active_mode_is_notified_before_nonzero_backlight()
{
	SupervisorFixture fixture;
	fixture.presence_source.detected = true;
	const auto result = fixture.supervisor.start("ordered-active");

	TEST_REQUIRE(result.has_value());
	const std::vector<std::string> expected_calls{"ui_mode", "backlight"};
	TEST_REQUIRE(fixture.calls == expected_calls);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification.has_value());
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->ui_mode == UiMode::Day);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->system_state ==
		     SystemState::Active);
	TEST_REQUIRE(fixture.ui_mode_notifier.last_notification->effective_brightness_percent ==
		     test_fixture::kPresentDayBrightnessPercent);
	return true;
}

bool presence_failure_is_wrapped_and_stops_downstream_outputs()
{
	SupervisorFixture fixture;
	fixture.presence_source.failure = unavailable_error("fake_presence_source");
	const auto result = fixture.supervisor.start("presence-failure");

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::PresenceReadFailed);
	TEST_REQUIRE(result.error().cause() != nullptr);
	TEST_REQUIRE(result.error().cause()->code() == ErrorCode::AdapterUnavailable);
	TEST_REQUIRE(fixture.solar_calculator.calculation_count == 0);
	TEST_REQUIRE(fixture.backlight.write_count == 0);
	TEST_REQUIRE(fixture.supervisor.state().system_state == SystemState::Faulted);
	TEST_REQUIRE(fixture.log_output.str().find("PANEL_SUPERVISOR.PRESENCE_READ_FAILED") !=
		     std::string::npos);
	return true;
}

bool empty_trace_id_returns_typed_error_without_reading_ports()
{
	SupervisorFixture fixture;
	const auto result = fixture.supervisor.poll("");

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::InvalidTraceId);
	TEST_REQUIRE(fixture.clock.read_count == 0);
	TEST_REQUIRE(fixture.presence_source.read_count == 0);
	return true;
}

bool thrown_adapter_exception_is_converted_to_typed_failure()
{
	SupervisorFixture fixture;
	fixture.presence_source.throw_exception = true;
	const auto result = fixture.supervisor.start("presence-exception");

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::PresenceReadFailed);
	TEST_REQUIRE(result.error().cause() != nullptr);
	TEST_REQUIRE(result.error().cause()->code() == ErrorCode::AdapterUnavailable);
	TEST_REQUIRE(result.error().cause()->context().at("exception_kind") ==
		     "standard_exception");
	TEST_REQUIRE(fixture.supervisor.state().system_state == SystemState::Faulted);
	return true;
}

bool failed_active_ui_notification_does_not_raise_backlight()
{
	SupervisorFixture fixture;
	fixture.presence_source.detected = true;
	fixture.ui_mode_notifier.failure = unavailable_error("fake_ui_mode_notifier");
	const auto result = fixture.supervisor.start("ui-mode-failure");

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::UiModeNotificationFailed);
	TEST_REQUIRE(!fixture.supervisor.state().applied_brightness_percent.has_value());
	TEST_REQUIRE(!fixture.supervisor.state().notified_ui_mode.has_value());
	TEST_REQUIRE(fixture.backlight.write_count == 0);
	TEST_REQUIRE(fixture.supervisor.state().system_state == SystemState::Faulted);
	TEST_REQUIRE(fixture.suspend.request_count == 0);
	return true;
}

bool failed_active_backlight_preserves_successful_ui_notification()
{
	SupervisorFixture fixture;
	fixture.presence_source.detected = true;
	fixture.backlight.failure = unavailable_error("fake_backlight");
	const auto result = fixture.supervisor.start("backlight-failure");

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::BacklightWriteFailed);
	TEST_REQUIRE(fixture.supervisor.state().notified_ui_mode == UiMode::Day);
	TEST_REQUIRE(!fixture.supervisor.state().applied_brightness_percent.has_value());
	const std::vector<std::string> expected_calls{"ui_mode", "backlight"};
	TEST_REQUIRE(fixture.calls == expected_calls);
	TEST_REQUIRE(fixture.supervisor.state().system_state == SystemState::Faulted);
	return true;
}

bool safety_backlight_blank_writes_only_exact_zero_and_wraps_failure()
{
	SupervisorFixture fixture;
	const auto success_result = force_backlight_off_for_safety(fixture.backlight);
	TEST_REQUIRE(success_result.has_value());
	TEST_REQUIRE(fixture.backlight.write_count == 1);
	TEST_REQUIRE(fixture.backlight.applied_brightness == kSafeBacklightOffPercent);

	fixture.backlight.failure = unavailable_error("fake_backlight");
	const auto failure_result = force_backlight_off_for_safety(fixture.backlight);
	TEST_REQUIRE(!failure_result.has_value());
	TEST_REQUIRE(failure_result.error().code() == ErrorCode::BacklightWriteFailed);
	TEST_REQUIRE(failure_result.error().cause() != nullptr);
	TEST_REQUIRE(failure_result.error().cause()->code() == ErrorCode::AdapterUnavailable);
	return true;
}

bool safety_blank_turns_off_a_lit_backlight_after_runtime_clock_failure()
{
	SupervisorFixture fixture;
	fixture.presence_source.detected = true;
	TEST_REQUIRE(fixture.supervisor.start("runtime-clock-initial-active").has_value());
	TEST_REQUIRE(fixture.backlight.applied_brightness ==
		     test_fixture::kPresentDayBrightnessPercent);

	fixture.clock.failure = SupervisorError(
		ErrorCode::SystemClockUntrusted, "Test runtime clock became untrusted",
		{{"operation", "fake_clock.validate_synchronization"}}, true);
	const auto clock_failure = fixture.supervisor.poll("runtime-clock-untrusted");
	TEST_REQUIRE(!clock_failure.has_value());
	TEST_REQUIRE(clock_failure.error().code() == ErrorCode::ClockReadFailed);

	const auto blank_result = force_backlight_off_for_safety(fixture.backlight);
	TEST_REQUIRE(blank_result.has_value());
	TEST_REQUIRE(fixture.backlight.applied_brightness == kSafeBacklightOffPercent);
	return true;
}

bool fatal_blank_error_preserves_primary_and_blank_failure_evidence()
{
	const SupervisorError primary_error = SupervisorError::wrap(
		ErrorCode::PresenceReadFailed, "Test primary presence acquisition failed",
		SupervisorError(ErrorCode::AdapterUnavailable,
				"Test presence adapter unavailable", {}, true),
		{}, true);
	const SupervisorError blank_error(
		ErrorCode::BacklightDeviceWriteFailed, "Test emergency blank write failed",
		{{"operation", "fake_backlight.set_brightness_percent"}}, true);
	const std::string primary_chain =
		"PANEL_SUPERVISOR.PRESENCE_READ_FAILED <- PANEL_SUPERVISOR.ADAPTER_UNAVAILABLE";

	const auto safety_error = make_fatal_exit_backlight_blank_error(
		primary_error, blank_error, primary_chain);
	TEST_REQUIRE(safety_error.code() == ErrorCode::BacklightWriteFailed);
	TEST_REQUIRE(safety_error.cause() != nullptr);
	TEST_REQUIRE(safety_error.cause()->code() == ErrorCode::BacklightDeviceWriteFailed);
	TEST_REQUIRE(safety_error.context().at("primary_error_code") ==
		     "PANEL_SUPERVISOR.PRESENCE_READ_FAILED");
	TEST_REQUIRE(safety_error.context().at("primary_error_message") ==
		     primary_error.message());
	TEST_REQUIRE(safety_error.context().at("primary_stack_trace") == primary_chain);
	TEST_REQUIRE(safety_error.retryable());
	return true;
}

bool retryable_untrusted_startup_clock_can_be_retried_without_intermediate_output()
{
	SupervisorFixture fixture;
	fixture.clock.failure = SupervisorError(
		ErrorCode::SystemClockUntrusted,
		"Test clock is not synchronized yet",
		{{"operation", "fake_clock.validate_synchronization"}}, true);
	const auto untrusted_result = fixture.supervisor.start("clock-untrusted");

	TEST_REQUIRE(!untrusted_result.has_value());
	TEST_REQUIRE(is_retryable_startup_clock_readiness_failure(untrusted_result.error()));
	TEST_REQUIRE(fixture.clock.read_count == 1);
	TEST_REQUIRE(fixture.presence_source.read_count == 0);
	TEST_REQUIRE(fixture.solar_calculator.calculation_count == 0);
	TEST_REQUIRE(fixture.backlight.write_count == 0);
	TEST_REQUIRE(fixture.ui_mode_notifier.notification_count == 0);
	TEST_REQUIRE(fixture.suspend.request_count == 0);

	fixture.clock.failure.reset();
	const auto synchronized_result = fixture.supervisor.start("clock-synchronized");
	TEST_REQUIRE(synchronized_result.has_value());
	TEST_REQUIRE(fixture.clock.read_count == 2);
	TEST_REQUIRE(fixture.presence_source.read_count == 1);
	TEST_REQUIRE(fixture.solar_calculator.calculation_count == 1);
	return true;
}

bool startup_clock_retry_classifier_rejects_nonretryable_and_nonclock_failures()
{
	const SupervisorError retryable_query = SupervisorError::wrap(
		ErrorCode::ClockReadFailed, "Test retryable clock query failure",
		SupervisorError(ErrorCode::SystemClockSynchronizationQueryFailed,
				"Test interrupted synchronization query", {}, true),
		{}, true);
	TEST_REQUIRE(is_retryable_startup_clock_readiness_failure(retryable_query));

	const SupervisorError nonretryable_readiness = SupervisorError::wrap(
		ErrorCode::ClockReadFailed, "Test nonretryable clock query failure",
		SupervisorError(ErrorCode::SystemClockSynchronizationQueryFailed,
				"Test permanent synchronization query failure"),
		{}, false);
	TEST_REQUIRE(!is_retryable_startup_clock_readiness_failure(nonretryable_readiness));

	const SupervisorError retryable_presence = SupervisorError::wrap(
		ErrorCode::PresenceReadFailed, "Test retryable presence failure",
		SupervisorError(ErrorCode::AdapterUnavailable,
				"Test transient presence adapter failure", {}, true),
		{}, true);
	TEST_REQUIRE(!is_retryable_startup_clock_readiness_failure(retryable_presence));
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 14> tests{{
		{"resume_reacquires_all_inputs_and_applies_fresh_night_state",
		 resume_reacquires_all_inputs_and_applies_fresh_night_state},
		{"resume_while_still_absent_requests_suspend_again",
		 resume_while_still_absent_requests_suspend_again},
		{"initial_absence_applies_outputs_before_requesting_suspend",
		 initial_absence_applies_outputs_before_requesting_suspend},
		{"initial_active_mode_is_notified_before_nonzero_backlight",
		 initial_active_mode_is_notified_before_nonzero_backlight},
		{"presence_failure_is_wrapped_and_stops_downstream_outputs",
		 presence_failure_is_wrapped_and_stops_downstream_outputs},
		{"empty_trace_id_returns_typed_error_without_reading_ports",
		 empty_trace_id_returns_typed_error_without_reading_ports},
		{"thrown_adapter_exception_is_converted_to_typed_failure",
		 thrown_adapter_exception_is_converted_to_typed_failure},
		{"failed_active_ui_notification_does_not_raise_backlight",
		 failed_active_ui_notification_does_not_raise_backlight},
		{"failed_active_backlight_preserves_successful_ui_notification",
		 failed_active_backlight_preserves_successful_ui_notification},
		{"safety_backlight_blank_writes_only_exact_zero_and_wraps_failure",
		 safety_backlight_blank_writes_only_exact_zero_and_wraps_failure},
		{"safety_blank_turns_off_a_lit_backlight_after_runtime_clock_failure",
		 safety_blank_turns_off_a_lit_backlight_after_runtime_clock_failure},
		{"fatal_blank_error_preserves_primary_and_blank_failure_evidence",
		 fatal_blank_error_preserves_primary_and_blank_failure_evidence},
		{"retryable_untrusted_startup_clock_can_be_retried_without_intermediate_output",
		 retryable_untrusted_startup_clock_can_be_retried_without_intermediate_output},
		{"startup_clock_retry_classifier_rejects_nonretryable_and_nonclock_failures",
		 startup_clock_retry_classifier_rejects_nonretryable_and_nonclock_failures},
	}};

	for (const auto &[test_name, test_function] : tests) {
		if (!test_function()) {
			std::cerr << "FAILED: " << test_name << '\n';
			return 1;
		}
	}
	std::cout << "PASS: " << tests.size() << " panel supervisor tests\n";
	return 0;
}
